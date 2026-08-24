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
        bool showModText = false;  // mod-specific info texts (zPAM warnings, weapon info)
        bool showTimer = true;     // the round timer
        bool showPlayersLeftAlive = true;  // zPAM's players-left counters at the bottom
        bool showHints = true;             // cursor hints: weapon pickup, use / plant prompts, mantle
        bool showTeammateIcons = true;     // the team icon above teammates' heads
        bool showChat = true;              // player chat messages
        bool showBombTimer = true;         // the bomb stopwatch in the top left

        // killfeed sub-categories (apply while showKillfeed is on)
        bool showKillfeedKills = true;        // the obituary lines
        bool showKillfeedBombEvents = true;   // explosives planted / defused
        bool showKillfeedOtherInfo = true;    // connects, disconnects, team joins, round outcomes
        bool showKillfeedModMessages = true;  // everything the mod prints (zPAM damage report etc.)
    };
}  // namespace IWXMVM::Types