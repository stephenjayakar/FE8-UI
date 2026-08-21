# ROM library validation and recovery

The macOS library treats a ROM path as a reference to a user-owned file. It does
not copy the ROM into application storage. Before an import is accepted, the
frontend now streams the file once to calculate its SHA-1 and validate the
standard Game Boy Advance header.

## Validation policy

The importer rejects files that are too small to contain a GBA header, have an
invalid Nintendo logo block, or do not contain the required `0x96` fixed byte.
Those failures strongly indicate that the selected file is not a usable GBA ROM.

A mismatched header complement, an empty or non-printable title/game code, or a
size above the standard 32 MiB address space is reported as a warning instead of
a hard failure. ROM hacks occasionally ship with stale non-critical metadata;
the library keeps those files usable while making the discrepancy visible.
Warnings remain attached to the library row and are shown in its tooltip.

The SHA-1 remains the storage identity. Re-importing the same bytes updates the
existing row, while a modified ROM receives a separate save/state directory.
The compatibility label is derived from the header game code:

- `BE8E`: FE8U-compatible; validated FE8 extensions may activate at runtime.
- Other `BE8` codes: FE8-family; extension availability depends on the profile.
- Other valid GBA codes: standard mGBA rendering without FE8-specific promises.

Runtime structure validation remains authoritative. A compatibility label never
bypasses the renderer's per-frame pointer and layout checks.

## Missing or moved ROMs

A missing or unreadable path is shown directly in the Progress column. The
**Show ROM** button becomes **Locate ROM…**. A replacement path is accepted only
when its SHA-1 exactly matches the original library entry, preventing an
accidental switch to another revision while retaining the same save directory.
Use **Add ROMs…** for a genuinely different build.

## Launch diagnostics

Only one process can be launched for a library entry at a time. While it runs,
Play, Resume, and Remove are disabled and the row shows **Running**.

Each launch writes stdout and stderr to:

```text
~/Library/Application Support/FE8 Extended Frontend/Games/<ROM SHA-1>/last-run.log
```

The file is replaced on the next launch. If the child process exits with a
non-zero status, the library presents the tail of that log and leaves the full
file beside the game's save and quick state.

## Standalone inspector

The metadata reader has no SDL2 or mGBA dependency. Build only the inspector
when diagnosing an import or validating ROM-hack release files:

```sh
cmake -S . -B build-tools \
  -DFE8_BUILD_FRONTEND=OFF \
  -DFE8_BUILD_TESTS=OFF \
  -DFE8_BUILD_TOOLS=ON
cmake --build build-tools --target fe8-rom-inspect
```

Human-readable output:

```sh
build-tools/fe8-rom-inspect /path/to/game.gba
```

Machine-readable output:

```sh
build-tools/fe8-rom-inspect --json /path/to/game.gba
```

Warnings normally produce a successful exit status because the library permits
them. Add `--strict` for CI or release checks where any warning should fail.
Multiple ROM paths may be inspected in one invocation.
