#include "StdInclude.hpp"
#include "Camera.hpp"

#include "Components/CameraManager.hpp"
#include "Utilities/HookManager.hpp"
#include "Mod.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"
#include "../Functions.hpp"

namespace IWXMVM::IW2::Hooks::Camera
{
    using namespace Structures;

    float firstPersonFOV = 0.0f;

    float CalculateFovY(float fovX, const refdef_t& refdef)
    {
        if (refdef.width <= 0 || refdef.height <= 0)
            return fovX * 0.75f;

        const auto aspect = static_cast<float>(refdef.height) / static_cast<float>(refdef.width);
        return glm::degrees(2.0f * std::atan(std::tan(glm::radians(fovX) * 0.5f) * aspect));
    }

    void ApplyFov(refdef_t& refdef, float fovX)
    {
        refdef.fov_x = fovX;
        refdef.fov_y = CalculateFovY(fovX, refdef);
    }

    // CG_CalcViewValues computes the complete refdef (origin, angles -> axis, fov) for the current frame.
    // Everything that follows (sound listener, view weapon, FX camera, R_RenderScene) consumes cg.refdef, so
    // rewriting it right after the original ran is enough to drive the camera.
    typedef void(__cdecl* CG_CalcViewValues_t)();
    CG_CalcViewValues_t CG_CalcViewValues_Trampoline = nullptr;

    void __cdecl CG_CalcViewValues_Hook()
    {
        CG_CalcViewValues_Trampoline();

        auto& refdef = GetRefdef();
        auto* viewAngles = GetRefdefViewAngles();
        auto& camera = Components::CameraManager::Get().GetActiveCamera();

        if (!camera->IsModControlledCameraMode())
        {
            // keep the mod camera in sync with whatever the game is showing
            camera->GetPosition() = glm::make_vec3(refdef.vieworg);
            camera->GetRotation() = glm::make_vec3(viewAngles);

            if (firstPersonFOV > 0.0f && camera->GetMode() == Components::Camera::Mode::FirstPerson)
            {
                ApplyFov(refdef, firstPersonFOV);
            }

            camera->GetFov() = refdef.fov_x;
            return;
        }

        const auto& position = camera->GetPosition();
        const auto& rotation = camera->GetRotation();

        refdef.vieworg[0] = position.x;
        refdef.vieworg[1] = position.y;
        refdef.vieworg[2] = position.z;

        viewAngles[0] = rotation.x;
        viewAngles[1] = rotation.y;
        viewAngles[2] = rotation.z;

        Functions::AnglesToAxis(viewAngles, refdef.viewaxis);
        ApplyFov(refdef, camera->GetFov());
    }

    void OnCameraChanged()
    {
        auto& camera = Components::CameraManager::Get().GetActiveCamera();
        const auto isModControlled = camera->IsModControlledCameraMode();
        const auto isThirdPerson = camera->GetMode() == Components::Camera::Mode::ThirdPerson;

        // cg_thirdPerson / cg_drawGun are cheat protected; write the values directly (only once cgame has
        // registered them with their real type - an implicit string dvar must not be touched)
        if (auto cg_thirdPerson = Functions::FindDvar("cg_thirdPerson");
            cg_thirdPerson && cg_thirdPerson->type == Structures::DVAR_TYPE_BOOL)
            cg_thirdPerson->value.boolean = isModControlled || isThirdPerson;

        if (auto cg_drawGun = Functions::FindDvar("cg_drawGun"); cg_drawGun && cg_drawGun->type == Structures::DVAR_TYPE_BOOL)
            cg_drawGun->value.boolean = !isModControlled;
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_CalcViewValues, reinterpret_cast<uintptr_t>(CG_CalcViewValues_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_CalcViewValues_Trampoline));
    }
}  // namespace IWXMVM::IW2::Hooks::Camera
