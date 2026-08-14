# Repository reorganization plan

> Status: implemented locally in the fresh `FE8-UI-reorg` superproject. The
> replacement history has not been pushed.

## Decision

Turn FE8 Extended Frontend into a small, independent **superproject** that owns
only the native frontend and its supporting material. Pin external source trees
as submodules:

- `third_party/mgba`: a required build dependency at the exact revision used by
  the application.
- `reference/fireemblem8u`: an optional, read-only development reference. The
  application must build, test, and package without it.

This preserves the current design: FE8 and compatible ROM hacks execute through
libmGBA, while our code supplies the native library UI, extended renderer,
mouse controls, settings, saves, and save-state management. The FE8 decomp is
not compiled into the application.

## What exists today

The current Git repository began as a checkout of
`FireEmblemUniverse/fireemblem8u`, so almost all of its tracked files belong to
the decomp. Our work is comparatively self-contained:

| Component | Current location | Runtime/build role | Current ownership |
| --- | --- | --- | --- |
| Native frontend | `prototype/src/` | Compiled into the macOS application | Ours |
| Frontend tests | `prototype/tests/` | Tests our renderer, profiles, input, settings, and scaling | Ours |
| Developer tools | `prototype/tools/` | Inspects mGBA states and helps profile ROM hacks | Ours |
| Build definition | `prototype/CMakeLists.txt` | Fetches and links static libmGBA | Ours |
| Product documentation | `prototype/README.md`, selected `docs/`, root project files | Usage, testing, plans, notices, and roadmap | Ours |
| mGBA | Downloaded by CMake into `build/prototype/_deps/` | Required; compiled and statically linked | Upstream dependency |
| FE8 decomp | Most of the current repository | Reference for FE8 layouts and behavior | Upstream reference |
| ROMs, saves, and states | User-selected files and Application Support | Runtime data; never source dependencies | User-supplied |

The frontend currently pins mGBA commit
`afd6f14eaf8bd35214ed3fb9dc69a92bfc3877a9`. Searches of its CMake, sources,
and tests show no include or source reference into the surrounding FE8 decomp.

## Proposed repository layout

```text
FE8-UI/
├── CMakeLists.txt
├── app/
│   ├── src/
│   └── macos/                 # optional later split of Objective-C host code
├── tests/
├── tools/
├── assets/                    # app-owned icons, cursors, and shaders
├── cmake/
├── scripts/
│   ├── bootstrap.sh
│   ├── build.sh
│   └── test.sh
├── docs/
│   ├── architecture.md
│   ├── romhack-compatibility.md
│   ├── testing.md
│   └── plans/
├── third_party/
│   └── mgba/                  # required submodule
├── reference/
│   └── fireemblem8u/          # optional submodule
├── .gitmodules
├── .gitignore
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── README.md
└── TODO.md
```

`prototype/` should become `app/` because it is now the product. The first
migration should mostly preserve its internal file layout to keep the change
mechanical. Splitting platform-neutral code from `app/macos/` can happen later
as an ordinary refactor.

## What the new repository should commit

Commit as normal files:

- All original frontend C and Objective-C sources and headers.
- Frontend tests and ROM/state inspection tools.
- Root CMake configuration and small bootstrap/build/test scripts.
- App-owned visual assets and the shader copies already covered by their
  notices.
- Architecture, testing, compatibility, and design-plan documentation.
- `README.md`, `TODO.md`, the MIT `LICENSE`, and
  `THIRD_PARTY_NOTICES.md`.
- `.gitmodules`, which records the URLs, paths, and pinned commits for both
  external repositories.

Commit only a Git submodule pointer, not copied upstream files, for:

- mGBA under `third_party/mgba`.
- `fireemblem8u` under `reference/fireemblem8u`.

Do not commit:

- ROM images, BIOS files, Nintendo assets, or extracted game data.
- User saves, save states, screenshots, preferences, or game-library records.
- `.app` bundles, object files, CMake build directories, downloaded packages,
  Python caches, or local IDE state.
- Modified copies of decomp files. Any knowledge learned from the decomp should
  be expressed in our own profile/compatibility code and documented with a
  source revision and symbol/address reference.

## Submodule policy

### mGBA: required build dependency

mGBA should be a submodule because we compile against an exact source revision
and currently consume one of its OpenGL source files in addition to the
libmGBA target. The root build should use `add_subdirectory(third_party/mgba)`
instead of downloading a second copy with `FetchContent`.

Rules:

- Pin the current known-good revision initially.
- Keep local mGBA modifications at zero where possible.
- If an mGBA patch becomes unavoidable, carry it on a clearly named branch in
  a separate mGBA fork and document why; do not leave an unexplained dirty
  submodule.
- CI and release builds always initialize this submodule.

### fireemblem8u: optional reference dependency

The decomp is useful enough to pin for repeatable research, but it is not a
build input. Making it optional avoids turning a large, unlicensed reference
corpus into an apparent part of the product.

Rules:

- Normal configure/build/test operations cannot read from this directory.
- Bootstrap initializes it only when explicitly requested, for example
  `./scripts/bootstrap.sh --with-reference`.
- It is never copied into an application bundle or release archive.
- Documentation cites the pinned revision when an address or structure was
  derived from it.

An even leaner alternative is to omit this submodule and record only an
upstream URL and revision in the developer documentation. The optional
submodule is preferable here because it makes ROM-hack profiling and future
reverse-engineering work reproducible, while preserving the build boundary.

## Build and clone experience

The desired default flow is:

```sh
git clone --recurse-submodules <FE8-UI repository>
cd FE8-UI
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Because a recursive clone also downloads the optional reference, the README
should additionally provide the smaller product-only flow:

```sh
git clone <FE8-UI repository>
cd FE8-UI
git submodule update --init third_party/mgba
cmake -S . -B build
cmake --build build
```

The CMake configure step should fail early with a useful bootstrap command if
the mGBA submodule is absent. It should completely ignore an absent decomp
submodule.

## Licensing boundary

- Our original frontend remains MIT licensed.
- mGBA remains MPL-2.0. The superproject does not relicense it, and binary
  distributions must retain the applicable notice and provide the required
  route to the corresponding mGBA source.
- SDL2, zlib, and the adapted shader sources retain their existing licenses and
  notices.
- `fireemblem8u` does not provide a general-purpose software license. Its
  submodule remains a separately fetched reference at an upstream revision;
  its contents are not covered by our MIT license or included in releases.
- ROMs and game assets are not distributed. Users continue to supply their own
  legally obtained ROMs.

`THIRD_PARTY_NOTICES.md` should say this explicitly and identify both submodule
paths and pinned upstream projects.

## Safe migration sequence

History replacement should be the last step, after the clean repository proves
it can replace the existing one.

1. **Freeze and identify the source state.** Record the current `main` commit,
   the mGBA revision, the FE8 decomp revision, and the branches containing any
   work that must survive.
2. **Create a separate sibling repository.** Use a fresh directory such as
   `~/programming/vibe/FE8-UI-reorg`; do not use an orphan branch or mutate the
   working repository in place.
3. **Copy only our files.** Move the frontend, tests, tools, product docs, and
   metadata into the proposed layout. Exclude build products and caches.
4. **Add pinned submodules.** Add mGBA as required and `fireemblem8u` as
   optional. Replace `FetchContent` with the local mGBA submodule target.
5. **Make builds location-independent.** Ensure every source and asset path is
   relative to the new superproject, with no dependency on either old checkout.
6. **Validate from a clean clone.** Run configure, build, and tests with only
   mGBA initialized. Repeat with both submodules initialized.
7. **Perform runtime regression testing.** Launch clean FE8 plus Sacred Echoes,
   Archanea, and Pokémblem; verify extended rendering, camera transitions,
   audio, shaders, zoom, mouse input, saves, and per-game save states.
8. **Verify user-data compatibility.** Confirm the new bundle identifier and
   Application Support paths preserve the existing game library, settings,
   saves, and states, or implement an explicit migration before release.
9. **Create one new root commit.** Once validated, create the fresh history with
   a single commit named `initial`.
10. **Preserve the old history.** Tag and push the current tip under a dated
    legacy name, and retain the old repository directory until the new clone is
    independently verified.
11. **Replace remote `main` deliberately.** After explicit approval, push the
    new history using `--force-with-lease`, never an unchecked force push.
12. **Clean up later.** Delete obsolete branches or the old local repository
    only after the replacement has been used successfully and all desired work
    is reachable from a tag or remote branch.

## Acceptance criteria before replacing history

- A clean checkout builds without the FE8 decomp present.
- The build uses exactly the pinned mGBA submodule and performs no hidden
  network fetches after bootstrap.
- All frontend unit tests pass.
- The four representative ROM configurations pass the runtime smoke test.
- Existing user library metadata, saves, save states, settings, and hotkeys are
  preserved.
- `git status` is clean after build/test because generated files are ignored.
- The application and release archive contain no ROM, decomp, or unintended
  third-party source files.
- The old history is backed up remotely before `main` is replaced.

## Current checkpoint

The Pokémblem work is present in the legacy repository as the single squash
commit `155b55ef` (`support Pokemblem extended rendering`), immediately
following `a96061a4`. The complete migration source checkpoint is `e3074007`,
which adds this plan on top of that squash. It is protected locally by the
annotated tag `legacy/pre-reorganization-2026-08-14` in the old repository.
