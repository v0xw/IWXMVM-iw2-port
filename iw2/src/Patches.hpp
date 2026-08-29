#pragma once
#include "StdInclude.hpp"
#include "Utilities/Patches.hpp"
#include "Addresses.hpp"

namespace IWXMVM::IW2::Patches
{
    using namespace IWXMVM::Patches;

    struct IW2Patches
    {
        // Full-screen red damage blend ("blood overlay") drawn when the POV player takes damage.
        // int __cdecl(); toggled by the Visuals tab blood overlay switch.
        ReturnPatch CG_DrawDamageBlend{Addresses::CG_DrawDamageBlend, PatchApplySetting::Deferred};

        // The pulsing low-health blood overlay, drawn through the hud.menu ownerdraw path - the only ownerdraw
        // handler that skips the hud_enable check, so it needs its own patch. Toggled with the blood overlay
        // switch alongside CG_DrawDamageBlend. Caller-cleaned stack args, plain ret.
        ReturnPatch CG_DrawLowHealthOverlay{Addresses::CG_DrawLowHealthOverlay, PatchApplySetting::Deferred};

        // The spectator UI drawn when the POV player dies or spectates: the "SPECTATOR" label, the
        // "Following" + player name texts and the follow key hints. All int __cdecl(); patched out
        // in mod-controlled camera modes.
        ReturnPatch CG_DrawSpectatorLabel{Addresses::CG_DrawSpectatorLabel, PatchApplySetting::Deferred};
        ReturnPatch CG_DrawFollowHints{Addresses::CG_DrawFollowHints, PatchApplySetting::Deferred};
        ReturnPatch CG_DrawFollowText{Addresses::CG_DrawFollowText, PatchApplySetting::Deferred};

        // Latches the snapshot cursor hint (weapon pickup / use / plant prompts) into the cg globals the hint
        // drawer reads. int __cdecl(); patched out while the hints toggle is off.
        ReturnPatch CG_UpdateCursorHint{Addresses::CG_UpdateCursorHint, PatchApplySetting::Deferred};

        // Draws the above-head sprites (team head icons) for every player entity in the snapshot.
        // int __cdecl(); toggled by the teammate icons switch.
        ReturnPatch CG_DrawPlayerSprites{Addresses::CG_DrawPlayerSprites, PatchApplySetting::Deferred};

        // Draws the chat message lines. void __cdecl(); toggled by the chat switch.
        ReturnPatch CG_DrawChatMessages{Addresses::CG_DrawChatMessages, PatchApplySetting::Deferred};

        // Adds the obituary line to the killfeed (caller cleans the stack, so a plain ret skips it safely).
        // Toggled by the killfeed kills switch.
        ReturnPatch CG_AddObituaryMessage{Addresses::CG_AddObituaryMessage, PatchApplySetting::Deferred};

        // CL_KeyEvent converts every key press during demo playback into ESCAPE (pops up the main menu).
        // NOP the "demo playing" branch so keys behave like in a normal game.
        NopPatch<2> CL_KeyEvent_DemoKeyToEscape{Addresses::CL_KeyEvent_DemoPlayingJump, PatchApplySetting::Immediately};
    };

    inline IW2Patches& GetGamePatches()
    {
        static IW2Patches patches;
        return patches;
    }
}  // namespace IWXMVM::IW2::Patches
