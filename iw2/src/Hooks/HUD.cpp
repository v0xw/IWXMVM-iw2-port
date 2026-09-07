#include "StdInclude.hpp"
#include "HUD.hpp"

#include "Components/CameraManager.hpp"
#include "Components/Rendering.hpp"
#include "Mod.hpp"
#include "Graphics/PostProcessSettings.hpp"
#include "Types/RenderingFlags.hpp"
#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Functions.hpp"
#include "../Patches.hpp"
#include "Fog.hpp"
#include "Sky.hpp"
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
    bool showKillfeedKills = true;
    bool showKillfeedBombEvents = true;
    bool showKillfeedOtherInfo = false;
    bool showKillfeedModMessages = false;
    bool showBloodOverlay = true;
    // the game's own g_TeamColor_* defaults
    glm::vec3 killfeedTeam1Color = {0.5f, 0.5f, 1.0f};  // allies
    glm::vec3 killfeedTeam2Color = {1.0f, 0.5f, 0.5f};  // axis
    // the game's own con_gamemessagetime default
    float killfeedMessageTime = 5.0f;

    bool PlayerFeedbackVisible()
    {
        if (!Structures::IsDemoPlaying())
            return true;

        const auto& camera = Components::CameraManager::Get().GetActiveCamera();
        if (!camera)
            return true;

        const auto mode = camera->GetMode();
        return mode == Components::Camera::Mode::FirstPerson || mode == Components::Camera::Mode::ThirdPerson;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Scripted hudelem filtering.
    //
    // All scripted hud elements (server / GSC created: hint icons, timers, scores, hitmarkers, ...) live
    // in two arrays inside the current snapshot and are drawn by CG_Draw2dHudElems, which draws the elements
    // whose "foreground" field matches the pass it renders. To hide a subset we temporarily give the unwanted
    // elements a foreground value no pass ever asks for while CG_Draw2D runs, and restore them afterwards.
    //
    // Classifiers that describe zPAM's screen layout (mod text, players left) only apply while a mod demo is
    // loaded: their toggles are greyed out on vanilla demos, and a greyed-out toggle must not silently hide
    // vanilla elements that happen to sit in the same screen regions.
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

        // evaluated once per frame in MaskHiddenHudElems
        bool feedbackVisibleThisFrame = true;
        bool hitmarkersVisibleThisFrame = true;
        bool modTextVisibleThisFrame = true;
        bool playersLeftVisibleThisFrame = true;

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
            // both the stock _damagefeedback.gsc and zPAM's variant of it show hits through
            // client hudelems with this shader, so this matches vanilla and mod demos alike
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            return IsMaterialElem(type) && _stricmp(GetElemMaterialName(elem), "damage_feedback") == 0;
        }

        bool IsProgressBarElem(uint8_t* elem)
        {
            // zPAM's progress bars (SnD plant/defuse, HQ radio capture, RE objective) are raw "white" bar
            // elements on "black" backdrops; only these two materials are ever drawn as bare rectangles
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            if (!IsMaterialElem(type))
                return false;

            const auto name = GetElemMaterialName(elem);
            return _stricmp(name, "white") == 0 || _stricmp(name, "black") == 0;
        }

        bool IsTimerType(int type)
        {
            return type >= 3 && type <= 5 || type == 7;
        }

        bool IsBombTimerElem(uint8_t* elem)
        {
            // setClock elements draw the stopwatch dial material, so the bomb timer is a material-type
            // element showing hudStopwatch in the top left (identical in vanilla and zPAM; material names are
            // stored lowercased, hence the case-insensitive compare). zPAM's round-end countdown uses the same
            // material but sits mid-screen right and is classified as mod text instead.
            const auto type = *reinterpret_cast<int*>(elem + Addresses::hudElem_type);
            const auto y = *reinterpret_cast<float*>(elem + Addresses::hudElem_y);
            return IsMaterialElem(type) && y < 240.0f && _stricmp(GetElemMaterialName(elem), "hudStopwatch") == 0;
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
            if ((type == 1 || type == 2) && y >= 400.0f && x > -100.0f)
                return true;

            // zPAM's round-end "Round N Starting" cluster on the middle right (yellow texts, value and its
            // countdown stopwatch at x -90.., y 270..325)
            if ((type == 1 || type == 2) && y >= 240.0f && y < 400.0f && x <= -50.0f)
                return true;
            return IsMaterialElem(type) && y >= 240.0f && _stricmp(GetElemMaterialName(elem), "hudStopwatch") == 0;
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
            return IsMaterialElem(type) && _strnicmp(GetElemMaterialName(elem), "hudicon_", 8) == 0;
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
                    visible = hitmarkersVisibleThisFrame;
                else if (IsProgressBarElem(elem))
                    visible = feedbackVisibleThisFrame;  // POV interaction feedback, like the cursor hints
                else if (IsBombTimerElem(elem))
                    visible = showBombTimer;
                else if (IsTimerElem(elem))
                    visible = showTimer;
                else if (IsModTextElem(elem))
                    visible = modTextVisibleThisFrame;
                else if (IsPlayersLeftElem(elem))
                    visible = playersLeftVisibleThisFrame;
                else if (IsScoreElem(elem))
                    visible = showScore;
                else
                    visible = true;  // unclassified elements always draw

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
            feedbackVisibleThisFrame = PlayerFeedbackVisible();
            hitmarkersVisibleThisFrame = showHitmarkers && feedbackVisibleThisFrame;
            const bool isModDemo = !Mod::GetGameInterface()->GetDemoModName().empty();
            modTextVisibleThisFrame = showModText || !isModDemo;
            playersLeftVisibleThisFrame = showPlayersLeftAlive || !isModDemo;
            if (feedbackVisibleThisFrame && hitmarkersVisibleThisFrame && showScore && showTimer && showBombTimer &&
                playersLeftVisibleThisFrame && modTextVisibleThisFrame)
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

    // ---------------------------------------------------------------------------------------------------------
    // Player feedback that is controlled through dvars or patches (damage blend, low-health overlay,
    // directional damage icons, grenade indicator, spectator UI, player HUD, crosshair) is re-evaluated every
    // frame so it follows both the user's toggles and the active camera mode; camera switches don't run
    // through SetHudInfo.
    // ---------------------------------------------------------------------------------------------------------

    namespace
    {
        void SetBoolDvar(const char* name, bool value)
        {
            if (auto dvar = Functions::FindDvar(name); dvar && dvar->type == Structures::DVAR_TYPE_BOOL)
                dvar->value.boolean = value;
        }

        // Text the game timestamps against its clock goes stale on a timeline jump: after a rewind the
        // stamps lie in the future of the rewound clock, after a skip forward the catch-up replays old
        // events onto the landing frame. A cg.time step no real playback frame produces - backwards,
        // or a fast-forward catch-up chunk - marks the jump.
        bool TimelineJumpedThisFrame()
        {
            static int lastTime;
            const auto time = *Structures::At<int>(Addresses::cg_time);
            const auto delta = time - lastTime;
            lastTime = time;

            return Structures::IsDemoPlaying() && (delta < 0 || delta > 500);
        }

        // CG_DrawCenterString's elapsed-time gate never expires a centerprint stamped in the future, so
        // the "You killed X" text would linger until the next kill replaces it.
        void ClearStaleCenterPrint()
        {
            *Structures::At<int>(Addresses::cg_centerPrintTime) = 0;
        }

        // The killfeed (and the other console message windows) keeps its lines' start / expiry stamps.
        // An expired line only gets its start zeroed; the expiry stays. Con_UpdateMessageWindowLine, when
        // it adds a line, re-stamps every older line whose expiry still lies beyond now + fadeOut so it
        // starts fading right away - without checking that the line is in use. After a rewind the stale
        // expiries of long-cleared lines are exactly that, so the next kill resurrects them for one
        // fade-out (a 500 ms flash of old kills / mod prints under the new line). Wiping both stamps of
        // every line makes the windows forget everything from before the jump, including the lines the
        // catch-up just replayed onto the landing frame.
        void ClearMessageWindows()
        {
            for (uint32_t w = 0; w < Addresses::con_messageWindow_count; ++w)
            {
                const auto window = Addresses::con_messageWindows + w * Addresses::con_messageWindow_size;
                const auto lines = *Structures::At<uint8_t*>(window);
                const auto lineCount = *Structures::At<int>(window + 8);
                if (lines == nullptr || lineCount <= 0 || lineCount > 64)
                    continue;

                for (int i = 0; i < lineCount; ++i)
                {
                    const auto line = lines + i * Addresses::con_messageLine_size;
                    *reinterpret_cast<int*>(line + Addresses::con_messageLine_startTime) = 0;
                    *reinterpret_cast<int*>(line + Addresses::con_messageLine_expireTime) = 0;
                }
            }
        }

        void ClearStaleTextOnTimelineJump()
        {
            if (!TimelineJumpedThisFrame())
                return;

            ClearStaleCenterPrint();
            ClearMessageWindows();
        }

        // zPAM renders several info texts through client dvars picked up by its menus: the still-alive enemy
        // names (bottom right), the sniper / shotgun wielders (bottom left) and the server warnings ("Server
        // password is not set!", "This is an old version of map, use mp_xxx_fix!", changed cvars). The demo
        // keeps re-setting them through recorded server commands, so while the zPAM-text toggle is off they
        // are blanked every frame - and it has to happen here, after CG_DrawActiveFrame executed the frame's
        // server commands and before the menus paint, or a fresh value shows for exactly one frame.
        void SuppressZpamText()
        {
            if (showModText || !Structures::IsDemoPlaying())
                return;

            for (const auto name : {"ui_playersleft_list", "ui_sniper_info", "ui_shotgun_info", "ui_serverinfo_hud"})
            {
                const auto dvar = Functions::FindDvar(name);
                if (dvar && dvar->type == Structures::DVAR_TYPE_STRING && dvar->value.string && dvar->value.string[0])
                    Functions::Dvar_SetString(dvar, "");
            }
        }

        void ApplyPlayerFeedbackSuppression()
        {
            if (!Structures::IsDemoPlaying())
                return;

            const bool feedback = PlayerFeedbackVisible();

            // the blood on screen comes from two drawers: the on-hit red blend, and the pulsing low-health
            // overlay - a hud.menu ownerdraw that ignores hud_enable, so it needs its own patch
            if (showBloodOverlay && feedback)
            {
                Patches::GetGamePatches().CG_DrawDamageBlend.Revert();
                Patches::GetGamePatches().CG_DrawLowHealthOverlay.Revert();
            }
            else
            {
                Patches::GetGamePatches().CG_DrawDamageBlend.Apply();
                Patches::GetGamePatches().CG_DrawLowHealthOverlay.Apply();
            }

            // the spectator UI ("SPECTATOR" label, "Following" + player name, the follow key hints) belongs
            // to the POV player's view like the scope reticle does: keep it in first/third person, hide it
            // in mod-controlled cameras
            if (feedback)
            {
                Patches::GetGamePatches().CG_DrawSpectatorLabel.Revert();
                Patches::GetGamePatches().CG_DrawFollowHints.Revert();
                Patches::GetGamePatches().CG_DrawFollowText.Revert();
            }
            else
            {
                Patches::GetGamePatches().CG_DrawSpectatorLabel.Apply();
                Patches::GetGamePatches().CG_DrawFollowHints.Apply();
                Patches::GetGamePatches().CG_DrawFollowText.Apply();
            }

            if (auto damageIconTime = Functions::FindDvar("cg_hudDamageIconTime");
                damageIconTime && damageIconTime->type == Structures::DVAR_TYPE_INT)
                damageIconTime->value.integer = (showPlayerHUD && feedback) ? 2000 : 0;

            // range 0 keeps the grenade icon and danger pointer permanently out of range
            static float grenadeIconRange = -1.0f;
            if (auto grenadeRange = Functions::FindDvar("cg_hudGrenadeIconMaxRange");
                grenadeRange && grenadeRange->type == Structures::DVAR_TYPE_FLOAT)
            {
                if (grenadeRange->value.decimal != 0.0f)
                    grenadeIconRange = grenadeRange->value.decimal;
                if (feedback && grenadeIconRange > 0.0f)
                    grenadeRange->value.decimal = grenadeIconRange;
                else
                    grenadeRange->value.decimal = 0.0f;
            }

            SetBoolDvar("hud_enable", showPlayerHUD && feedback);
            SetBoolDvar("cg_drawCrosshairNames", showPlayerHUD && feedback);
            SetBoolDvar("cg_drawCrosshair", showCrosshair && feedback);
        }
    }  // namespace

    // ---------------------------------------------------------------------------------------------------------
    // DOF between the 3D scene and the 2D pass. The cgame is a command-queue frontend: CG_Draw2D only enqueues
    // render commands, which the backend executes at frame end - so the DOF pass cannot simply run from here.
    // Instead a SetViewport command with marker values is enqueued at this point (after the scene view command,
    // before the 2D commands), and the backend's SetViewport handler - swapped in the render command dispatch
    // table for a wrapper - recognizes the marker when the command stream reaches it, runs the DOF post process
    // on the freshly rendered scene, and consumes the command. Real viewport commands pass through untouched.
    // The table entry is re-checked every frame since vid_restart reloads the renderer DLL.
    // ---------------------------------------------------------------------------------------------------------

    namespace
    {
        constexpr int DOF_MARKER_VIEWPORT[4] = {1, 2, 3, 1};  // never produced by the game
        constexpr uint32_t RB_CMD_SET_VIEWPORT = 13;

        typedef void(__cdecl* RB_Command_t)(uint8_t** cmd);
        RB_Command_t RB_SetViewportCmd_Original = nullptr;

        void __cdecl RB_SetViewportCmd_Wrapper(uint8_t** cmd)
        {
            const auto values = reinterpret_cast<const int*>(*cmd + 4);
            if (values[0] == DOF_MARKER_VIEWPORT[0] && values[1] == DOF_MARKER_VIEWPORT[1] &&
                values[2] == DOF_MARKER_VIEWPORT[2] && values[3] == DOF_MARKER_VIEWPORT[3])
            {
                GFX::ApplyDofPostProcess();
                *cmd += *reinterpret_cast<const uint16_t*>(*cmd + 2);  // consume the marker command
                return;
            }

            RB_SetViewportCmd_Original(cmd);
        }

        bool EnsureViewportCmdWrapped()
        {
            const auto tableAddress = Addresses::Gfx(Addresses::GfxRVA::RB_RenderCommandTable);
            if (tableAddress == 0)
                return false;

            const auto entry = reinterpret_cast<void**>(tableAddress) + RB_CMD_SET_VIEWPORT;
            if (*entry == reinterpret_cast<void*>(RB_SetViewportCmd_Wrapper))
                return true;

            // first call, or the renderer DLL was reloaded by a vid_restart
            const auto original = reinterpret_cast<uintptr_t>(*entry);
            const auto moduleBase = reinterpret_cast<uintptr_t>(Addresses::GetGfxModule());
            if (original - moduleBase > 0x400000)  // sanity: the handler must live inside the module
                return false;

            RB_SetViewportCmd_Original = reinterpret_cast<RB_Command_t>(original);

            // the dispatch table lives in the renderer DLL's read-only .rdata
            DWORD oldProtection = 0;
            if (!::VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &oldProtection))
                return false;
            *entry = reinterpret_cast<void*>(RB_SetViewportCmd_Wrapper);
            ::VirtualProtect(entry, sizeof(void*), oldProtection, &oldProtection);
            return true;
        }
    }  // namespace

    // ---------------------------------------------------------------------------------------------------------
    // Multipass greenscreen passes. Core drives Types::RenderingFlags (OnlyWorld / OnlyPlayers /
    // WorldAndPlayers); on CoD2 they are applied through the renderer's debug dvars, which the scene building
    // consults. The normal render path never clears the color buffer (the sky covers it), so when the world is
    // hidden a green clear is enqueued ahead of the scene commands. Runs before R_RenderScene each frame.
    // ---------------------------------------------------------------------------------------------------------

    void ApplyRenderingFlags()
    {
        static bool needRestore = false;

        const auto flags =
            Structures::IsDemoPlaying() ? Components::Rendering::GetRenderingFlags() : Types::RenderingFlags_DrawEverything;
        const bool drawWorld = (flags & Types::RenderingFlags_DrawWorld) != 0;
        const bool drawPlayers = (flags & Types::RenderingFlags_DrawPlayers) != 0;

        if (drawWorld && drawPlayers && !needRestore)
            return;
        needRestore = !(drawWorld && drawPlayers);

        for (const auto name : {"r_drawWorld", "r_drawSModels", "r_drawBModels", "r_drawDecals", "r_drawWater",
                                "r_drawSun"})
            SetBoolDvar(name, drawWorld);
        SetBoolDvar("r_drawEntities", drawPlayers);

        if (!drawWorld)
        {
            typedef char(__cdecl* R_AddCmdClearScreen_t)(int clearFlags, const float* rgba, float depth,
                                                         char stencil);
            if (const auto addClear = Addresses::Gfx(Addresses::GfxRVA::R_AddCmdClearScreen))
            {
                constexpr float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
                reinterpret_cast<R_AddCmdClearScreen_t>(addClear)(1, green, 1.0f, 0);
            }
        }
    }

    void __cdecl CG_Draw2D_Hook()
    {
        // the backend resolves 'sampler.sky' from the world struct only when it executes the queued
        // scene, so applying the sky override here still affects the current frame
        Sky::Apply();
        // same for the fog state the backend reads
        Fog::Apply();

        // the scene view command is already queued at this point, the 2D commands are not - the
        // marker lands exactly between them in the backend's command stream
        if (Structures::IsDemoPlaying() && GFX::GetDofSettings().enabled && EnsureViewportCmdWrapped())
        {
            typedef void(__cdecl* R_AddCmdSetViewport_t)(int x, int y, int w, int h);
            if (const auto addCmd = Addresses::Gfx(Addresses::GfxRVA::R_AddCmdSetViewport))
            {
                reinterpret_cast<R_AddCmdSetViewport_t>(addCmd)(DOF_MARKER_VIEWPORT[0], DOF_MARKER_VIEWPORT[1],
                                                                DOF_MARKER_VIEWPORT[2], DOF_MARKER_VIEWPORT[3]);
            }
        }

        ClearStaleTextOnTimelineJump();
        SuppressZpamText();
        ApplyPlayerFeedbackSuppression();
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
        if (showShellshock && PlayerFeedbackVisible())
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
    // mantle hint additionally has its own dvar. zPAM's plant / defuse progress bar is unrelated: those are
    // scripted hudelems, classified as POV feedback by the hudelem masker above.
    // ---------------------------------------------------------------------------------------------------------

    void SuppressCursorHints()
    {
        if (!Structures::IsDemoPlaying())
            return;

        const bool hintsVisible = showHints && PlayerFeedbackVisible();

        if (const auto mantleHint = Functions::FindDvar("cg_drawMantleHint");
            mantleHint && mantleHint->type == Structures::DVAR_TYPE_BOOL)
            mantleHint->value.boolean = hintsVisible;

        if (hintsVisible)
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

    // ---------------------------------------------------------------------------------------------------------
    // Killfeed text classification. Plain text lines ("game message" server commands: bomb events, connects,
    // round outcomes, mod prints) all funnel through CG_AddGameMessage. The raw server command argument still
    // holds the untranslated string with the localized string key names, which classify language-independently;
    // the translated text is checked as a fallback for plain strings.
    // ---------------------------------------------------------------------------------------------------------

    static int __cdecl ShouldShowGameMessage(const char* text)
    {
        if (!Structures::IsDemoPlaying())
            return 1;

        const auto raw = *reinterpret_cast<const char* const*>(Addresses::cmd_argv + sizeof(char*));

        const auto matchesAny = [&](std::initializer_list<const char*> keys) {
            for (const auto key : keys)
            {
                if (raw && std::strstr(raw, key))
                    return true;
                if (text && std::strstr(text, key))
                    return true;
            }
            return false;
        };

        if (matchesAny({"MP_EXPLOSIVESPLANTED", "MP_EXPLOSIVESDEFUSED"}))
            return showKillfeedBombEvents ? 1 : 0;

        if (matchesAny({"MP_CONNECTED", "MP_DISCONNECTED", "MP_JOINED", "MP_SWITCHING", "MP_RENAMED",
                        "ELIMINATED", "ACCOMPLISHED", "MP_ROUNDDRAW", "MP_TIMEHASEXPIRED", "MP_TIME_LIMIT_REACHED",
                        "MP_FRIENDLY_FIRE"}))
            return showKillfeedOtherInfo ? 1 : 0;

        // unrecognized lines are mod prints on mod demos; on vanilla demos (where the "zPAM
        // Messages" toggle is greyed out) they are just more "other info" and must follow that
        // toggle instead of silently disappearing
        if (Mod::GetGameInterface()->GetDemoModName().empty())
            return showKillfeedOtherInfo ? 1 : 0;

        return showKillfeedModMessages ? 1 : 0;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Sniper scope overlay. The scope belongs to the POV player's first person view; in any other camera mode
    // (free, dolly, bone, orbit) it is just an overlay popping up whenever the POV player zooms. Suppressing it
    // here covers both draw paths: the game's own CG_Draw2D call and our forced call when cg_draw2D is off.
    // When idle, the original returns 1.0 (the crosshair fade factor), so the suppression does the same.
    // ---------------------------------------------------------------------------------------------------------

    typedef double(__cdecl* CG_DrawWeapReticle_t)();
    CG_DrawWeapReticle_t CG_DrawWeapReticle_Trampoline = nullptr;

    static double __cdecl CG_DrawWeapReticle_Hook()
    {
        if (Structures::IsDemoPlaying())
        {
            const auto& camera = Components::CameraManager::Get().GetActiveCamera();
            if (camera && camera->GetMode() != Components::Camera::Mode::FirstPerson)
            {
                return 1.0;
            }
        }

        return CG_DrawWeapReticle_Trampoline();
    }

    // ---------------------------------------------------------------------------------------------------------
    // "Connection Interrupted". A paused demo starves the client of snapshots, which constantly triggers the
    // interrupted-connection text and icon, so they are suppressed entirely during demo playback.
    // ---------------------------------------------------------------------------------------------------------

    typedef int(__cdecl* CG_DrawDisconnect_t)();
    CG_DrawDisconnect_t CG_DrawDisconnect_Trampoline = nullptr;

    static int __cdecl CG_DrawDisconnect_Hook()
    {
        if (Structures::IsDemoPlaying())
            return 0;

        return CG_DrawDisconnect_Trampoline();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Team colors (killfeed names, crosshair names). CG_GetTeamColor reads the g_TeamColor_* dvars, but those
    // are DVAR_CONFIG: replicated through configstrings, so during demo playback they are implicitly created
    // as string dvars and re-applied with the server's values on every gamestate parse (every rewind). Writing
    // the dvars is a losing battle - substitute our colors at the consumer instead. The original preserves the
    // output vec4's alpha, so the override only touches rgb.
    // ---------------------------------------------------------------------------------------------------------

    uintptr_t CG_GetTeamColor_Trampoline = 0;

    static int __cdecl OverrideTeamColor(float* color, int team)
    {
        if (!Structures::IsDemoPlaying())
            return 0;

        const glm::vec3* custom = nullptr;
        if (team == 2)
            custom = &killfeedTeam1Color;  // allies
        else if (team == 1)
            custom = &killfeedTeam2Color;  // axis
        if (custom == nullptr)
            return 0;  // spectators / unknown teams keep the original white

        color[0] = custom->x;
        color[1] = custom->y;
        color[2] = custom->z;
        return 1;
    }

    void __declspec(naked) CG_GetTeamColor_Hook()
    {
        static float* teamColorOut;
        static int teamColorHandled;

        __asm
        {
            mov teamColorOut, eax
            pushad
            mov eax, [esp + 32 + 4]  // stack arg past pushad and the return address
            push eax
            push teamColorOut
            call OverrideTeamColor
            add esp, 8
            mov teamColorHandled, eax
            popad
            cmp teamColorHandled, 0
            jne handled
            jmp CG_GetTeamColor_Trampoline
        handled:
            ret
        }
    }

    uintptr_t CG_AddGameMessage_Trampoline = 0;
    static const char* gameMessageText = nullptr;
    static int gameMessageShow = 1;

    void __declspec(naked) CG_AddGameMessage_Hook()
    {
        __asm
        {
            mov gameMessageText, ecx
            pushad
            push gameMessageText
            call ShouldShowGameMessage
            add esp, 4
            mov gameMessageShow, eax
            popad
            cmp gameMessageShow, 0
            je skip
            jmp CG_AddGameMessage_Trampoline
        skip:
            ret
        }
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_Draw2D, reinterpret_cast<uintptr_t>(CG_Draw2D_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_Draw2D_Trampoline));

        HookManager::CreateHook(Addresses::CG_AddGameMessage, reinterpret_cast<uintptr_t>(CG_AddGameMessage_Hook),
                                &CG_AddGameMessage_Trampoline);

        HookManager::CreateHook(Addresses::CG_DrawDisconnect, reinterpret_cast<uintptr_t>(CG_DrawDisconnect_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_DrawDisconnect_Trampoline));

        HookManager::CreateHook(Addresses::CG_DrawWeapReticle, reinterpret_cast<uintptr_t>(CG_DrawWeapReticle_Hook),
                                reinterpret_cast<uintptr_t*>(&CG_DrawWeapReticle_Trampoline));

        HookManager::CreateHook(Addresses::CG_GetTeamColor, reinterpret_cast<uintptr_t>(CG_GetTeamColor_Hook),
                                &CG_GetTeamColor_Trampoline);
    }
}  // namespace IWXMVM::IW2::Hooks::HUD
