param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$configurationProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$configurationProjectRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$configurationProjectRoot/tmp/toolchains/Qt/6.8.3/mingw_64"
$mingwRoot = "$configurationProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$configurationProjectRoot/tmp/build-harmony-bluetooth-configuration-test"
$cmake = "$configurationProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/cmake.exe"
$ctest = "$configurationProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/ctest.exe"
$env:PATH = "$qtRoot/bin;$mingwRoot;" + $env:PATH
& $cmake -S $PSScriptRoot -B $buildRoot -G 'MinGW Makefiles' `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$mingwRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$mingwRoot/mingw32-make.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildRoot --parallel 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Baseline) {
    & (Join-Path $buildRoot 'HarmonyBluetoothConfigurationTest.exe') `
        -o "$buildRoot/baseline.txt,txt" -o "$buildRoot/baseline.xml,junitxml"
} else {
    & $ctest --test-dir $buildRoot --parallel 1 --output-on-failure `
        --output-junit (Join-Path $buildRoot 'test-results.xml')
}
exit $LASTEXITCODE
