$ErrorActionPreference = 'Stop'
$bluetoothHostProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$bluetoothHostProjectRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$bluetoothHostProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$mingwRoot = "$bluetoothHostProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$bluetoothHostProjectRoot/tmp/build-bluetooth-host-test"
$cmake = "$bluetoothHostProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/cmake.exe"
$ctest = "$bluetoothHostProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/ctest.exe"
$env:PATH = "$qtRoot/bin;$mingwRoot;" + $env:PATH
& $cmake -S $PSScriptRoot -B $buildRoot -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$mingwRoot/g++.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildRoot --parallel 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ctest --test-dir $buildRoot --parallel 1 --output-on-failure `
    --output-junit (Join-Path $buildRoot 'test-results.xml')
exit $LASTEXITCODE
