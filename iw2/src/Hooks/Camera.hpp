#pragma once

namespace IWXMVM::IW2::Hooks::Camera
{
    // First person FOV override (degrees); <= 0 means "leave the game's value alone"
    extern float firstPersonFOV;

    void Install();
    void OnCameraChanged();
}  // namespace IWXMVM::IW2::Hooks::Camera
