#include "StdInclude.hpp"
#include "Diagnostics.hpp"

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"

namespace IWXMVM::IW2::Hooks::Diagnostics
{
    std::atomic<uint32_t> vidRestartCount = 0;
    std::atomic<uint32_t> comErrorCount = 0;

    uint32_t GetVidRestartCount()
    {
        return vidRestartCount;
    }

    uint32_t GetComErrorCount()
    {
        return comErrorCount;
    }

    std::string DescribeAddress(uintptr_t address)
    {
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module) && module)
        {
            char name[MAX_PATH];
            GetModuleFileNameA(module, name, MAX_PATH);
            return std::format("{:#x} ({}+{:#x})", address, std::filesystem::path(name).filename().string(),
                               address - reinterpret_cast<uintptr_t>(module));
        }
        return std::format("{:#x}", address);
    }

    // --- Com_Error(int code, const char* fmt, ...) -------------------------------------------------------------

    uintptr_t Com_Error_Trampoline = 0;

    // configstrings 0 (serverinfo) and 1 (systeminfo) carry mapname, fs_game, sv_pure, sv_iwds - what a demo
    // demands from the local filesystem. Logged on errors during demo playback to diagnose map load failures.
    void LogGameStateInfo()
    {
        const auto stringOffsets = reinterpret_cast<const int*>(Addresses::cl_gameState);
        const auto stringData = reinterpret_cast<const char*>(Addresses::cl_gameState + 2048 * sizeof(int));
        for (int cs = 0; cs <= 1; cs++)
        {
            const auto offset = stringOffsets[cs];
            if (offset <= 0 || offset >= 0x20000)
                continue;
            LOG_INFO("configstring {}: {:.900}", cs, stringData + offset);
        }
    }

    void LogComError(int code, const char* fmt, uintptr_t caller)
    {
        ++comErrorCount;
        LOG_ERROR("Com_Error({}) from {}: {}", code, DescribeAddress(caller), fmt ? fmt : "(null)");

        if (*reinterpret_cast<const int*>(Addresses::clc_demoplaying) != 0)
            LogGameStateInfo();
    }

    void __declspec(naked) Com_Error_Hook()
    {
        static int code;
        static const char* fmt;
        static uintptr_t caller;

        __asm
        {
            mov eax, [esp]
            mov caller, eax
            mov eax, [esp + 4]
            mov code, eax
            mov eax, [esp + 8]
            mov fmt, eax
            pushad
        }

        LogComError(code, fmt, caller);

        __asm
        {
            popad
            jmp Com_Error_Trampoline
        }
    }

    // --- vid_restart ------------------------------------------------------------------------------------------

    uintptr_t VidRestart_Trampoline = 0;

    void LogVidRestart(uintptr_t caller)
    {
        ++vidRestartCount;
        LOG_WARN("vid_restart #{} requested from {}", vidRestartCount.load(), DescribeAddress(caller));
    }

    void __declspec(naked) VidRestart_Hook()
    {
        static uintptr_t caller;

        __asm
        {
            mov eax, [esp]
            mov caller, eax
            pushad
        }

        LogVidRestart(caller);

        __asm
        {
            popad
            jmp VidRestart_Trampoline
        }
    }

    // --- Cbuf_AddText(text in EAX) ------------------------------------------------------------------------------

    uintptr_t Cbuf_AddText_Trampoline = 0;

    void LogCbufAddText(const char* text, uintptr_t caller)
    {
        if (!text)
            return;
        std::string_view sv(text);
        if (sv.find("vid_restart") != std::string_view::npos || sv.find("disconnect") != std::string_view::npos ||
            sv.find("demo") != std::string_view::npos)
        {
            std::string oneLine(sv.substr(0, 120));
            std::replace(oneLine.begin(), oneLine.end(), '\n', ' ');
            LOG_DEBUG("Cbuf_AddText from {}: \"{}\"", DescribeAddress(caller), oneLine);
        }
    }

    void __declspec(naked) Cbuf_AddText_Hook()
    {
        static const char* text;
        static uintptr_t caller;

        __asm
        {
            mov text, eax
            mov ecx, [esp]
            mov caller, ecx
            pushad
        }

        LogCbufAddText(text, caller);

        __asm
        {
            popad
            jmp Cbuf_AddText_Trampoline
        }
    }

    // --- first-chance access violation logging ----------------------------------------------------------------
    //
    // The UI render loop swallows access violations into a generic "error occurred while rendering" message
    // (on x86 MSVC, catch(...) catches SEH exceptions), losing the faulting address. This vectored handler
    // observes every access violation before any handler runs and logs where it happened, plus any stack
    // values that resolve to code addresses as a rough backtrace. It never handles anything itself.

    std::atomic<int> loggedAvCount = 0;

    bool IsCodeAddress(uintptr_t address)
    {
        HMODULE module = nullptr;
        return address >= 0x10000 &&
               GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCSTR>(address), &module) &&
               module != nullptr;
    }

    LONG CALLBACK FirstChanceAvLogger(EXCEPTION_POINTERS* info)
    {
        if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
            return EXCEPTION_CONTINUE_SEARCH;

        static thread_local bool inHandler = false;
        if (inHandler || loggedAvCount.fetch_add(1) >= 20)
            return EXCEPTION_CONTINUE_SEARCH;
        inHandler = true;

        const auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
        const auto isWrite = info->ExceptionRecord->ExceptionInformation[0] != 0;
        const auto target = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
        LOG_ERROR("First-chance access violation at {} ({} {:#x}), esp {:#x}", DescribeAddress(address),
                  isWrite ? "writing" : "reading", target, static_cast<uintptr_t>(info->ContextRecord->Esp));

        // scan the top of the stack for code addresses as a rough backtrace
        const auto esp = static_cast<uintptr_t>(info->ContextRecord->Esp);
        int logged = 0;
        for (uintptr_t slot = esp; slot < esp + 0x400 && logged < 8; slot += sizeof(uintptr_t))
        {
            if (IsBadReadPtr(reinterpret_cast<const void*>(slot), sizeof(uintptr_t)))
                break;
            const auto value = *reinterpret_cast<const uintptr_t*>(slot);
            if (IsCodeAddress(value))
            {
                LOG_ERROR("  stack[{:#x}]: {}", slot - esp, DescribeAddress(value));
                logged++;
            }
        }

        inHandler = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::Cbuf_AddText, reinterpret_cast<uintptr_t>(Cbuf_AddText_Hook), &Cbuf_AddText_Trampoline);
        HookManager::CreateHook(Addresses::Com_Error, reinterpret_cast<uintptr_t>(Com_Error_Hook), &Com_Error_Trampoline);
        HookManager::CreateHook(Addresses::CL_Vid_Restart_f, reinterpret_cast<uintptr_t>(VidRestart_Hook), &VidRestart_Trampoline);

        AddVectoredExceptionHandler(0, FirstChanceAvLogger);
    }
}  // namespace IWXMVM::IW2::Hooks::Diagnostics
