# Future feature ideas

Brainstormed 2026-08-27. Roughly ranked by bang-for-buck; feasibility notes reflect what the
codebase already has.

## Death animation switching (enable the dormant upstream feature)

Upstream IWXMVM already ships a death-anim switcher (`core/src/Components/PlayerAnimation.*`,
`PlayerAnimationUI`): the IW3 hook on `CG_ProcessEntity` rewrites a corpse's `legsAnim` to any
animation whose name contains "death", enumerated live from `bgs.animScriptData.animations[]` —
plus "hide all corpses" and "attach weapon to corpse". The UI is hidden on CoD2 only because
`IW2Interface::GetSupportedFeatures()` returns `Features_None`.

Porting the hook is tractable now: `cg_bgs` is mapped (see the bone-camera work), CoD2 is where
the animScriptData system originated, `ET_PLAYER_CORPSE` and the `entityState` anim fields
(`legsAnim` @ 0xCC) are in `Structures.hpp`, and `CG_ProcessEntity` is known in the decompiles.
Steps: recover CoD2's `animation_s` array layout from the Mac decompile, hook `CG_ProcessEntity`,
rewrite corpse `legsAnim` (mind CoD2's anim toggle-bit mask), set `Features_ChangeAnimations`.

### CoD4/WaW death animations (content project, rides on the above)

No decompilations needed — it's an asset-conversion job, and demo-safe because demos network anim
*indices* into the client-side anim tree; replacing or extending the assets changes what plays.
Pipeline: Greyhound/Wraith Archon exports xanims from CoD4/WaW/MW2 → convert to `xanim_export` →
CoD2 mod tools compile them → ship as an optional IWD. Skeletons are closely related (same
`j_`/`tag_` lineage); pick fully-keyframed deaths only (CoD2 has no ragdoll to blend into).
Once the anims exist in the animtree, the switcher above lists them automatically; a small
deterministic variety hook (choose by entity+time so multi-pass captures agree) could randomize
them across a demo.

## Fog control

The natural companion to the sky switcher (its known limitation: map fog gives away the swap).
Expose the per-map fog start/end/color in the Visuals tab like the sun controls. Requires finding
the fog state/command in the gfx DLL — same reversing recipe as the sky work (see
`cod2-renderer-facts` notes / commit 2884238's address-hunting approach).

## Camera export to After Effects / Blender

Dump a campath or captured camera as a per-frame AE camera (`.jsx`) or FBX so editors can
composite 3D text/particles that stick to the world. The exact projection formula is known
(infinite far plane; see renderer notes). Pure math + file writing, no reversing.

## In-engine motion blur

Real shutter-angle blur: render N sub-frames per output frame at fractional demo times and
average them in the capture pipeline. Demos are deterministic and capture already controls
playback timing, so this is mostly plumbing in `CaptureManager`.

## Corpse persistence

CoD2 fades corpses out of a small body queue; patch the queue/fade client-side so battlefields
stay littered for wide shots. Small effort, high visual impact.

## Custom sky import

Extend the sky switcher to load user-supplied cubemaps (e.g. a folder of .iwi/DDS next to the
port). The switcher infrastructure (material registration + texdef patching) makes this
incremental.

## Per-player visibility / model override

Hide specific players for clean shots, or force chosen skins per team. Demos carry model names in
configstrings resolved client-side, so both are swappable without touching the demo.

## Depth pass precision upgrade (only if banding shows up)

A depth capture pass already exists (`CaptureManager`'s `Depth` pass). The one gap is precision:
it renders through a shader into 8-bit video, which can band on far gradients. A 16-bit PNG/EXR
linear-depth sequence would fix that — an upgrade, not a new feature; only worth it if banding
actually bothers anyone in post.
