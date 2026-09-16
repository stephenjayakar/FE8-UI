[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$BuildDirectory = 'build-windows',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

if (-not $VcpkgRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) { $VcpkgRoot = Join-Path $vs 'VC/vcpkg' }
    }
}
if (-not $VcpkgRoot -or -not (Test-Path "$VcpkgRoot/scripts/buildsystems/vcpkg.cmake")) {
    throw 'Install vcpkg, then pass -VcpkgRoot C:/path/to/vcpkg (or set VCPKG_ROOT).'
}
if (-not (Test-Path "$repo/third_party/mgba/CMakeLists.txt")) {
    Invoke-Checked git @('-C', $repo, 'submodule', 'update', '--init', '--depth', '1', 'third_party/mgba')
}
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo $BuildDirectory
}

Invoke-Checked cmake @('-S', $repo, '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-UZ_VCPKG_ROOT_DIR',
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>',
    '-DFE8_BUILD_TESTS=ON')
Invoke-Checked cmake @('--build', $BuildDirectory, '--config', $Configuration, '--parallel')
Invoke-Checked ctest @('--test-dir', $BuildDirectory, '-C', $Configuration, '--output-on-failure')
Invoke-Checked cmake @('--install', $BuildDirectory, '--config', $Configuration,
    '--prefix', "$BuildDirectory/dist")
Compress-Archive -Path "$BuildDirectory/dist/*" -DestinationPath "$BuildDirectory/fe8-ui-windows-x64.zip" -Force
Write-Host "Built and tested: $BuildDirectory/dist/bin/fe8-mgba-sdl.exe"
Write-Host "Package: $BuildDirectory/fe8-ui-windows-x64.zip"
