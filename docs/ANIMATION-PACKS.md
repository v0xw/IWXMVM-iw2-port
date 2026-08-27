# Imported death-animation packs (CoD4 / WaW / MW2)

Plan and conventions for bringing other CoD games' death animations into CoD2 demos.
The in-game switcher (Player Death Animations window) already groups animations by source
game; packs plug in purely through a naming convention — no further code changes needed.

## Naming convention (already wired into the UI)

Converted animations are registered under a game prefix; the UI groups by it and strips it
for display:

| Prefix     | Shown as | Source game |
| ---------- | -------- | ----------- |
| *(none)*   | CoD2     | native animations |
| `iwx_iw3_` | CoD4     | Call of Duty 4 |
| `iwx_iw4_` | MW2      | Modern Warfare 2 |
| `iwx_t4_`  | WaW      | World at War |

Only groups that actually contain animations appear in the dropdown.

## Conversion pipeline (offline, per animation)

1. **Extract** from the donor game with [Greyhound](https://github.com/Scobalula/Greyhound)
   (open-source fork of Wraith Archon) → export XAnims as **SEAnim**.
   Requires the donor game's files — extract from your own installed copy.
2. **Retarget** in Blender: import with
   [io_anim_seanim](https://github.com/SE2Dev/io_anim_seanim), retarget onto the CoD2
   player skeleton. This step is *required*, not optional: the 2010s-era "xanim converter"
   route produces corrupted results on full-body animations (community consensus:
   `j_mainroot` origin/bind-pose differences → sunken torsos, stretched limbs). Constraint-
   based retargeting handles it since the skeletons share most bone names
   (`j_mainroot`, `pelvis`, `j_hip_*`, ...). Pick **fully keyframed** deaths only — CoD4+
   deaths that hand off to ragdoll are useless in CoD2 (no physics).
3. **Export** as `XANIM_EXPORT` v3 with [blender-cod](https://github.com/CoDEmanX/blender-cod)
   (supports CoD2 targets), renamed to the pack convention above.
4. **Compile** with the CoD2 mod tools converter into binary xanims.

## CoD2-side integration

- Ship a content IWD containing: the compiled `xanim/iwx_*` files, plus `mp/playeranim.script`
  and `animtrees/multiplayer.atr` with the new animations **appended after** the stock
  entries. Appending keeps all stock animation indices stable, so recorded demos resolve
  their networked anim indices unchanged; the new entries only exist to be selected by the
  switcher. The port already serves local IWDs into demo playback (`sv_iwdNames` appending).
- Table headroom: the engine caps at 512 animations (`MAX_ANIMATIONS`); stock MP uses well
  under that, so there is room for a generous pack. The switcher re-enumerates the table
  per demo, so mod demos (zpam) that ship their own animscripts coexist fine.

## Security policy for third-party files

- Tools only from their canonical open-source repos (Greyhound / io_anim_seanim /
  blender-cod are all source-available); scan any downloaded release binaries
  (`MpCmdRun -Scan -File ...`) before running.
- No prebuilt community "anim packs" or self-extracting archives from forums — we convert
  from game files ourselves. The files we produce (`.seanim`, `.xanim_export`, compiled
  xanims, `.iwd`) are pure data, never executables.
- Donor assets come from installed copies of games you own.
