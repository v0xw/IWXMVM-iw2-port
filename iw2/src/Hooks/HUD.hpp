#pragma once

namespace IWXMVM::IW2::Hooks::HUD
{
    extern bool showIconsAndText;
    extern bool showHitmarkers;
    extern bool showScore;
    extern bool showShellshock;
    extern bool showKilledByMessages;
    extern bool showModText;
    extern bool showTimer;
    extern bool showPlayersLeftAlive;
    extern bool showHints;

    void Install();

    // zeroes the snapshot shellshock state while showShellshock is off; called every frame
    void SuppressShellshock();

    // zeroes the snapshot cursor hint state while showHints is off; called every frame
    void SuppressCursorHints();
}  // namespace IWXMVM::IW2::Hooks::HUD
