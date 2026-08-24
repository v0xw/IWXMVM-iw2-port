#pragma once

namespace IWXMVM::IW2::Hooks::HUD
{
    extern bool showHitmarkers;
    extern bool showScore;
    extern bool showShellshock;
    extern bool showKilledByMessages;
    extern bool showModText;
    extern bool showTimer;
    extern bool showPlayersLeftAlive;
    extern bool showHints;
    extern bool showTeammateIcons;
    extern bool showChat;
    extern bool showBombTimer;
    extern bool showPlayerHUD;
    extern bool showCrosshair;
    extern bool show2DElements;
    extern bool showKillfeed;
    extern bool showKillfeedKills;
    extern bool showKillfeedBombEvents;
    extern bool showKillfeedOtherInfo;
    extern bool showKillfeedModMessages;

    void Install();

    // zeroes the snapshot shellshock state while showShellshock is off; called every frame
    void SuppressShellshock();

    // clears the latched cursor hint state while showHints is off; called from the CG_Draw2D hook
}  // namespace IWXMVM::IW2::Hooks::HUD
