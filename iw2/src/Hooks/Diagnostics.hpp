#pragma once

namespace IWXMVM::IW2::Hooks::Diagnostics
{
    // Logs Com_Error messages and vid_restart invocations (with their caller) to the mod log
    void Install();

    // Number of vid_restart / Com_Error calls seen since injection
    uint32_t GetVidRestartCount();
    uint32_t GetComErrorCount();

    // "0x12345678 (module.dll+0x1234)"
    std::string DescribeAddress(uintptr_t address);
}  // namespace IWXMVM::IW2::Hooks::Diagnostics
