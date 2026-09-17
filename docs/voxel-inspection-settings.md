# Voxel backend visibility and enemy range inspection

This patch is based on `voxel-live` at
`1baf252db8c30fd4505d719ff6ed0cfd7e6f908b`.

## Finding the backend

The previous macOS Settings window puts the OpenGL/software selector in an
870-point scrolling document, above Video shader. OpenGL already defaults to on;
the game title says `Voxels: live - OpenGL GPU` when it is actually in use.
The preference alone is not proof of the active backend after a GPU error.

The updated window pins Voxel rendering, its enable checkbox, and the backend
selector above the scrolling settings. The same choice is available directly
under **Settings > Voxel Backend > OpenGL (GPU) / Software (CPU)**. Both controls
write the same preference. On activation, the running game picks up a changed
backend preference from the separate Library process; an unchanged preference
does not override a session-only command-line choice or GPU failure fallback.

## Enemy inspection

The native engine removes an inspected enemy from pathfinding occupancy using
`US_HIDDEN`, but leaves its standing map-sprite handle visible. It does not
allocate a player movement animation for this range-only inspection. The previous
voxel renderer demanded such an animation and reported `unsupported active-unit
animation`, returning the entire view to 2D.

The renderer now distinguishes range inspection from travel. An idle player-phase
range view may use the existing standing sprite only when its live unit, class,
map position, handle, palette/tile descriptor, snapshot sprite-list entry and fog
visibility agree. Hidden/dead/rescued actors, conflicting occupants, stale handles
and actual unsupported travel still fail safely. It does not clear any hide bit,
write to the game, reveal fog, or synthesize an enemy movement animation.

## Camera settling

A long mouse route also reproduced one native-renderer fallback after the logical
camera stopped. The exact-position terrain match was 14%; a bounded eight-pixel
alignment search found an 85% match with the current framebuffer. The PPU was
still finishing the scroll even though the logical camera had stopped changing.

The shared tactical alignment helper keeps the cheap exact-position check on
settled frames, retrying the existing bounded search only when it fails. The
acceptance threshold and independent HUD verification are unchanged. This is
fresh frame verification, not a stale-HUD latch or a timeout hiding bad frames.

## Validation and limits

The local Linux suite passed 49/49 tests with the supplied Archanae and Sacred
Echoes ROMs. The expanded interaction test uses normal game/controller input to
visit every visible ally and enemy in the fixture maps, hover, inspect ranges,
and cancel. It checks every frame for a 3D presentation, preserved actors, fixed
camera projection and serialized-state immutability. Existing player selection,
walking, action-menu, cancellation and Wait checks remain enabled; acted-unit
hover is also exercised.

Synthetic renderer cases cover range-only red, green and acted-blue ownership,
plus negative cases for stale/missing/hidden handles, wrong tiles/palettes, fog,
dead/rescued units, conflicting occupants, invalid class pointers and unsupported
travel. The renderer passes AddressSanitizer, UndefinedBehaviorSanitizer and leak
detection. The actual Linux OpenGL presenter passes geometry/depth, transparency,
resize, capture and native/software/GPU switching readback tests.

The macOS regression was expanded to check the direct menu, preference changes,
visibility at minimum window size and while scrolling, and to capture the pinned
header. **These updated Cocoa tests have not been run on macOS in this workspace.**
The existing repository workflow will run them after publication. An included
macOS screenshot is from the previously published build, not this changed layout.
No M1 performance or end-to-end macOS gameplay result is claimed.

This patch does not make every menu/event into a 3D scene. Combat, dialogue,
enemy-phase travel, inventory, unsupported HUD layouts and unverified transition
frames retain the native safety fallback. Ordinary enemy range inspection is the
specific newly supported case. No ROMs, save states, or decoded assets are bundled
in the source patch.
