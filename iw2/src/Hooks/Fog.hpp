#pragma once

namespace IWXMVM::IW2::Hooks::Fog
{
    // Installs the FX_PlayEffect detour that mutes the ambient-weather particles a demo itself
    // carries (see SetOverride's particles flag).
    void Install();

    // Fog override: force fog off, or apply another stock map's fog values on the fly.
    // enabled=false forces fog off; enabled=true with an empty preset shows the demo's own fog -
    // or, for demos whose mod never set any (zPAM comp rules suppress it), the current map's
    // stock fog. A non-empty preset applies that map's fog values instead, and carries that
    // map's atmosphere with it: the current map's emitter anchor points play the preset map's
    // dominant weather effect (Leningrad snow on Toujane, ...).
    //
    // particles toggles the ambient-weather particles (dust, fog banks, snow) independently of
    // the fog: false mutes the real emitter entities a vanilla demo carries and disables the
    // client-side replay that fogless (comp) demos get; true shows whichever of the two applies.
    void SetOverride(bool enabled, const std::string& presetName, bool particles);

    // Whether the loaded demo itself carries fog (a usable fog configstring).
    bool DemoHasFog();

    // The maps whose stock fog values can be applied.
    std::vector<std::string> GetPresetNames();

    // Called once per rendered frame (from the CG_Draw2D hook); keeps the override applied across
    // rewinds/gamestate re-parses and hands fog back to the game when the override is cleared.
    void Apply();
}  // namespace IWXMVM::IW2::Hooks::Fog
