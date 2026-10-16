param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$protocolProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$protocolProjectRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$protocolProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$mingwRoot = "$protocolProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$protocolProjectRoot/tmp/build-bootloader-protocol-test"
$cmake = "$protocolProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/cmake.exe"
$ctest = "$protocolProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/ctest.exe"
$env:PATH = "$qtRoot/bin;$mingwRoot;" + $env:PATH
& $cmake -S $PSScriptRoot -B $buildRoot -G 'MinGW Makefiles' '-DCMAKE_BUILD_TYPE=Debug' `
    "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" "-DCMAKE_CXX_COMPILER=$mingwRoot/g++.exe" `
    "-DCMAKE_MAKE_PROGRAM=$mingwRoot/mingw32-make.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildRoot --parallel 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Baseline) {
    & (Join-Path $buildRoot 'BootloaderProtocolTest.exe') `
        -o "$buildRoot/baseline.txt,txt" -o "$buildRoot/baseline.xml,junitxml"
} else {
    & $ctest --test-dir $buildRoot --parallel 1 --output-on-failure `
        --output-junit (Join-Path $buildRoot 'test-results.xml')
}
exit $LASTEXITCODE

