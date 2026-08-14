# FE8 macOS project plans

These documents preserve the three architectures considered for a native-feeling
macOS version of *Fire Emblem: The Sacred Stones*. They are alternatives, not
three phases of one implementation.

| Plan | FE8 execution model | ROM-hack reach | Difficulty |
| --- | --- | --- | --- |
| [1. Native recomp/source port](01-native-recomp-or-source-port.md) | Host-compiled arm64/x86-64 code | Low without extra compatibility work | Very high |
| [2. FE8-specific libmGBA frontend](02-libmgba-fe8-extended-frontend.md) | GBA code runs inside embedded mGBA | Mostly one known FE8U build | Medium-high |
| [3. ROM-hack-compatible libmGBA frontend](03-libmgba-romhack-compatible-frontend.md) | GBA code runs inside embedded mGBA | Profile-driven FE8U hacks | Medium-high, incremental |

The current prototype follows Plan 3. It keeps mGBA authoritative for game
execution and the original 240×160 image, then decodes FE8 map state read-only to
draw additional terrain around that image. Mouse targets are converted into
ordinary D-pad input so the game remains in control of cursor rules and events.
