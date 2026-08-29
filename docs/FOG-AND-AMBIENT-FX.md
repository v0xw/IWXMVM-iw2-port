# Fog control & ambient-FX replay

State of the fog feature as of 2026-08-29, including the reversing facts it is built on and what
is still open. Implementation: `iw2/src/Hooks/Fog.cpp`, UI in `core/src/UI/Components/VisualsMenu.cpp`,
game API in `core/src/GameInterface.hpp` (`GetAvailableFogPresets` / `HasDemoFog` / `SetFog` /
`GetAvailableParticlePresets` / `SetAmbientParticles`).

## What vanilla "fog" actually is

On the stock maps the foggy look is two independent systems, both driven by the map GSC and both
suppressed by comp configs (zPAM comp rules default all `scr_allow_ambient_*` dvars to 0):

1. **Distance fog** — `setExpFog(density, r, g, b, 0)` behind `scr_allow_ambient_fog`. Server-side
   it lands in configstring 12 (`CS_FOGVARS`); the client's `CG_ParseFog` (0x4D07D0) hands it to
   the renderer. No configstring → the demo renders no fog at all.
2. **Ambient weather particles** — the `scr_allow_ambient_weather` blocks: `loopfx` calls that
   spawn looped-fx **entities** server-side (blowing dust, fog banks, snow, smoke banks). Pub
   demos carry these entities in their snapshots; comp demos simply don't contain them.

## What the tool does

The Visuals tab gets a **Fog** checkbox and an independent **Particles** checkbox, each with its
own **From Map** preset combo — stock-map fog values on one, stock-map particle styles on the
other (the particle list has one extra entry: `mp_decoy` has night dust but sets no fog).
Behavior matrix:

- **Demo carries fog** (vanilla/pub): both checkboxes start on, showing the demo as-is. Fog off
  forces fog off (`R_SwitchFog` to slot 0 every frame); a preset applies that map's `setExpFog`
  values (`R_SetFog` slot 1 + `R_SwitchFog`); back to "Original" re-runs `CG_ParseFog` once.
  The demo's particles are real looped-fx entities in its snapshots; Particles off mutes them
  through the `FX_PlayEffect` filter (see below).
- **Demo carries no fog** (comp): both checkboxes start off. Fog on restores the current map's
  own stock fog values ("Original") or a chosen preset; Particles on replays the map's stock
  ambient-weather emitters client-side — each independent of the other, so fog-only, dust-only,
  or both. Renamed stock-map variants (`mp_toujane_fix`, `mp_matmata_fix`, ...) are matched by
  stock-name prefix up to a non-letter boundary.
- **Particle preset**: while Particles is on and its combo selects a *different* stock map, the
  current map's emitter anchor points play the preset map's **dominant weather effect** (its
  most used ambient efx, with that emitter's firing delay) instead of their own — Leningrad
  snow on Toujane, Toujane dust on Railyard. The preset map's own emitters can't be replayed
  directly: their origins are world coordinates anchored to that map's geometry. On demos that
  carry real emitters the swap also mutes them so the styles don't stack. Fog and particle
  presets are fully independent — Railyard fog with Matmata dust is a valid combination.

Fog values live in `STOCK_MAP_FOG` (copied from the map GSCs' `setExpFog` calls; `mp_decoy` sets
none). Emitters live in `STOCK_MAP_AMBIENT` — extracted from the `scr_allow_ambient_weather`
blocks of the stock map GSCs (as mirrored in zPAM's sources, which wrap the stock calls in the
`scr_allow_*` conditions): 15 maps, 247 emitters, per-map effect variants (`dust_wind_brown` on
Toujane, `dust_wind_brown_thick` on Matmata, `dust_wind_eldaba` on Burgundy/Carentan/Rhine, snow
on Downtown/Harbor/Railyard/Leningrad, `dust_wind_night` on Decoy).

## Reversed facts (CoD2MP_s.exe 1.3, verified by disassembly)

- Renderer fog goes through the exe's gfx function table (base 0x68A1E8, refilled from the gfx
  DLL on vid_restart — read slots at call time): `R_SetFog` @ slot 0x68A264
  (`int __cdecl(index, near, far, r255, g255, b255, density)`, density < 1 = exponential),
  `R_SwitchFog` @ 0x68A268 (`int __cdecl(index, timeMs, durationMs)`). Slot 0 = no fog, slot 1 =
  the script fog. This mirrors exactly what `CG_ParseFog` does with an exp-fog configstring.
- The looped-fx entity handler (eType 8 branch of the `CG_ProcessEntity` dispatch, 0x4CD6B0) is
  the template for the replay: per entity it keeps a next-fire time, fires once per elapsed
  interval catching up in whole `delay` steps, clamps + fires immediately when `cg.time` went
  backwards, then calls `FX_PlayEffect`.
- `FX_PlayEffect` = 0x499570, custom convention: ECX = the fx system (`*(void**)0x19A1BEC`),
  stack args `(fxHandle, float origin[3], float forward[3])`, callee-cleaned (`ret 0xC`) — i.e.
  `__fastcall` with a dummy EDX. (`FX_PlaySimpleEffect` = 0x499500, EDX = handle, ECX = &origin,
  for the zero-forward case — currently unused, every stock emitter has a direction.)
- Effect names are configstrings: `CS_EFFECT_NAMES` = 846, 64 slots, fx id = cs − 846 (ids 1..63),
  content is the server's `loadfx` path **verbatim** (`G_EffectIndex` → `G_FindConfigstringIndex`
  base 846). The client registers each via `FX_RegisterEffect` = 0x49A9F0
  (`int __cdecl(const char* name)` → handle, accepts the full `fx/....efx` form) and stores
  handles at `0x14E5308 + cs*4`, i.e. the id-indexed table at `0x14E6040` (`cgs_fxHandles`).
- Custom maps (e.g. `mp_toujane_fix`) precache their own effect set, so a wanted stock effect may
  not be in the demo's configstrings at all. In that case the tool calls `FX_RegisterEffect`
  itself — the same call the game runs when an effect configstring arrives, loading the `.efx`
  from the game's own IWDs. On first fallback the demo's precached effect list is dumped to the
  log at debug level.
- The replay runs from the CG_Draw2D hook (`Fog::Apply`), one frame stage later than the engine's
  own entity processing; the fx system picks the spawned particles up on the next frame. Verified
  working in practice.
- Muting the demo's own ambient entities has no per-entity kill switch, but every looped-fx
  firing funnels through `FX_PlayEffect` — so the tool detours it and swallows calls whose
  handle belongs to a game-registered ambient-weather effect (the `STOCK_MAP_AMBIENT` efx paths
  matched against the effect-name configstrings, handles read from `cgs_fxHandles`). The tool's
  own replay calls the trampoline directly so the filter cannot eat a swapped-in effect that
  shares a handle with a muted one. Already-spawned particles live out their (short) lifetime
  after muting kicks in.

## Verified on

`seba_krompir_3v3_tj.dm_1` (zPAM comp, `mp_toujane_fix`): fog restore on/off, stock-fog prefix
match, preset fog, and the dust replay incl. the `FX_RegisterEffect` fallback (the fix map does
not precache `fx/dust/dust_wind_brown.efx`). The `FX_PlayEffect` mute filter and the particle
style swap verified in game 2026-08-29; the separate particle preset combo is UI wiring on top
of the same two mechanisms.

## Open items

- The particle style swap contributes only the preset map's *dominant* effect — a mixed
  ambient set (Railyard's snow + fog banks + smoke banks) collapses to its most used one. Per
  emitter the current map's anchors and the preset effect's own firing delay are used.
- The mute filter swallows the entire weather block, fog/smoke banks included, and would also
  mute non-weather uses of the same assets (e.g. `thin_light_smoke_L` doubling as ambient-fire
  smoke on maps that use that exact asset for both).
- Ambient fire (`scr_allow_ambient_fire`: burning smoke plumes, `thin_light_smoke`) is
  deliberately not replayed — it reads as battle scenery rather than weather. Could become its
  own toggle.
- Free-form fog sliders (density/color) + keyframeable properties — see FEATURE-IDEAS.md; the
  renderer plumbing is done, this is UI + keyframe work.
- Locally registered fx handles are cached until the toggle/demo/map changes; a `vid_restart`
  while active could in theory leave a stale handle (the game-registered ones refresh via the
  configstring table, the fallback ones re-register on the next cache rebuild). Not observed in
  practice; toggle fog off/on after a vid_restart if particles ever vanish. The muted-handle set
  of the `FX_PlayEffect` filter is cached the same way and shares the caveat.
- Emitter data was generated by parsing the zPAM map GSCs; if it ever needs regenerating, the
  `scr_allow_ambient_weather` blocks in `zpam3-master/source/maps/mp/mp_*_fx.gsc` (plus each
  map's `loadfx` name→path table) are the source of truth.
