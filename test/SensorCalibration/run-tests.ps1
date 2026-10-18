param(
    [string]$ToolchainRoot,
    [string]$QtRoot,
    [string]$BuildDirectory,
    [string]$MavlinkIncludeRoot
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $ToolchainRoot) { $ToolchainRoot = $projectRoot }
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
if (-not $QtRoot) { $QtRoot = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/6.12.0/mingw_64' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'tmp/build-sensor-calibration-test' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory, $projectRoot)
$python = Join-Path $ToolchainRoot 'tmp/venv/Scripts/python.exe'
$compiler = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$cmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$startedUtc = [DateTime]::UtcNow
$extraArguments = @()
if ($MavlinkIncludeRoot) { $extraArguments += "--cmake-arg=-DMAVLINK_INCLUDE_ROOT=$MavlinkIncludeRoot" }
& $python -B (Join-Path $projectRoot 'test/HarmonyBluetoothHost/run-host-tests.py') `
    --project-root $projectRoot --source $PSScriptRoot --build $buildRoot `
    --qt-root $QtRoot --compiler-dir $compiler --cmake-dir $cmake `
    --executable SensorCalibrationTest.exe @extraArguments
$testExitCode = $LASTEXITCODE
$resultPath = Join-Path $buildRoot 'result.xml'
if (-not (Test-Path -LiteralPath $resultPath) -or (Get-Item -LiteralPath $resultPath).LastWriteTimeUtc -lt $startedUtc) {
    exit $(if ($testExitCode) { $testExitCode } else { 1 })
}
& $python -B (Join-Path $PSScriptRoot 'write-report.py') --build-directory $buildRoot --test-exit $testExitCode
if ($LASTEXITCODE -and -not $testExitCode) { $testExitCode = $LASTEXITCODE }
exit $testExitCode
