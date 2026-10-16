$ErrorActionPreference = 'Stop'
$networkProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$networkProjectRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot = "$networkProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$mingwRoot = "$networkProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot = "$networkProjectRoot/tmp/build-network-links-test"
$cmake = "$networkProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/cmake.exe"
$ctest = "$networkProjectRoot/tmp/ohos-sdk/native/build-tools/cmake/bin/ctest.exe"
$env:PATH = "$mingwRoot;$qtRoot/bin;" + $env:PATH

& $cmake -S $PSScriptRoot -B $buildRoot -G 'MinGW Makefiles' `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$mingwRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$mingwRoot/mingw32-make.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildRoot --parallel 1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($name in @('result-default.xml', 'result-harmony.xml', 'result.xml', 'variant-counts.json')) {
    $path = Join-Path $buildRoot $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
& $ctest --test-dir $buildRoot --parallel 1 --output-on-failure `
    --output-junit (Join-Path $buildRoot 'test-results.xml')
$networkExitCode = $LASTEXITCODE
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
