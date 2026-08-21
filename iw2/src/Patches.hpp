#pragma once
#include "StdInclude.hpp"
#include "Utilities/Patches.hpp"
#include "Addresses.hpp"

namespace IWXMVM::IW2::Patches
{
    using namespace IWXMVM::Patches;

    struct IW2Patches
    {
        // Hides scripted hud elements (objective icons, timers, ...). Toggled from the Visuals tab.
        ReturnPatch CG_Draw2dHudElems{Addresses::CG_Draw2dHudElems, PatchApplySetting::Deferred};

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
