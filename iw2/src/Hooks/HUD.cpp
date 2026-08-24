#include "StdInclude.hpp"
#include "HUD.hpp"

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Functions.hpp"
#include "../Patches.hpp"
#include "../Structures.hpp"

namespace IWXMVM::IW2::Hooks::HUD
{
    // defaults: the clean moviemaking setup - only the 2D master switch, hitmarkers, killed-by text,
    // killfeed and blood overlay start enabled
    bool showHitmarkers = true;
    bool showScore = false;
    bool showShellshock = false;
    bool showKilledByMessages = true;
    bool showModText = false;
    bool showTimer = false;
    bool showPlayersLeftAlive = false;
    bool showHints = false;
    bool showTeammateIcons = false;
    bool showChat = false;
    bool showBombTimer = false;
    bool showPlayerHUD = false;
    bool showCrosshair = false;
    bool show2DElements = true;
    bool showKillfeed = true;

    // ---------------------------------------------------------------------------------------------------------
    // Scripted hudelem filtering.
    //
    // All scripted hud elements (server / GSC created: hint icons, timers, scores, zPAM's hitmarkers, ...) live
    // in two arrays inside the current snapshot and are drawn by CG_Draw2dHudElems, which draws the elements
    // whose "foreground" field matches the pass it renders. To hide a subset we temporarily give the unwanted
    // elements a foreground value no pass ever asks for while CG_Draw2D runs, and restore them afterwards.
    // ---------------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr int MASKED_FOREGROUND = 0x7FFFFFFF;

        struct MaskedElem
        {
            int* foreground;
            int original;
        };
        // two arrays of 31 elements each
        MaskedElem maskedElems[2 * 31];
        size_t maskedCount = 0;

        bool IsMaterialElem(int type)
        {
            // 6 = setShader icons; 0xB/0xC = the other material draw path
            return type == 6 || type == 0xB || type == 0xC;
        }

        const char* GetElemMaterialName(uint8_t* elem)
        {
            const auto materialIdx = *reinterpret_cast<int*>(elem + Addresses::hudElem_materialIdx);
            if (materialIdx <= 0 || materialIdx >= 128)
                return "";

            const auto offset = reinterpret_cast<int*>(Addresses::materialCSOffsets)[materialIdx];
            return reinterpret_cast<const char*>(Addresses::materialCSData) + offset;
        }

        bool IsHitmarkerElem(uint8_t* elem)
        {
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            return IsMaterialElem(type) && std::strcmp(GetElemMaterialName(elem), "damage_feedback") == 0;
        }

        bool IsTimerType(int type)
        {
            return type >= 3 && type <= 5 || type == 7;
        }

        bool IsBombTimerElem(uint8_t* elem)
        {
            // setClock elements draw the stopwatch dial material, so the bomb timer is a material-type
            // element showing hudStopwatch (identical in vanilla and zPAM)
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            return IsMaterialElem(type) && std::strcmp(GetElemMaterialName(elem), "hudStopwatch") == 0;
        }

        bool IsTimerElem(uint8_t* elem)
        {
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            return IsTimerType(type);
        }

        bool IsModTextElem(uint8_t* elem)
        {
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            const auto x = *reinterpret_cast<float*>(elem + Addresses::hudElem_x);
            const auto y = *reinterpret_cast<float*>(elem + Addresses::hudElem_y);

            // zPAM's header lines at the very top left ("Search and destroy MR12", league rules, version)
            if (type == 1 && y <= 30.0f)
                return true;

            // texts near the bottom center (zPAM's text bomb countdown, spectator prompts); the players-left
            // display sits in the same strip but far to the right (x <= -100, right-aligned)
            return (type == 1 || type == 2) && y >= 400.0f && x > -100.0f;
        }

        bool IsPlayersLeftElem(uint8_t* elem)
        {
            // zPAM's players-left display at the bottom right: label texts and count values at y 479,
            // right-aligned with x -280..-185
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            const auto x = *reinterpret_cast<float*>(elem + Addresses::hudElem_x);
            const auto y = *reinterpret_cast<float*>(elem + Addresses::hudElem_y);
            return (type == 1 || type == 2) && y >= 400.0f && x <= -100.0f;
        }

        bool IsScoreElem(uint8_t* elem)
        {
            // the team win score in the top left: value elements plus the hudicon_ team flags beside them.
            // Numbers in the lower half (like zPAM's players-left counters) belong to their neighbouring label
            // texts and stay under icons and text.
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            const auto y = *reinterpret_cast<float*>(elem + Addresses::hudElem_y);
            if (y >= 240.0f)
                return false;
            if (type == 2)
                return true;
            return IsMaterialElem(type) && std::strncmp(GetElemMaterialName(elem), "hudicon_", 8) == 0;
        }

        void MaskArray(uint8_t* elems)
        {
            for (uint32_t i = 0; i < Addresses::hudElem_count; i++)
            {
                const auto elem = elems + i * Addresses::hudElem_size;
                if (*reinterpret_cast<int*>(elem + Addresses::hudElem_type) == 0)
                    break;  // the game stops collecting at the first empty element too

                bool visible;
                if (IsHitmarkerElem(elem))
                    visible = showHitmarkers;
                else if (IsBombTimerElem(elem))
                    visible = showBombTimer;
                else if (IsTimerElem(elem))
                    visible = showTimer;
                else if (IsModTextElem(elem))
                    visible = showModText;
                else if (IsPlayersLeftElem(elem))
                    visible = showPlayersLeftAlive;
                else if (IsScoreElem(elem))
                    visible = showScore;
                else
                    visible = true;  // unclassified elements (e.g. zPAM's plant progress bar) always draw

                if (!visible)
                {
                    const auto foreground = reinterpret_cast<int*>(elem + Addresses::hudElem_foreground);
                    maskedElems[maskedCount++] = {foreground, *foreground};
                    *foreground = MASKED_FOREGROUND;
                }
            }
        }

        void MaskHiddenHudElems()
        {
            maskedCount = 0;
            if (showHitmarkers && showScore && showTimer && showBombTimer && showPlayersLeftAlive && showModText)
                return;

            const auto snap = *Structures::At<uint8_t*>(Addresses::cg_nextSnap);
            if (snap == nullptr)
                return;

            MaskArray(snap + Addresses::snap_hudElemsCurrent);
            MaskArray(snap + Addresses::snap_hudElemsArchival);
        }

        void RestoreHudElems()
        {
            for (size_t i = 0; i < maskedCount; i++)
                *maskedElems[i].foreground = maskedElems[i].original;
            maskedCount = 0;
        }
    }  // namespace

    // ---------------------------------------------------------------------------------------------------------
    // CG_Draw2D returns immediately when cg_draw2D is 0, which also drops the sniper scope overlay - leaving a
    // plain zoomed-in view. The scope is part of what the player sees through the weapon, not HUD clutter, so
    // it must survive "show 2D elements off": when the original bails on cg_draw2D, draw just the reticle.
    // CG_DrawWeapReticle draws nothing unless the player is actually zoomed into a scoped weapon.
    // ---------------------------------------------------------------------------------------------------------

    typedef void(__cdecl* CG_Draw2D_t)();
    CG_Draw2D_t CG_Draw2D_Trampoline = nullptr;

    void SuppressCursorHints();

    void __cdecl CG_Draw2D_Hook()
    {
        SuppressCursorHints();
        MaskHiddenHudElems();
        CG_Draw2D_Trampoline();
        RestoreHudElems();

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

    // ---------------------------------------------------------------------------------------------------------
    // Shellshock: the effect state (blur, sound filtering, view kick) is driven from the snapshot playerstate.
    // CG_UpdateShellShock deactivates everything cleanly when the start time reads 0, so while the toggle is
    // off we zero the fields in the snapshots the cgame reads. Called every frame from the Com_ModifyMsec hook.
    // ---------------------------------------------------------------------------------------------------------

    void SuppressShellshock()
    {
        if (showShellshock)
            return;

        for (const auto address : {Addresses::cg_snap, Addresses::cg_nextSnap})
        {
            const auto snap = *Structures::At<uint8_t*>(address);
            if (snap == nullptr)
                continue;

            *reinterpret_cast<int*>(snap + Addresses::snap_shellshockIndex) = 0;
            *reinterpret_cast<int*>(snap + Addresses::snap_shellshockTime) = 0;
            *reinterpret_cast<int*>(snap + Addresses::snap_shellshockDuration) = 0;
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Cursor hints (weapon pickup, use / bomb plant prompts, mantle) are latched from the snapshot playerstate
    // into cg globals during the frame, where they linger and fade - the drawer reads the latch. It is cleared
    // right before CG_Draw2D runs, after the frame's latching already happened, so nothing can re-arm it. The
    // mantle hint additionally has its own dvar. zPAM's plant / defuse progress bar is unrelated (scripted
    // hudelems) and stays visible.
    // ---------------------------------------------------------------------------------------------------------

    void SuppressCursorHints()
    {
        if (!Structures::IsDemoPlaying())
            return;

        if (const auto mantleHint = Functions::FindDvar("cg_drawMantleHint");
            mantleHint && mantleHint->type == Structures::DVAR_TYPE_BOOL)
            mantleHint->value.boolean = showHints;

        if (showHints)
        {
            Patches::GetGamePatches().CG_UpdateCursorHint.Revert();
            return;
        }

        // stop the latch from updating and clear whatever is still fading out
        Patches::GetGamePatches().CG_UpdateCursorHint.Apply();
        *Structures::At<int>(Addresses::cg_cursorHintLatched) = 0;
        *Structures::At<int>(Addresses::cg_cursorHintTime) = 0;
        *Structures::At<int>(Addresses::cg_cursorHintString) = 0;
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_Draw2D, reinterpret_cast<uintptr_t>(CG_Draw2D_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_Draw2D_Trampoline));
    }
}  // namespace IWXMVM::IW2::Hooks::HUD
