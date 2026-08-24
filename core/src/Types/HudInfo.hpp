#pragma once
#include "glm/vec3.hpp"

namespace IWXMVM::Types
{
    struct HudInfo
    {
        bool show2DElements;
        bool showPlayerHUD;
        bool showShellshock;
        bool showCrosshair;
        bool showScore;
        bool showIconsAndText;
        bool showBloodOverlay;

        bool showKillfeed;
        glm::vec3 killfeedTeam1Color;
        glm::vec3 killfeedTeam2Color;

        // appended after the fields above so game modules using positional aggregate
        // initialization keep compiling; games that do not support these can ignore them
        bool showHitmarkers = true;
        bool showKilledByMessages = true;  // the "You killed X" / "Killed by X" screen texts
    };
}  // namespace IWXMVM::Types