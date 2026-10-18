param(
    [string]$ToolchainRoot,
    [string]$QtRoot,
    [string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $ToolchainRoot) { $ToolchainRoot = $projectRoot }
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
if (-not $QtRoot) { $QtRoot = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/6.12.0/mingw_64' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'tmp/build-network-links-test' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory, $projectRoot)
$python = Join-Path $ToolchainRoot 'tmp/venv/Scripts/python.exe'
$compiler = Join-Path $ToolchainRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$cmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin'
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
$startedUtc = [DateTime]::UtcNow
& $python -B (Join-Path $projectRoot 'test/HarmonyBluetoothHost/run-host-tests.py') `
    --project-root $projectRoot --source $PSScriptRoot --build $buildRoot `
    --qt-root $QtRoot --compiler-dir $compiler --cmake-dir $cmake `
    --executable NetworkLinksTest.exe
$testExitCode = $LASTEXITCODE
$freshReports = @('result-default.xml', 'result-harmony.xml') | Where-Object {
    $path = Join-Path $buildRoot $_
    (Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $startedUtc
}
if ($freshReports.Count -ne 2) { exit $(if ($testExitCode) { $testExitCode } else { 1 }) }
$networkExitCode = $testExitCode
[xml]$combined = '<testsuites />'
$variants = @()
foreach ($variant in @('default', 'harmony')) {
    $path = Join-Path $buildRoot "result-$variant.xml"
    [xml]$report = Get-Content -LiteralPath $path -Raw
    $cases = @($report.SelectNodes('//testcase'))
    $failed = @($cases | Where-Object { $_.SelectSingleNode('failure|error') }).Count
    $skipped = @($cases | Where-Object { $_.SelectSingleNode('skipped') }).Count
    $lifecycle = @($cases | Where-Object { $_.name -match '(^|::)(initTestCase|cleanupTestCase)(\(|$)' }).Count
    $variants += [pscustomobject]@{
        variant = $variant; report = $path; tests = $cases.Count
        passed = $cases.Count - $failed - $skipped; failed = $failed; skipped = $skipped
        lifecycleInvocations = $lifecycle; functionalInvocations = $cases.Count - $lifecycle
    }
    $suite = $combined.ImportNode($report.DocumentElement, $true)
    $suite.SetAttribute('name', "NetworkLinksTest.$variant")
    [void]$combined.DocumentElement.AppendChild($suite)
}
$combined.Save((Join-Path $buildRoot 'result.xml'))
$variants | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $buildRoot 'variant-counts.json') -Encoding utf8
exit $networkExitCode
