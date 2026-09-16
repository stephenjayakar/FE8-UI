# Stable voxel cursor and native hover billboards

This fixes two idle-map regressions in the `voxel-live` presentation. No ROM,
save, or emulated RAM is modified; existing selection/combat fallback remains.

## Cursor movement no longer switches cameras

FE8 slides the unit, objective, and terrain HUD panels between screen edges as
the cursor moves. The original HUD extractor recognized only fully open panel
rectangles. Narrower/shorter in-flight panels failed verification and repeatedly
switched the entire display between the voxel and native projections.

The extractor now recognizes the edge-clipped versions of those same supported
panel shapes, including a genuinely empty idle-HUD frame between slides. It
continues to reconstruct the current pixels and reject unknown BG0/BG1 content,
unsupported OBJ effects, selection/menu states, fades, and mismatched frames.
This is not a delay, stale-frame latch, or removal of the HUD safety gate.

In voxel mode, a stationary pointer keeps its world-tile target through sprite
animation. A click at that unchanged position confirms the same target instead
of re-picking a temporarily transparent sprite pixel. Pointer motion re-picks;
camera changes, scene changes, state loads, and invalid hits clear stale targets.
Native 2D camera reprojection is unchanged.

## Highlighted units retain their hover animation

After five frames over an eligible ally, FE8 hides the standing map sprite and
starts a native moving-unit (MU) sprite, even though the unit is not selected.
The old billboard renderer saw only the standing hide bit and omitted the unit.

The renderer now decodes the matching live OAM/OBJ hover image as a camera-facing
billboard. This preserves the ROM's hover animation, palette, transparency and
flip flags, and keeps the billboard and click target anchored to its map tile.
The match requires an idle, visible blue cursor occupant, a matching unit-map
entry and standing-sprite handle, and a compatible live OBJ at the native unit
position. Small native idle-animation offsets are allowed; neighboring sprites,
fog-hidden/dead/rescued/event-hidden units, unsupported OBJ formats and actual
selected-unit travel are not restored by clearing hide flags.

## Regression coverage

`test_native_hud` checks edge-clipped unit/terrain/objective shapes on both screen
edges, empty idle HUD, frame immutability, and rejection of unknown UI/menu states.
`test_voxel_renderer` checks native hover handoff, animation, flip flags, small
OBJ offsets, raised-sprite picking, and negative cases for hidden/fog/dead actors,
wrong occupants/handles/palettes, unsupported OBJ formats and busy game states.

`test_voxel_interaction_rom` boots a user-supplied ROM with normal game input.
It checks every rendered frame through held-key cursor routes that cross native
camera/HUD boundaries, then uses the actual mouse controller to visit each
eligible ally and wait through the hover handoff. All standing/hover actors must
remain accounted for, every idle frame must stay in the voxel presentation,
and a projected world landmark must remain stationary. A-selection and action
menus must still fall back to native rendering, with cancellation restoring
voxels. Serialized emulator state is compared before/after presentation calls.

Configure `FE8_ARCHANAE_ROM` and/or `FE8_SACRED_ECHOES_ROM` to run the optional
interaction tests locally; ROMs and saves are not bundled or uploaded to CI.
The local Linux suite with both supplied ROMs passes 48/48 tests. Native macOS
validation runs the ROM-independent suite, including actual AppKit settings
and OpenGL compositor tests; it is not an end-to-end Mac ROM gameplay test.

The actual Linux client was also driven with X11 keyboard/mouse events. The
same cursor route produced five HUD-driven native fallbacks before this patch
and zero afterward. Conversation screenshots are actual application captures.

## Scope

Merely hovering or moving the cursor stays in 3D. Confirming a unit with A or a
click, moving a selected unit, action menus, dialogue, combat and unsupported
scenes still use the native fallback. This patch does not implement full 3D
movement/combat. Billboard and scenery caching from the performance pass remain.
