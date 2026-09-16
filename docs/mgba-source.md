# mGBA source for this build

This executable statically links mGBA, licensed under MPL-2.0. The complete
upstream source for the pinned revision is available at:

https://github.com/mgba-emu/mgba/tree/afd6f14eaf8bd35214ed3fb9dc69a92bfc3877a9

The build changes two accesses to `state->hw.unlCartFlags` in
`src/gba/cart/unlicensed.c` from 32-bit to 16-bit accesses. The complete modified
file is included beside this notice under `modified-source/src/gba/cart/`.
All other mGBA files are unchanged. The repository's
`cmake/MgbaStateCompatibility.cmake` applies this correction during configuration.
