#pragma once
#include "Types/Dof.hpp"

// Lightweight accessors for the DOF post process settings (see GraphicsManager::ApplyDof),
// so game modules don't have to pull in the full Graphics.hpp
namespace IWXMVM::GFX
{
    Types::DoF GetDofSettings();
    void SetDofSettings(const Types::DoF& settings);
}  // namespace IWXMVM::GFX
