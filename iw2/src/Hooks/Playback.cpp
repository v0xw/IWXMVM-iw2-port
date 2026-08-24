#include "StdInclude.hpp"
#include "Playback.hpp"

#include "Components/Playback.hpp"
#include "Components/Rewinding.hpp"
#include "Utilities/HookManager.hpp"
#include "Events.hpp"
#include "Mod.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"
#include "../Functions.hpp"
#include "../DemoParser.hpp"
#include "HUD.hpp"
#include "Kills.hpp"

namespace IWXMVM::IW2::Hooks::Playback
{
    using namespace Structures;

    // ---------------------------------------------------------------------------------------------------------
    // Mouse capture
    //
    // Both the vanilla Mouse_Loop (0x464B30) and CoD2x's replacement early-out on the "mouse enabled" byte, so
    // clearing it is the one switch that stops cursor recentering + delta accumulation with and without CoD2x.
    // CoD2x / vid_restart may set it back, so the per-frame hook below re-asserts it.
    // ---------------------------------------------------------------------------------------------------------

    std::atomic<bool> mouseCaptured = false;
    uint8_t savedMouseEnabled = 1;
    bool haveSavedMouseEnabled = false;

    void ApplyMouseCapture()
    {
        auto& mouseEnabled = *At<uint8_t>(Addresses::mouse_enabled);
        auto& ingameCursorActive = *At<uint8_t>(Addresses::mouse_ingameCursorActive);

        if (mouseCaptured)
        {
            if (!haveSavedMouseEnabled)
            {
                savedMouseEnabled = mouseEnabled;
                haveSavedMouseEnabled = true;
            }

            mouseEnabled = 0;

            if (ingameCursorActive)
            {
                ingameCursorActive = 0;
                ::ReleaseCapture();
                while (::ShowCursor(TRUE) < 0)
                    ;
            }
        }
        else if (haveSavedMouseEnabled)
        {
            mouseEnabled = savedMouseEnabled;
            haveSavedMouseEnabled = false;
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // vid_xpos / vid_ypos are archived and updated from WM_MOVE - including the (-32000, -32000) position
    // Windows assigns to minimized windows. Quitting (or crashing) while minimized then writes those values to
    // config_mp.cfg, and the next windowed launch creates the window entirely off-screen. Keep the dvars pinned
    // to the last on-screen position so a bogus one can never be archived.
    // ---------------------------------------------------------------------------------------------------------

    void SanitizeWindowPositionDvars()
    {
        static Structures::dvar_t* xpos = nullptr;
        static Structures::dvar_t* ypos = nullptr;
        static int lastGoodX = 0;
        static int lastGoodY = 0;
        if (!xpos || !ypos)
        {
            xpos = Functions::FindDvar("vid_xpos");
            ypos = Functions::FindDvar("vid_ypos");
            if (!xpos || !ypos)
                return;
            if (xpos->type == Structures::DVAR_TYPE_INT && ypos->type == Structures::DVAR_TYPE_INT)
            {
                // until an on-screen position has been observed, fall back to the engine's registered defaults
                lastGoodX = xpos->defaultValue.integer;
                lastGoodY = ypos->defaultValue.integer;
            }
        }
        if (xpos->type != Structures::DVAR_TYPE_INT || ypos->type != Structures::DVAR_TYPE_INT)
            return;

        constexpr int offscreenLimit = -20000;  // minimized windows sit at -32000; real monitors never do

        const auto hwnd = *At<HWND>(Addresses::win_hwnd);
        const bool iconic = hwnd && ::IsIconic(hwnd);

        if (!iconic && xpos->value.integer > offscreenLimit && ypos->value.integer > offscreenLimit)
        {
            lastGoodX = xpos->value.integer;
            lastGoodY = ypos->value.integer;
        }
        else
        {
            xpos->value.integer = lastGoodX;
            ypos->value.integer = lastGoodY;
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // zPAM renders several info texts through client dvars picked up by its menus: the still-alive enemy names
    // (bottom right), the sniper / shotgun wielders (bottom left) and the server warnings ("Server password is
    // not set!", "This is an old version of map, use mp_xxx_fix!", changed cvars). The demo keeps re-setting
    // them through recorded server commands, so while the zPAM-text toggle is off they are blanked every frame.
    // ---------------------------------------------------------------------------------------------------------

    void SuppressZpamText()
    {
        if (HUD::showModText || !IsDemoPlaying())
            return;

        for (const auto name : {"ui_playersleft_list", "ui_sniper_info", "ui_shotgun_info", "ui_serverinfo_hud"})
        {
            const auto dvar = Functions::FindDvar(name);
            if (dvar && dvar->type == Structures::DVAR_TYPE_STRING && dvar->value.string && dvar->value.string[0])
                Functions::Dvar_SetString(dvar, "");
        }
    }

    void SetMouseCaptured(bool captured)
    {
        mouseCaptured = captured;
        ApplyMouseCapture();
    }

    bool IsMouseCaptured()
    {
        return mouseCaptured;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Com_ModifyMsec: the single choke point for demo time. Everything downstream (SV_Frame, cls.realtime and
    // thus cl.serverTime / CL_ReadDemoMessage) consumes the value we return.
    // ---------------------------------------------------------------------------------------------------------

    typedef int(__cdecl* Com_ModifyMsec_t)(int msec);
    Com_ModifyMsec_t Com_ModifyMsec_Trampoline = nullptr;

    int __cdecl Com_ModifyMsec_Hook(int msec)
    {
        ApplyMouseCapture();
        SanitizeWindowPositionDvars();
        HUD::SuppressShellshock();
        HUD::SuppressCursorHints();
        SuppressZpamText();

        const auto gameMsec = Com_ModifyMsec_Trampoline(msec);
        const auto delta = Components::Playback::CalculatePlaybackDelta(gameMsec);

        if (IsDemoPlaying())
        {
            // Skipping forward bumps cls.realtime by up to the whole demo length at once; CL_CheckTimeout would
            // read that as "no packet for minutes" and drop us with "server connection timed out".
            *At<int>(Addresses::clc_lastPacketTime) = *At<int>(Addresses::cls_realtime);

            // offline event scan of the loaded demo, a few milliseconds per frame
            DemoParser::Step(6.0);
        }

        return delta;
    }

    // ---------------------------------------------------------------------------------------------------------
    // FS_PureServerSetLoadedIwds: the demo's gamestate carries the pure-server IWD checksum list of whatever
    // files the recording server ran. Applying it locks local IWDs with different checksums out of the search
    // path, and demos then fail to load maps the viewer actually has (e.g. a different zPAM map pack version).
    // A demo viewer wants local files always usable, so the restriction is cleared during demo playback.
    // ---------------------------------------------------------------------------------------------------------

    // __usercall: checksum string in ECX, name string as a single stack argument that the CALLER cleans up
    // (verified in the disassembly: plain ret, ECX consumed by the first Cmd_TokenizeString, the name string
    // read from [esp+0x2020] above the 0x200C chkstk frame). No standard calling convention matches, so the
    // hook is a naked shim that swaps the arguments for empty strings while a demo is playing.
    uintptr_t FS_PureServerSetLoadedIwds_Trampoline = 0;
    static const char* const emptyPureList = "";

    static int __cdecl ShouldIgnorePureRestrictions()
    {
        if (!IsDemoPlaying())
            return 0;

        LOG_DEBUG("Ignoring pure server IWD restrictions during demo playback");
        return 1;
    }

    static int pureHookIgnore = 0;
    void __declspec(naked) FS_PureServerSetLoadedIwds_Hook()
    {
        __asm
        {
            pushad
            call ShouldIgnorePureRestrictions
            mov pureHookIgnore, eax
            popad
            cmp pureHookIgnore, 0
            je passthrough
            mov ecx, emptyPureList      // checksum list argument (ECX)
            mov eax, emptyPureList
            mov [esp + 4], eax          // name list argument (stack slot; argument slots are callee scratch)
        passthrough:
            jmp FS_PureServerSetLoadedIwds_Trampoline
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // CL_GetConfigString: CoD2x filters which custom IWD files get loaded while a demo plays - only names
    // prefix-matching the demo's sv_iwdNames systeminfo entry survive (its Sys_ListFiles hook). A demo from a
    // server with an older mod set (e.g. zpam_maps_v6) then unloads the viewer's zpam_maps_v7.iwd and the map
    // load fails even though the map exists locally. While a demo plays, serve a systeminfo whose sv_iwdNames
    // additionally lists every IWD present locally, so CoD2x keeps them all loaded.
    // ---------------------------------------------------------------------------------------------------------

    uintptr_t CL_GetConfigString_Trampoline = 0;

    static char doctoredSystemInfo[8192];

    const char* __cdecl DoctorSystemInfo(const char* original)
    {
        static bool active = false;

        if (!IsDemoPlaying() || original == nullptr)
        {
            active = false;
            return original;
        }

        const auto key = std::strstr(original, "\\sv_iwdNames\\");
        if (key == nullptr)
            return original;

        try
        {
            const auto valueStart = key + std::strlen("\\sv_iwdNames\\");
            const auto valueEnd = std::strchr(valueStart, '\\');  // nullptr when it is the last pair

            std::string result(original, valueStart);
            result.append(valueStart, valueEnd ? valueEnd - valueStart : std::strlen(valueStart));

            std::error_code ec;
            for (const auto& entry :
                 std::filesystem::directory_iterator(Functions::GetGameDirectory() / "main", ec))
            {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".iwd")
                    continue;
                const auto stem = entry.path().stem().string();
                if (stem.find(' ') != std::string::npos)
                    continue;
                result += " " + stem;
            }

            if (valueEnd)
                result += valueEnd;

            if (result.size() >= sizeof(doctoredSystemInfo))
                return original;

            std::memcpy(doctoredSystemInfo, result.c_str(), result.size() + 1);

            if (!active)
            {
                active = true;
                LOG_DEBUG("Serving systeminfo with all local IWDs appended to sv_iwdNames");
            }
            return doctoredSystemInfo;
        }
        catch (...)
        {
            return original;
        }
    }

    static int configStringIndex = 0;
    static const char* configStringResult = nullptr;
    void __declspec(naked) CL_GetConfigString_Hook()
    {
        __asm
        {
            mov configStringIndex, eax
            call CL_GetConfigString_Trampoline  // index still in EAX; returns the string in EAX
            mov configStringResult, eax
            cmp configStringIndex, 1            // CS_SYSTEMINFO
            jne done
            pushad
            push configStringResult
            call DoctorSystemInfo
            add esp, 4
            mov configStringResult, eax
            popad
        done:
            mov eax, configStringResult
            ret
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // SCR_UpdateFrame: skip rendering entirely while the game window is minimized (fullscreen CoD2 already does
    // this, windowed does not). Rendering into a minimized window eventually loses the D3D device, and the
    // renderer's silent device recreation executes a frame that still references freed frontend data - observed
    // as an access violation in RB_TessStaticModelCached while a paused demo sat in the background.
    // ---------------------------------------------------------------------------------------------------------

    typedef DWORD(__cdecl* SCR_UpdateFrame_t)();
    SCR_UpdateFrame_t SCR_UpdateFrame_Trampoline = nullptr;

    DWORD __cdecl SCR_UpdateFrame_Hook()
    {
        const auto hwnd = *At<HWND>(Addresses::win_hwnd);
        if (hwnd && ::IsIconic(hwnd))
        {
            return ::GetCurrentThreadId();  // what the original returns; callers use it as a recursion guard
        }

        return SCR_UpdateFrame_Trampoline();
    }

    // ---------------------------------------------------------------------------------------------------------
    // FS_Read: hand all reads on the demo file to core's Rewinding component.
    //
    // CoD2 demo records are [int32 serverMessageSequence][int32 length][byte data[length]] (length == -1 is the
    // end marker), read as three consecutive FS_Read calls. Core needs to know which of those a read is.
    // ---------------------------------------------------------------------------------------------------------

    enum class ReadState
    {
        Sequence,
        Length,
        Data
    };

    ReadState readState = ReadState::Sequence;
    int lastDemoHandle = 0;

    void ResetReadState()
    {
        readState = ReadState::Sequence;
    }

    typedef int(__cdecl* FS_Read_t)(void* buffer, int len, int handle);
    FS_Read_t FS_Read_Trampoline = nullptr;

    int __cdecl FS_Read_Hook(void* buffer, int len, int handle)
    {
        const auto demoHandle = GetDemoFileHandle();
        if (handle == 0 || handle != demoHandle || !IsDemoPlaying())
        {
            return FS_Read_Trampoline(buffer, len, handle);
        }

        if (handle != lastDemoHandle)
        {
            lastDemoHandle = handle;
            ResetReadState();
        }

        Components::Rewinding::DemoReadKind kind;
        switch (readState)
        {
            case ReadState::Sequence:
                kind = Components::Rewinding::DemoReadKind::MessageStart;
                break;
            case ReadState::Length:
                kind = Components::Rewinding::DemoReadKind::Other;
                break;
            default:
                kind = Components::Rewinding::DemoReadKind::Payload;
                break;
        }

        auto result = Components::Rewinding::FS_Read(buffer, len, kind);
        if (result == -1)
        {
            result = FS_Read_Trampoline(buffer, len, handle);
        }

        // advance the state machine based on what the game just read
        switch (readState)
        {
            case ReadState::Sequence:
                readState = ReadState::Length;
                break;
            case ReadState::Length:
            {
                const auto length = (len == 4) ? *reinterpret_cast<int*>(buffer) : -1;
                readState = (length == -1 || result != len) ? ReadState::Sequence : ReadState::Data;
                break;
            }
            default:
                readState = ReadState::Sequence;
                break;
        }

        return result;
    }

    // ---------------------------------------------------------------------------------------------------------
    // "demo" command: resolve the demo path, fire the demo lifecycle events and parse the demo bounds.
    // The original command loads the map and reads the first snapshots synchronously.
    // ---------------------------------------------------------------------------------------------------------

    std::filesystem::path currentDemoPath;
    std::string currentDemoName;

    std::filesystem::path GetCurrentDemoPath()
    {
        return currentDemoPath;
    }

    std::string GetCurrentDemoName()
    {
        return currentDemoName;
    }

    std::filesystem::path ResolveDemoPath(std::string arg)
    {
        constexpr std::string_view extension = ".dm_1";

        if (!arg.ends_with(extension))
            arg += extension;

        std::vector<std::filesystem::path> candidates;

        const auto gameDirName = Functions::GetGameDirName();
        for (auto dvarName : {"fs_homepath", "fs_basepath"})
        {
            const auto dvar = Functions::FindDvar(dvarName);
            if (dvar && dvar->value.string && dvar->value.string[0])
            {
                candidates.push_back(std::filesystem::path(dvar->value.string) / gameDirName / "demos" / arg);
                if (gameDirName != "main")
                    candidates.push_back(std::filesystem::path(dvar->value.string) / "main" / "demos" / arg);
            }
        }
        candidates.push_back(Functions::GetDemoDirectory() / arg);

        for (const auto& candidate : candidates)
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
        }

        return candidates.empty() ? std::filesystem::path(arg) : candidates.front();
    }

    typedef void(__cdecl* CL_PlayDemo_f_t)();
    CL_PlayDemo_f_t CL_PlayDemo_f_Trampoline = nullptr;

    void __cdecl CL_PlayDemo_f_Hook()
    {
        const std::string arg = GetCmdArgv(1);
        if (!arg.empty())
        {
            currentDemoPath = ResolveDemoPath(arg);
            currentDemoName = std::filesystem::path(arg).filename().replace_extension().string();
            LOG_DEBUG("demo command: '{}' -> '{}'", arg, currentDemoPath.string());

            Events::Invoke(EventType::PreDemoLoad);
            ResetReadState();
            Kills::OnDemoUnloaded();
        }

        CL_PlayDemo_f_Trampoline();

        if (!arg.empty() && IsDemoPlaying())
        {
            Kills::OnDemoLoaded();
            Events::Invoke(EventType::PostDemoLoad);

            try
            {
                DemoParser::Run();
                if (Kills::HasCompleteCache())
                    DemoParser::CancelScan();
            }
            catch (std::exception& ex)
            {
                LOG_ERROR("Failed to parse demo: {}", ex.what());
            }

            Events::Invoke(EventType::OnDemoBoundsDetermined);
        }
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::Com_ModifyMsec, reinterpret_cast<uintptr_t>(Com_ModifyMsec_Hook),
                                reinterpret_cast<uintptr_t*>(&Com_ModifyMsec_Trampoline));

        HookManager::CreateHook(Addresses::SCR_UpdateFrame, reinterpret_cast<uintptr_t>(SCR_UpdateFrame_Hook),
                                reinterpret_cast<uintptr_t*>(&SCR_UpdateFrame_Trampoline));

        HookManager::CreateHook(Addresses::FS_PureServerSetLoadedIwds,
                                reinterpret_cast<uintptr_t>(FS_PureServerSetLoadedIwds_Hook),
                                reinterpret_cast<uintptr_t*>(&FS_PureServerSetLoadedIwds_Trampoline));

        HookManager::CreateHook(Addresses::CL_GetConfigString, reinterpret_cast<uintptr_t>(CL_GetConfigString_Hook),
                                reinterpret_cast<uintptr_t*>(&CL_GetConfigString_Trampoline));

        HookManager::CreateHook(Addresses::FS_Read, reinterpret_cast<uintptr_t>(FS_Read_Hook),
                                reinterpret_cast<uintptr_t*>(&FS_Read_Trampoline));

        HookManager::CreateHook(Addresses::CL_PlayDemo_f, reinterpret_cast<uintptr_t>(CL_PlayDemo_f_Hook),
                                reinterpret_cast<uintptr_t*>(&CL_PlayDemo_f_Trampoline));

        Events::RegisterListener(EventType::PreDemoLoad, ResetReadState);
    }
}  // namespace IWXMVM::IW2::Hooks::Playback
