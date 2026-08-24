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

        // Latches the snapshot cursor hint (weapon pickup / use / plant prompts) into the cg globals the hint
        // drawer reads. int __cdecl(); patched out while the hints toggle is off.
        ReturnPatch CG_UpdateCursorHint{Addresses::CG_UpdateCursorHint, PatchApplySetting::Deferred};

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
