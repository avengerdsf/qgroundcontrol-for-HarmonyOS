param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$nmeaRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$nmeaRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$nmeaRoot/tmp/toolchains/Qt/6.8.3/mingw_64"
$compilerRoot = "$nmeaRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$nmeaRoot/tmp/build-nmea-serial-source-test"
$cmakeRoot = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin"
$env:PATH = "$qtRoot/bin;$compilerRoot;" + $env:PATH
$env:NODE_OPTIONS = '--max-old-space-size=256'
& "$cmakeRoot/cmake.exe" -S $PSScriptRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
    "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" "-DCMAKE_CXX_COMPILER=$compilerRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$cmakeRoot/ninja.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$cmakeRoot/cmake.exe" --build $buildRoot --parallel 1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Baseline) {
    & "$buildRoot/NmeaSerialSourceTest.exe" -o "$buildRoot/baseline.txt,txt" -o "$buildRoot/baseline.xml,junitxml"
} else {
    & "$cmakeRoot/ctest.exe" --test-dir $buildRoot --parallel 1 --output-on-failure --output-junit "$buildRoot/test-results.xml"
}
exit $LASTEXITCODE
