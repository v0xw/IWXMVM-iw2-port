#pragma once

namespace IWXMVM::Types
{
    // A game-specific fine-grained HUD toggle, described by the game module and rendered
    // generically by the visuals tab. The id is stable and persisted in presets as
    // "iwxmvm_ui_<id>".
    struct HudToggle
    {
        enum class Section
        {
            Main,             // listed with the other HUD element toggles
            KillfeedFilters,  // indented under "Show Killfeed", applies while it is on
        };

        std::string id;
        std::string label;
        Section section = Section::Main;
        bool value = true;
        bool requiresModDemo = false;  // greyed out unless a mod demo is loaded
    };
}  // namespace IWXMVM::Types
