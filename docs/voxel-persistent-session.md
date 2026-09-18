# Persistent 3D presentation and mouse camera

Voxel mode is now a presentation **session**, not an idle-map-only effect.
Once enabled, the frontend never switches to the native 2D map projection.
Disabling voxel mode explicitly still restores the ordinary frontend.

## Scene audit

| Scene | Presentation |
| --- | --- |
| Idle map, ally hover, enemy inspection | Live 3D terrain and camera-facing unit sprites; native HUD |
| Selection, movement ranges, walking, action menus | Live 3D map; native action menu anchored near the action |
| Weapon lists and supported combat forecasts | Current native UI panels over the live 3D map, including multi-palette and sub-tile-scrolled windows |
| Attack / staff / other native target selection | Read-only native target-ring discovery; exact legal target clicks are routed through native Up/A input. Recognized panels detach; unusual effects use the intact native panel |
| Full combat, healing effects, HP/experience/level-up, dialogue, phase changes | Last verified 3D world with the **current, complete, animated native frame** in a panel; camera remains interactive |
| Unit info, minimap, custom menus / unsupported PPU effects | Same retained 3D world and intact live native panel |
| Armory inventory | Original desktop inventory layout and hit targets over 3D, with a translucent canvas background |
| Startup, reset or loaded state before a map validates | Asset-free neutral 3D stage plus the live native screen; no previous map leaks across a state load |

The battle panel preserves the ROM's original 2D battle animations. This does
**not** invent 3D battle animations, recreate combat logic, or simulate enemy
movement in the context scenery. During these scenes the retained battlefield
is context, not an authoritative animation of every actor. The native panel
continues to show the actual game state and receive ordinary game input.

## Camera and pointer behavior

Drag the unobscured world with the left or middle mouse button to pan. Drag
with the right mouse button to orbit horizontally and tilt vertically. Scroll
on the world to zoom; trackpad fractional wheel movement is retained.

A short left click selects/confirms; a short right click cancels. Selection and
cancellation are issued **on release**, only if the pointer never exceeded the
six-window-point drag threshold. Dragging out and returning to the start is
still a drag. The threshold is independent of Retina drawable scaling.

Dragging from inside a UI panel does not move the camera or accidentally
confirm. Scroll over a native menu/forecast panel to send ordinary Up/Down menu
or target input. Inventory retains its own drag/drop behavior. Existing camera
shortcuts remain optional: brackets orbit, C focuses the cursor, Home resets.

Capture, gesture, and pending target input are cleared on focus loss, resize,
state load, explicit setting changes, and mode exit. An in-flight click cannot
confirm an unrelated scene after a menu/scene transition. Actual target clicks
revalidate the live target list before every input pulse and confirm only after
the requested target becomes current; invalidated/frozen/fog-hidden targets or
unrecognized ROM layouts never receive guessed cyclic input.

## Architecture and safety

`voxel_presentation.h` remains the strict *live world refresh* gate. Its non-ready
states no longer select a full-window 2D renderer. `voxel_stage` owns copies of
verified EWRAM, IWRAM, VRAM, palettes, OAM, IO, map configuration, and the snapshot.
It references only the original immutable ROM block. No live RAM fallback is
used while menus/battles repurpose the PPU. Rendering reprojects that saved
world data through the same camera; it is not a frozen screenshot.

Detached details retain the 98% PPU/frame match requirement. Hardware windows,
affine/mosaic effects, HBlank tricks and unverified UI use a complete live native
panel rather than deleting unknown pixels or relaxing the oracle. Target-list
discovery validates the native proc script, callbacks, owner, cyclic links,
coordinates, fog and current node before routing inputs. It never writes the
cursor, target index, damage, animation state or RNG.

The retained input copy uses bounded memcpy operations. It reuses the live ROM
checksum rather than hashing a 32 MiB cartridge every frame. Scenery/texture
caches and GPU rendering remain. Map growth also fixes a pre-existing duplicate
free / dangling mesh-buffer bug: terrain allocation no longer frees GPU meshes;
geometry rebuilds reuse capacity and advance resource revisions.

## Validation

`test_voxel_stage` covers a neutral startup, verified retention, repurposed live
memory, camera changes, resize, larger maps, backend changes and reset. It also
checks every native-panel pixel across successive frame updates and bounds its
screen size so the world remains visible. `test_camera_gesture` covers the click
threshold, complete drag histories, right-click versus orbit, UI ownership,
coalesced/far releases, cancellation and invalid coordinates.

`test_voxel_targets` covers target discovery, native next/confirm sequencing,
wrong owners, changed targets, frozen selection, fog, malformed lists and timeouts.
`test_native_hud` additionally checks multi-palette/scrolled forecast panels,
brightness effects, immutability and rejection of mismatched frames.

Optional `test_voxel_session_rom` tests boot each supplied ROM with native input,
walk to an enemy, choose a weapon, show/cancel/reopen its forecast, target and
fight, open unit info/minimap, and end the turn. Every presentation frame must
produce 3D geometry and preserve the current native UI; emulator-state equality
is sampled around rendering. The Archanae test adds an **explicit disposable
healing fixture** (a Heal staff/proficiency and an injured adjacent ally) because
the opening roster has no healer. All subsequent healing menus, targets, effects
and HP changes are produced by the original game. That setup is test-only and
never changes the ROM or a user's save file.

Actual Linux client captures additionally exercise X11 left-drag pan,
right-drag orbit, release-only forecast confirmation, camera movement during
combat, and the inventory overlay. macOS validation is a native build and
ROM-independent tests, not a claim of M1 gameplay or frame-rate measurements.

```sh
cmake -S . -B build -DFE8_BUILD_TESTS=ON \
  -DFE8_ARCHANAE_ROM="/path/to/Archanae.gba" \
  -DFE8_SACRED_ECHOES_ROM="/path/to/Sacred Echoes.gba"
cmake --build build
ctest --test-dir build --output-on-failure
```

ROMs, emulator states and decoded game assets are not distributed with the code.
