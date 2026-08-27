#include "StdInclude.hpp"
#include "BoneCamera.hpp"

#include "Mod.hpp"
#include "Events.hpp"
#include "Components/FreeCamera.hpp"
#include "Components/Playback.hpp"
#include "Utilities/MathUtils.hpp"
#include "UI/UIManager.hpp"

namespace IWXMVM::Components
{
    namespace
    {
        // Time the camera takes to cover ~63% of the distance to the bone. Small enough that the
        // camera stays attached to a sprinting player, large enough to swallow the animation jitter
        // (weapon bob, recoil, footstep jolts) that makes the raw bone unusable as a camera mount.
        constexpr float SMOOTHING_TIME_CONSTANT = 0.1f;
    }  // namespace

    bool BoneCamera::IsTemporalSmoothingSupported()
    {
        return (Mod::GetGameInterface()->GetSupportedFeatures() & Types::Features_TemporalBoneSmoothing) != 0;
    }

    void BoneCamera::Initialize()
    {
        positionOffset = glm::vec3(0, 10, 0);

        Events::RegisterListener(EventType::PostDemoLoad, [&]() {
            entityId = 0;
            ResetSmoothing();
        });
    }

    void BoneCamera::ResetSmoothing()
    {
        hasSmoothingState = false;
    }

    Types::BoneData BoneCamera::SmoothBoneData(const Types::BoneData& boneData, int32_t sourceEntityId)
    {
        if (!useTemporalSmoothing || !IsTemporalSmoothingSupported() || boneData.id == -1)
        {
            // no usable bone to filter from; start fresh on the next valid one rather than gliding
            // across whatever moved in the meantime
            ResetSmoothing();
            return boneData;
        }

        const auto tick = Playback::GetTimelineTick();
        const auto targetRotation = glm::normalize(glm::quat(boneData.rotation));

        // Snap rather than glide whenever the source or the timeline is discontinuous: a different
        // player or bone, a rewind, or the first frame after smoothing was switched on.
        if (!hasSmoothingState || sourceEntityId != smoothingEntityId || boneIndex != smoothingBoneIndex ||
            tick < smoothingTick)
        {
            hasSmoothingState = true;
            smoothingEntityId = sourceEntityId;
            smoothingBoneIndex = boneIndex;
            smoothingTick = tick;
            smoothedBonePosition = boneData.position;
            smoothedBoneRotation = targetRotation;
            return boneData;
        }

        // Demo time, not real time: the preview and a capture of the same tick agree, and every
        // pass of a multipass frame - which all share one demo tick - sees the same camera. A large
        // forward jump lands on an alpha of ~1, which is a snap, so scrubbing needs no special case.
        const auto deltaSeconds = static_cast<float>(tick - smoothingTick) / 1000.0f;
        smoothingTick = tick;

        if (deltaSeconds > 0.0f)
        {
            const auto alpha = glm::clamp(1.0f - std::exp(-deltaSeconds / SMOOTHING_TIME_CONSTANT), 0.0f, 1.0f);
            smoothedBonePosition = glm::mix(smoothedBonePosition, boneData.position, alpha);
            smoothedBoneRotation = glm::normalize(glm::slerp(smoothedBoneRotation, targetRotation, alpha));
        }

        Types::BoneData smoothed = boneData;
        smoothed.position = smoothedBonePosition;
        smoothed.rotation = glm::mat3_cast(smoothedBoneRotation);
        return smoothed;
    }

    void BoneCamera::SetPositionFromBoneData(const Types::BoneData& boneData)
    {
        if (boneData.id == -1)
            return;

        this->position = boneData.position + boneData.rotation * positionOffset;

        auto qBoneRotation = glm::quat(boneData.rotation);
        auto qRotationOffset = glm::quat(glm::radians(rotationOffset));
        auto newRotation = glm::degrees(glm::eulerAngles(qBoneRotation * qRotationOffset));

        // im sure theres a glm way to fix our orientation without having
        // to do this manually (also in Graphics.cpp), but im just happy this works right now
        this->rotation[0] = newRotation[1];
        this->rotation[1] = newRotation[2];
        this->rotation[2] = newRotation[0];
    }

    void BoneCamera::HandleInput(const Types::BoneData& boneData)
    {
        if (!UI::UIManager::Get().GetUIComponent(UI::Component::GameView)->HasFocus())
            return;
        auto worldSpacePosition = boneData.position + boneData.rotation * positionOffset;


        glm::vec3 adjustedRotationOffset = 
        {
            rotationOffset[1], 
            rotationOffset[2],
            rotationOffset[0]
        };

        FreeCamera::HandleFreecamInput(worldSpacePosition, adjustedRotationOffset, fov, GetForwardVector(),
                                       GetRightVector());

        rotationOffset = 
        {
            adjustedRotationOffset[2],
            adjustedRotationOffset[0],  
            adjustedRotationOffset[1]  
        };

        positionOffset = boneData.rotation / (worldSpacePosition - boneData.position);
    }
    
    void BoneCamera::Update()
    {
        const auto& bones = Mod::GetGameInterface()->GetSupportedBoneNames();
        const std::string& selectedBoneName = bones.at(boneIndex);
        const auto selectedEntityId = entityId;
        const auto& boneData = Mod::GetGameInterface()->GetBoneData(selectedEntityId, selectedBoneName);

        auto entities = Mod::GetGameInterface()->GetEntities();
        auto selectedEntity = entities.at(selectedEntityId);
        if (!selectedEntity.isValid && selectedEntity.type == Types::EntityType::Player)
        {
            for (const auto& entity : entities)
            {
                if (entity.type == Types::EntityType::Corpse && entity.clientNum == selectedEntityId)
                {
                    SetPositionFromBoneData(
                        SmoothBoneData(Mod::GetGameInterface()->GetBoneData(entity.id, selectedBoneName), entity.id));
                    return;
                }
            }
        }

        // the input handler derives the position offset from the same frame that gets rendered, so
        // dragging the camera stays consistent while smoothing is on
        const auto smoothedBoneData = SmoothBoneData(boneData, selectedEntityId);

        HandleInput(smoothedBoneData);

        SetPositionFromBoneData(smoothedBoneData);
    }

}  // namespace IWXMVM::Components
