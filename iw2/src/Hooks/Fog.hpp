#pragma once

namespace IWXMVM::IW2::Hooks::Fog
{
    // Installs the FX_PlayEffect detour that mutes the ambient-weather particles a demo itself
    // carries (see SetOverride's particles flag).
    void Install();

    // Fog override: force fog off, or apply another stock map's fog values on the fly.
    // enabled=false forces fog off; enabled=true with an empty preset shows the demo's own fog -
    // or, for demos whose mod never set any (zPAM comp rules suppress it), the current map's
    // stock fog. A non-empty preset applies that map's fog values instead. Fog only - the
    // ambient particles have their own override below.
    void SetFogOverride(bool enabled, const std::string& presetName);

    // Ambient-weather particle override, independent of the fog. enabled=false mutes the real
    // emitter entities a vanilla demo carries and disables the client-side replay that fogless
    // (comp) demos get; enabled=true shows whichever of the two applies. An empty preset plays
    // the current map's own particle style; a non-empty preset swaps it for that map's dominant
    // weather effect at the current map's emitter anchor points (Leningrad snow on Toujane, ...).
    void SetParticlesOverride(bool enabled, const std::string& presetName);

    // Whether the loaded demo itself carries fog (a usable fog configstring).
    bool DemoHasFog();

    // The maps whose stock fog values can be applied.
    std::vector<std::string> GetPresetNames();

    // The maps whose ambient particle style can be applied (all maps with an ambient block).
    std::vector<std::string> GetParticlePresetNames();

    // Called once per rendered frame (from the CG_Draw2D hook); keeps the override applied across
    // rewinds/gamestate re-parses and hands fog back to the game when the override is cleared.
    void Apply();
}  // namespace IWXMVM::IW2::Hooks::Fog
