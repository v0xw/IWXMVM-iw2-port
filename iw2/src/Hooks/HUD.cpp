#include "StdInclude.hpp"
#include "HUD.hpp"

#include "../Patches.hpp"

namespace IWXMVM::IW2::Hooks::HUD
{
    bool showIconsAndText = true;

    void Apply()
    {
        auto& patches = Patches::GetGamePatches();

        if (showIconsAndText)
            patches.CG_Draw2dHudElems.Revert();
        else
            patches.CG_Draw2dHudElems.Apply();
    }

    void Install()
    {
        // Nothing to hook: all HUD toggles are driven by dvars and the deferred patches in Patches.hpp.
    }
}  // namespace IWXMVM::IW2::Hooks::HUD
