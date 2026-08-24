#pragma once

namespace IWXMVM::IW2::Hooks::HUD
{
    extern bool showIconsAndText;
    extern bool showHitmarkers;
    extern bool showScore;
    extern bool showShellshock;
    extern bool showKilledByMessages;

    void Install();

    // zeroes the snapshot shellshock state while showShellshock is off; called every frame
    void SuppressShellshock();
}  // namespace IWXMVM::IW2::Hooks::HUD
