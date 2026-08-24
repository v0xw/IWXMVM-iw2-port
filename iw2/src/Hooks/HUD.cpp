#include "StdInclude.hpp"
#include "HUD.hpp"

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Patches.hpp"
#include "../Structures.hpp"

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

    // ---------------------------------------------------------------------------------------------------------
    // CG_Draw2D returns immediately when cg_draw2D is 0, which also drops the sniper scope overlay - leaving a
    // plain zoomed-in view. The scope is part of what the player sees through the weapon, not HUD clutter, so
    // it must survive "show 2D elements off": when the original bails on cg_draw2D, draw just the reticle.
    // CG_DrawWeapReticle draws nothing unless the player is actually zoomed into a scoped weapon.
    // ---------------------------------------------------------------------------------------------------------

    typedef void(__cdecl* CG_Draw2D_t)();
    CG_Draw2D_t CG_Draw2D_Trampoline = nullptr;

    void __cdecl CG_Draw2D_Hook()
    {
        CG_Draw2D_Trampoline();

        if (*Structures::At<int>(Addresses::cg_cubemapShot) == 0)  // same gate the original checks first
        {
            const auto draw2D = *reinterpret_cast<Structures::dvar_t**>(Addresses::dvar_cg_draw2D);
            if (draw2D && draw2D->value.boolean == false)
            {
                // declared with its real return type so the compiler pops the st0 result
                typedef double(__cdecl * CG_DrawWeapReticle_t)();
                reinterpret_cast<CG_DrawWeapReticle_t>(Addresses::CG_DrawWeapReticle)();
            }
        }
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_Draw2D, reinterpret_cast<uintptr_t>(CG_Draw2D_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_Draw2D_Trampoline));
    }
}  // namespace IWXMVM::IW2::Hooks::HUD
