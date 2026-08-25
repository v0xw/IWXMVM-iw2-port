#pragma once
#include "Types/Dof.hpp"
#include "Types/Filmtweaks.hpp"

// Lightweight accessors for the post process settings (see GraphicsManager::ApplyDof and
// GraphicsManager::ApplyFilmtweaks), so game modules don't have to pull in the full Graphics.hpp
namespace IWXMVM::GFX
{
    Types::DoF GetDofSettings();
    void SetDofSettings(const Types::DoF& settings);

    Types::Filmtweaks GetFilmtweaksSettings();
    void SetFilmtweaksSettings(const Types::Filmtweaks& settings);

    // runs the DOF post process on the current backbuffer contents; called by the game module
    // between the 3D scene and the 2D pass so HUD elements stay sharp
    void ApplyDofPostProcess();
}  // namespace IWXMVM::GFX
