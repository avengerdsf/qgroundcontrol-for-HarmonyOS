$ErrorActionPreference = 'Stop'
$audioProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace([char]92, [char]47)
. "$audioProjectRoot/deploy/harmonyos/deveco-env.ps1"
$audioQtRoot = "$audioProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$audioCompilerRoot = "$audioProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$audioBuildRoot = "$audioProjectRoot/tmp/build-harmony-startup-test"
$audioCmake = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/cmake.exe"
$audioNinja = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/ninja.exe"
$env:PATH = "$audioQtRoot/bin;$audioCompilerRoot;" + $env:PATH
& $audioCmake -S $PSScriptRoot -B $audioBuildRoot -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$audioQtRoot/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$audioCompilerRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$audioNinja"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $audioCmake --build $audioBuildRoot --parallel 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/ctest.exe" --test-dir $audioBuildRoot --parallel 1 --output-on-failure
$audioTestExit = $LASTEXITCODE
Get-Content -LiteralPath "$audioBuildRoot/audio-startup-results.txt"
Get-Content -LiteralPath "$audioBuildRoot/audio-synchronous-results.txt"
exit $audioTestExit
