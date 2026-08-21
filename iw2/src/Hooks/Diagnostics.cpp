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

    void LogComError(int code, const char* fmt, uintptr_t caller)
    {
        ++comErrorCount;
        LOG_ERROR("Com_Error({}) from {}: {}", code, DescribeAddress(caller), fmt ? fmt : "(null)");
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

    void Install()
    {
        HookManager::CreateHook(Addresses::Cbuf_AddText, reinterpret_cast<uintptr_t>(Cbuf_AddText_Hook), &Cbuf_AddText_Trampoline);
        HookManager::CreateHook(Addresses::Com_Error, reinterpret_cast<uintptr_t>(Com_Error_Hook), &Com_Error_Trampoline);
        HookManager::CreateHook(Addresses::CL_Vid_Restart_f, reinterpret_cast<uintptr_t>(VidRestart_Hook), &VidRestart_Trampoline);
    }
}  // namespace IWXMVM::IW2::Hooks::Diagnostics
