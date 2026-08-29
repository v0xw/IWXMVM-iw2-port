# Fog control & ambient-FX replay

State of the fog feature as of 2026-08-29, including the reversing facts it is built on and what
is still open. Implementation: `iw2/src/Hooks/Fog.cpp`, UI in `core/src/UI/Components/VisualsMenu.cpp`,
game API in `core/src/GameInterface.hpp` (`GetAvailableFogPresets` / `HasDemoFog` / `SetFog`).

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

The Visuals tab gets a **Fog** checkbox plus a **From Map** preset combo (stock-map fog values);
fogless demos additionally get a **Particles** sub-toggle (on by default) that controls the
ambient-weather replay independently of the fog itself. Behavior matrix:

- **Demo carries fog** (vanilla/pub): checkbox starts on showing the demo's own fog; off forces
  fog off (`R_SwitchFog` to slot 0 every frame); a preset applies that map's `setExpFog` values
  (`R_SetFog` slot 1 + `R_SwitchFog`); back to "Original" re-runs `CG_ParseFog` once. Particles
  need no handling — the real entities are in the demo.
- **Demo carries no fog** (comp): checkbox starts off. Enabling it restores the current map's own
  stock fog values ("Original") or a chosen preset, **and** — while the **Particles** sub-toggle
  is on — replays the map's stock ambient-weather emitters client-side (see below); unticking it
  gives the fog alone. Renamed stock-map variants (`mp_toujane_fix`, `mp_matmata_fix`, ...) are
  matched by stock-name prefix up to a non-letter boundary. The Particles toggle is hidden on
  demos that carry fog — their particles are real entities the tool does not touch.

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

## Verified on

`seba_krompir_3v3_tj.dm_1` (zPAM comp, `mp_toujane_fix`): fog restore on/off, stock-fog prefix
match, preset fog, and the dust replay incl. the `FX_RegisterEffect` fallback (the fix map does
not precache `fx/dust/dust_wind_brown.efx`).

## Open items

- **Preset fog does not swap the particles.** The replayed emitters are always the *current*
  map's own — their origins are world coordinates anchored to that map's geometry, so another
  map's emitter set would land in the void. If "full preset atmosphere" is wanted (e.g. Leningrad
  snow on Toujane), the plausible approach is to keep the current map's emitter positions/delays
  and swap only the *effect* per preset (snow/dust/fogbank at the local anchor points). Needs a
  per-preset "atmosphere kind" mapping. (The replay itself is now independently switchable via
  the Particles sub-toggle, so fog color and particles can already be mixed on/off deliberately.)
- Ambient fire (`scr_allow_ambient_fire`: burning smoke plumes, `thin_light_smoke`) is
  deliberately not replayed — it reads as battle scenery rather than weather. Could become its
  own toggle.
- Free-form fog sliders (density/color) + keyframeable properties — see FEATURE-IDEAS.md; the
  renderer plumbing is done, this is UI + keyframe work.
- Locally registered fx handles are cached until the toggle/demo/map changes; a `vid_restart`
  while active could in theory leave a stale handle (the game-registered ones refresh via the
  configstring table, the fallback ones re-register on the next cache rebuild). Not observed in
  practice; toggle fog off/on after a vid_restart if particles ever vanish.
- Emitter data was generated by parsing the zPAM map GSCs; if it ever needs regenerating, the
  `scr_allow_ambient_weather` blocks in `zpam3-master/source/maps/mp/mp_*_fx.gsc` (plus each
  map's `loadfx` name→path table) are the source of truth.
