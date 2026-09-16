# Windows builds

The Windows x64 version uses the SDL2 game window and FreeType inventory UI.
The native macOS game library, settings menus, and OpenGL shader presets are
macOS-only. Supply a ROM path when launching on Windows.

## Build from a Mac with GitHub Actions

Push these files to your fork on GitHub. The **Windows build** workflow runs
automatically on pushes and pull requests. It compiles and runs the unit tests
on a Windows runner; your Mac needs no Windows SDK or cross-compiler.

1. Open your fork's **Actions** tab and select **Windows build**.
2. Open the completed run, or choose **Run workflow** to start another build
   (GitHub shows this button after the workflow reaches the default branch).
3. Download **fe8-ui-windows-x64** from the run's **Artifacts** section.
4. Extract the ZIP on Windows and launch the executable as shown below.

The same workflow can be started from a Mac with GitHub CLI:

```sh
gh workflow run windows.yml --ref main
gh run list --workflow windows.yml
gh run watch RUN_ID
gh run download RUN_ID --name fe8-ui-windows-x64 --dir fe8-ui-windows
```

Replace `RUN_ID` with the run ID printed by `gh run list`. This is a remote
Windows build, so it needs GitHub access; it does not compile locally on macOS.

## Build locally on Windows

Install Visual Studio 2022 with **Desktop development with C++**, CMake, Git,
and an up-to-date vcpkg. From PowerShell:

```powershell
./scripts/build-windows.ps1
# For a standalone vcpkg installation:
./scripts/build-windows.ps1 -VcpkgRoot C:/src/vcpkg
```

If the Visual Studio bundled vcpkg reports missing downloads, install a current
standalone copy and pass its path:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:/src/vcpkg
C:/src/vcpkg/bootstrap-vcpkg.bat -disableMetrics
./scripts/build-windows.ps1 -VcpkgRoot C:/src/vcpkg
```

The script initializes only the pinned mGBA submodule, installs the pinned
SDL2/zlib/FreeType dependencies, builds with MSVC, runs CTest, and stages the
package under `build-windows/dist` and creates `build-windows/fe8-ui-windows-x64.zip`.
It uses static dependencies and the static
MSVC runtime so the executable does not need separate SDL or Visual C++ runtime
DLLs. First builds require network access and take longer than rebuilds.

## Run

In PowerShell, from the extracted package directory:

```powershell
./bin/fe8-mgba-sdl.exe --rom 'C:/Games/fireemblem8.gba' --save 'C:/Games/fireemblem8.sav' --quick-state 'C:/Games/fireemblem8.ss'
```

Use your own ROM. Give each game its own save and quick-state paths. Press
**I** for Armory, **F5/F8** to save/load the configured quick state, and **F6**
to toggle extended rendering. Add `--mute` to disable audio. Run with `--help`
for the complete command-line options.
