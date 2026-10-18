param(
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$ToolchainRoot,
    [Parameter(Mandatory)][string]$FFmpegRoot
)
$ErrorActionPreference = 'Stop'
$ffmpegProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $ffmpegProjectRoot 'deploy/harmonyos/deveco-env.ps1')
$ffmpegPython = Join-Path $ToolchainRoot 'tmp/vcpkg-downloads/tools/python/python-3.14.2-x64-1/python.exe'
$ffmpegCmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin/cmake.exe'
$ffmpegCompiler = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin/g++.exe'
& $ffmpegPython -B (Join-Path $PSScriptRoot 'compile-interface.py') `
    --source $PSScriptRoot --project-root $ffmpegProjectRoot --qt-root $QtRoot `
    --ffmpeg-root $FFmpegRoot --cmake $ffmpegCmake --compiler $ffmpegCompiler
exit $LASTEXITCODE
