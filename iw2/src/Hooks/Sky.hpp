#pragma once

namespace IWXMVM::IW2::Hooks::Sky
{
    // Sky override: swap the world's sky cubemap for another stock map's on the fly.
    // An empty name restores the current map's own sky.
    void SetOverride(const std::string& materialName);

    // Called once per rendered frame (from the CG_Draw2D hook); keeps the override applied
    // across world reloads and restores the original sky when the override is cleared.
    void Apply();
}  // namespace IWXMVM::IW2::Hooks::Sky
