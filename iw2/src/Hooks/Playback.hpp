#pragma once

namespace IWXMVM::IW2::Hooks::Playback
{
    void Install();

    // Called by the game interface when the mouse mode changes; the per-frame hook re-asserts the state.
    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured();

    // Absolute path of the demo currently being played (resolved in the "demo" command hook)
    std::filesystem::path GetCurrentDemoPath();
    std::string GetCurrentDemoName();
}  // namespace IWXMVM::IW2::Hooks::Playback
