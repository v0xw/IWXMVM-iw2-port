#pragma once

namespace IWXMVM::IW2::Hooks::Fog
{
    // Fog override: force fog off, or apply another stock map's fog values on the fly.
    // enabled=false forces fog off; enabled=true with an empty preset shows the demo's own fog -
    // or, for demos whose mod never set any (zPAM comp rules suppress it), the current map's
    // stock fog. A non-empty preset applies that map's fog values instead. On fogless demos,
    // enabling fog also replays the map's ambient-weather particle emitters (dust, fog banks,
    // snow) the comp server suppressed alongside the fog.
    void SetOverride(bool enabled, const std::string& presetName);

    // Whether the loaded demo itself carries fog (a usable fog configstring).
    bool DemoHasFog();

    // The maps whose stock fog values can be applied.
    std::vector<std::string> GetPresetNames();

    // Called once per rendered frame (from the CG_Draw2D hook); keeps the override applied across
    // rewinds/gamestate re-parses and hands fog back to the game when the override is cleared.
    void Apply();
}  // namespace IWXMVM::IW2::Hooks::Fog
