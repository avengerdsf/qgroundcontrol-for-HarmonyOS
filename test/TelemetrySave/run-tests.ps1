param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$telemetryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$telemetryRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$telemetryRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$compilerRoot = "$telemetryRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$telemetryRoot/tmp/build-telemetry-save-test"
$cmakeRoot = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin"
$env:PATH = "$qtRoot/bin;$compilerRoot;" + $env:PATH
$env:NODE_OPTIONS = '--max-old-space-size=256'
$env:_JAVA_OPTIONS = '-Xmx768m'
& "$cmakeRoot/cmake.exe" -S $PSScriptRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
    "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" "-DCMAKE_CXX_COMPILER=$compilerRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$cmakeRoot/ninja.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$cmakeRoot/cmake.exe" --build $buildRoot --parallel 1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Baseline) {
    & "$buildRoot/TelemetrySaveTest.exe" -o "$buildRoot/baseline.txt,txt" -o "$buildRoot/baseline.xml,junitxml"
} else {
    & "$cmakeRoot/ctest.exe" --test-dir $buildRoot --parallel 1 --output-on-failure --output-junit "$buildRoot/test-results.xml"
}
exit $LASTEXITCODE
