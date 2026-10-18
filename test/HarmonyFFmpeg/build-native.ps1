param(
    [ValidateSet('arm64-v8a', 'x86_64')][string]$Architecture = 'x86_64',
    [Parameter(Mandatory)][string]$ToolchainRoot
)
$ErrorActionPreference = 'Stop'
$ffmpegProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $ffmpegProjectRoot 'deploy/harmonyos/deveco-env.ps1')
$ffmpegPython = Join-Path $ToolchainRoot 'tmp/vcpkg-downloads/tools/python/python-3.14.2-x64-1/python.exe'
$ffmpegCmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin/cmake.exe'
$ffmpegQtName = if ($Architecture -eq 'arm64-v8a') { 'harmonyos_arm64_v8a' } else { 'harmonyos_x86_64' }
$ffmpegPackageName = if ($Architecture -eq 'arm64-v8a') { 'arm64' } else { 'x86_64' }
$ffmpegQtTarget = Join-Path $ToolchainRoot "tmp/toolchains/Qt/6.12.0/$ffmpegQtName"
$ffmpegQtHost = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/6.12.0/mingw_64'
$ffmpegRoot = Join-Path $ToolchainRoot "tmp/ffmpeg/9.0.1/$ffmpegPackageName/install"
& $ffmpegPython -B (Join-Path $PSScriptRoot 'compile-native.py') `
    --source $PSScriptRoot --project-root $ffmpegProjectRoot --qt-target $ffmpegQtTarget `
    --qt-host $ffmpegQtHost --ffmpeg-root $ffmpegRoot --cmake $ffmpegCmake `
    --sdk-root $env:OHOS_SDK_ROOT --arch $Architecture
exit $LASTEXITCODE
