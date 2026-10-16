param([string]$MavlinkIncludeRoot)
$ErrorActionPreference = 'Stop'
$sensorRepository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$sensorRepository/deploy/harmonyos/deveco-env.ps1"
$sensorQt = "$sensorRepository/tmp/toolchains/Qt/6.12.0/mingw_64"
$sensorCompiler = "$sensorRepository/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$sensorBuild = "$sensorRepository/tmp/build-sensor-calibration-test"
$sensorCmake = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin"
$env:PATH = "$sensorQt/bin;$sensorCompiler;" + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$sensorExtraArguments = @()
if ($MavlinkIncludeRoot) { $sensorExtraArguments += "-DMAVLINK_INCLUDE_ROOT=$MavlinkIncludeRoot" }
& "$sensorCmake/cmake.exe" -S $PSScriptRoot -B $sensorBuild -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$sensorQt/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$sensorCompiler/g++.exe" "-DCMAKE_MAKE_PROGRAM=$sensorCmake/ninja.exe" @sensorExtraArguments
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$sensorCmake/cmake.exe" --build $sensorBuild --parallel 1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$sensorCmake/ctest.exe" --test-dir $sensorBuild --parallel 1 --output-on-failure `
    --output-junit "$sensorBuild/test-results.xml"
$sensorTestExit = $LASTEXITCODE
& "$sensorRepository/tmp/venv/Scripts/python.exe" -B "$PSScriptRoot/write-report.py" `
    --build-directory $sensorBuild --test-exit $sensorTestExit
if ($sensorTestExit) { exit $sensorTestExit }
exit $LASTEXITCODE
