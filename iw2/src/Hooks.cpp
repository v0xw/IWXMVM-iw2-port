#include "StdInclude.hpp"
#include "Hooks.hpp"

#include "Hooks/Playback.hpp"
#include "Hooks/Camera.hpp"
#include "Hooks/HUD.hpp"
#include "Hooks/Kills.hpp"
#include "Hooks/Diagnostics.hpp"

namespace IWXMVM::IW2::Hooks
{
    void Install()
    {
        Hooks::Diagnostics::Install();
        Hooks::Playback::Install();
        Hooks::Camera::Install();
        Hooks::HUD::Install();
        Hooks::Kills::Install();
    }
}  // namespace IWXMVM::IW2::Hooks
