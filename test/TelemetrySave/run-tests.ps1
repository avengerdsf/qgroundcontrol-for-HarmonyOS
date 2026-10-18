param(
    [string]$ToolchainRoot,
    [string]$QtRoot,
    [string]$BuildDirectory,
    [switch]$Baseline
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $ToolchainRoot) { $ToolchainRoot = $projectRoot }
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
if (-not $QtRoot) { $QtRoot = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/6.12.0/mingw_64' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'tmp/build-telemetry-save-test' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory, $projectRoot)
$python = Join-Path $ToolchainRoot 'tmp/venv/Scripts/python.exe'
$compiler = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$cmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$startedUtc = [DateTime]::UtcNow
& $python -B (Join-Path $projectRoot 'test/HarmonyBluetoothHost/run-host-tests.py') `
    --project-root $projectRoot --source $PSScriptRoot --build $buildRoot `
    --qt-root $QtRoot --compiler-dir $compiler --cmake-dir $cmake `
    --executable TelemetrySaveTest.exe `
    "--cmake-arg=-DTELEMETRY_BACKUP_DIR=$(Join-Path $ToolchainRoot 'tmp/verification/upload-pad-telemetry-backup')"
$testExitCode = $LASTEXITCODE
if ($Baseline -and $testExitCode -eq 0) {
    foreach ($extension in @('txt', 'xml')) {
        $report = Join-Path $buildRoot "result.$extension"
        if ((Test-Path -LiteralPath $report) -and (Get-Item -LiteralPath $report).LastWriteTimeUtc -ge $startedUtc) {
            Copy-Item -LiteralPath $report -Destination (Join-Path $buildRoot "baseline.$extension") -Force
        }
    }
}
exit $testExitCode
