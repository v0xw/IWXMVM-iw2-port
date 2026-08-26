#pragma once
#include "Camera.hpp"
#include "Types/BoneData.hpp"

namespace IWXMVM::Components
{
    class BoneCamera : public Camera
    {
       public:
        BoneCamera()
        {
            this->mode = Camera::Mode::Bone;
            entityId = 0;
            boneIndex = 0;
            positionOffset = glm::vec3(0);
            rotationOffset = glm::vec3(0);
            useTemporalSmoothing = false;
            showBone = true;
        }

        void Initialize() override;
        void Update() override;

        int32_t& GetEntityId() { return entityId; }
        int32_t& GetBoneIndex() { return boneIndex; }
        glm::vec3& GetPositionOffset()
        {
            return positionOffset;
        }
        glm::vec3& GetRotationOffset()
        {
            return rotationOffset;
        }

        bool& UseTemporalSmoothing()
        {
            return useTemporalSmoothing;
        }

        // Temporal smoothing is implemented for IW2 only; the other games keep the disabled
        // placeholder in the camera menu. Flipping this predicate is all it takes to offer the
        // option elsewhere once it has been tested there.
        static bool IsTemporalSmoothingSupported();

        bool& ShowBone()
        {
            return showBone;
        }

       private:
        int32_t entityId;
        int32_t boneIndex;

        glm::vec3 positionOffset;
        glm::vec3 rotationOffset;

        bool useTemporalSmoothing;
        bool showBone;

        // smoothing state, advanced in demo time so the preview and a capture of the same tick
        // produce the same camera
        bool hasSmoothingState = false;
        int32_t smoothingEntityId = -1;
        int32_t smoothingBoneIndex = -1;
        uint32_t smoothingTick = 0;
        glm::vec3 smoothedBonePosition{};
        glm::quat smoothedBoneRotation{};

        Types::BoneData SmoothBoneData(const Types::BoneData& boneData, int32_t sourceEntityId);
        void ResetSmoothing();

        void SetPositionFromBoneData(const Types::BoneData& boneData);
        void HandleInput(const Types::BoneData& boneData);
    };
}  // namespace IWXMVM::Components