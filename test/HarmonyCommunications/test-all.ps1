param([switch]$IncludeUsbRegression)

$ErrorActionPreference = 'Stop'
$communicationsProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
. "$communicationsProjectRoot/deploy/harmonyos/deveco-env.ps1"
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$env:CTEST_PARALLEL_LEVEL = '1'
$communicationsShell = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
$communicationsNode = "$env:NODE_HOME/node.exe"
$communicationsRunRoot = "$communicationsProjectRoot/tmp/verification/harmony-communications/run-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
New-Item -ItemType Directory -Path $communicationsRunRoot -Force | Out-Null
$communicationsReportPath = "$communicationsRunRoot/report.json"

function New-Counts([int]$Passed, [int]$Failed, [int]$Skipped, [int]$Lifecycle, [string]$Source) {
    [pscustomobject]@{
        total = $Passed + $Failed + $Skipped
        passed = $Passed
        failed = $Failed
        skipped = $Skipped
        lifecycleInvocations = $Lifecycle
        functionalInvocations = $Passed + $Failed + $Skipped - $Lifecycle
        source = $Source
    }
}

function Read-QtCounts([string[]]$Paths, [string]$Output) {
    foreach ($path in $Paths) {
        if ([IO.Path]::GetFileName($path) -ne 'result.xml') { continue }
        try {
            [xml]$xml = Get-Content -LiteralPath $path -Raw
            $cases = @($xml.SelectNodes('//testcase'))
            if ($cases.Count -eq 0) { continue }
            $failed = @($cases | Where-Object { $_.SelectSingleNode('failure|error') }).Count
            $skipped = @($cases | Where-Object { $_.SelectSingleNode('skipped') }).Count
            $lifecycle = @($cases | Where-Object { $_.name -match '(^|::)(initTestCase|cleanupTestCase)(\(|$)' }).Count
            return New-Counts ($cases.Count - $failed - $skipped) $failed $skipped $lifecycle $path
        } catch {
            # An interrupted test may leave partial XML; try its text report.
        }
    }
    foreach ($path in @($Paths) + @('process-output')) {
        if ([string]::IsNullOrEmpty($path)) { continue }
        $content = if ($path -eq 'process-output') { $Output } else { Get-Content -LiteralPath $path -Raw }
        if ($path.EndsWith('.xml')) {
            try { [xml]$xml = $content; $content = $xml.InnerText } catch { continue }
        }
        $totals = [regex]::Matches($content, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted')
        if ($totals.Count -eq 0) { continue }
        $passed = 0; $failed = 0; $skipped = 0
        foreach ($total in $totals) {
            $passed += [int]$total.Groups[1].Value
            $failed += [int]$total.Groups[2].Value
            $skipped += [int]$total.Groups[3].Value + [int]$total.Groups[4].Value
        }
        $lifecycle = [regex]::Matches($content, '(?m)^\s*(?:PASS|FAIL!|SKIP)\s*:\s*[^\r\n]*::(?:initTestCase|cleanupTestCase)\(').Count
        return New-Counts $passed $failed $skipped $lifecycle $path
    }
    return $null
}

function Read-NodeCounts([string]$Output) {
    $content = [regex]::Replace($Output, '\x1b\[[0-?]*[ -/]*[@-~]', '')
    $passed = 0; $failed = 0; $skipped = 0; $total = 0
    $cancelled = 0; $todo = 0; $found = $false
    foreach ($summary in [regex]::Matches($content, '(?m)^\s*(\d+) passed,\s*(\d+) failed\s*$')) {
        $passed += [int]$summary.Groups[1].Value
        $failed += [int]$summary.Groups[2].Value
        $total += [int]$summary.Groups[1].Value + [int]$summary.Groups[2].Value
        $found = $true
    }
    foreach ($summary in [regex]::Matches($content, '(?m)^\s*[^:\r\n]+:\s*(\d+)\s*/\s*(\d+) tests passed\s*$')) {
        $pass = [int]$summary.Groups[1].Value
        $count = [int]$summary.Groups[2].Value
        if ($pass -gt $count) { throw 'Invalid passed/total test summary' }
        $passed += $pass
        $failed += $count - $pass
        $total += $count
        $found = $true
    }
    foreach ($summary in [regex]::Matches($content, '(?ms)^\s*[#ℹ]\s+tests\s+(?<total>\d+)\r?\n(?<body>.*?)(?=^\s*[#ℹ]\s+duration_ms\s|\z)')) {
        $body = $summary.Groups['body'].Value
        $pass = [regex]::Match($body, '(?m)^\s*[#ℹ]\s+pass\s+(\d+)\s*$')
        $fail = [regex]::Match($body, '(?m)^\s*[#ℹ]\s+fail\s+(\d+)\s*$')
        if (-not $pass.Success -or -not $fail.Success) { continue }
        $passed += [int]$pass.Groups[1].Value
        $failed += [int]$fail.Groups[1].Value
        $total += [int]$summary.Groups['total'].Value
        $skip = [regex]::Match($body, '(?m)^\s*[#ℹ]\s+skipped\s+(\d+)\s*$')
        if ($skip.Success) { $skipped += [int]$skip.Groups[1].Value }
        $cancel = [regex]::Match($body, '(?m)^\s*[#ℹ]\s+cancelled\s+(\d+)\s*$')
        $pending = [regex]::Match($body, '(?m)^\s*[#ℹ]\s+todo\s+(\d+)\s*$')
        if ($cancel.Success) { $cancelled += [int]$cancel.Groups[1].Value }
        if ($pending.Success) { $todo += [int]$pending.Groups[1].Value }
        $found = $true
    }
    if ($found) {
        $counts = New-Counts $passed $failed $skipped 0 'process-output'
        $counts.total = $total
        $counts.functionalInvocations = $total
        $counts | Add-Member -NotePropertyName cancelled -NotePropertyValue $cancelled
        $counts | Add-Member -NotePropertyName todo -NotePropertyValue $todo
        return $counts
    }
    return $null
}

function Read-SdkCounts([string]$Output) {
    $summaries = [regex]::Matches($Output, '(?m)^.*SDK[^\r\n]*:\s*(\d+) errors,\s*(\d+) warnings\s*$')
    if ($summaries.Count -eq 0) { return $null }
    $errors = 0; $warnings = 0
    foreach ($summary in $summaries) {
        $errors += [int]$summary.Groups[1].Value
        $warnings += [int]$summary.Groups[2].Value
    }
    return [pscustomobject]@{ checks = $summaries.Count; errors = $errors; warnings = $warnings }
}

function New-Step([string]$Id, [string]$Kind, [string]$Runner, [string]$Build = '') {
    $reports = @()
    if ($Build) {
        $base = "$communicationsProjectRoot/tmp/$Build"
        $reports = @("$base/result.xml", "$base/result.txt", "$base/Testing/Temporary/LastTest.log", "$base/test-results.xml")
    }
    [pscustomobject]@{
        id = $Id; kind = $Kind; runner = "$communicationsProjectRoot/$Runner"
        reportCandidates = $reports; status = 'not_run'; exitCode = $null
        startedUtc = $null; finishedUtc = $null; seconds = $null
        stdoutLog = "$communicationsRunRoot/$Id.stdout.log"
        stderrLog = "$communicationsRunRoot/$Id.stderr.log"
        sourceReports = @(); testCounts = $null; sdkCounts = $null; failure = $null
    }
}

$communicationsSteps = @(
    (New-Step 'network-links' 'qt' 'test/NetworkLinks/run-tests.ps1' 'build-network-links-test'),
    (New-Step 'network-configuration' 'qt' 'test/NetworkConfiguration/run-tests.ps1' 'build-network-configuration-test'),
    (New-Step 'bluetooth-host' 'qt' 'test/HarmonyBluetoothHost/run-tests.ps1' 'build-bluetooth-host-test'),
    (New-Step 'bluetooth-configuration' 'qt' 'test/HarmonyBluetoothConfiguration/run-tests.ps1' 'build-harmony-bluetooth-configuration-test'),
    (New-Step 'bluetooth-settings' 'qt' 'test/HarmonyBluetoothSettings/run-tests.ps1' 'build-harmony-bluetooth-settings-test'),
    (New-Step 'spp' 'node' 'test/HarmonyBluetooth/Spp-tests.cjs'),
    (New-Step 'spp-sdk' 'sdk' 'test/HarmonyBluetooth/spp-sdk-check.cjs'),
    (New-Step 'ble' 'node' 'test/HarmonyBluetooth/ble-tests.cjs'),
    (New-Step 'ble-sdk' 'sdk' 'test/HarmonyBluetooth/ble-sdk-check.cjs')
)
if ($IncludeUsbRegression) {
    $communicationsSteps += New-Step 'usb-regression' 'usb' 'test/HarmonyUsbCdc/test-all.ps1'
}
$communicationsReport = [pscustomobject]@{
    schemaVersion = 1
    scope = 'Host software tests; no device, HDC, emulator, installation, or flight-controller operation'
    projectRoot = $communicationsProjectRoot
    includeUsbRegression = [bool]$IncludeUsbRegression
    sdkOverrides = 'Rejected for formal runs'
    startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
    status = 'running'; failure = $null; summary = $null
    components = $communicationsSteps
}

function Save-CommunicationsReport {
    $total = 0; $passed = 0; $failed = 0; $skipped = 0; $lifecycle = 0
    $cancelled = 0; $todo = 0
    $sdkChecks = 0; $sdkErrors = 0; $sdkWarnings = 0
    foreach ($step in $communicationsSteps) {
        if ($null -ne $step.testCounts) {
            $total += $step.testCounts.total; $passed += $step.testCounts.passed
            $failed += $step.testCounts.failed; $skipped += $step.testCounts.skipped
            $lifecycle += $step.testCounts.lifecycleInvocations
            $cancelled += $step.testCounts.cancelled; $todo += $step.testCounts.todo
        }
        if ($null -ne $step.sdkCounts) {
            $sdkChecks += $step.sdkCounts.checks; $sdkErrors += $step.sdkCounts.errors
            $sdkWarnings += $step.sdkCounts.warnings
        }
    }
    $communicationsReport.summary = [pscustomobject]@{
        componentsPassed = @($communicationsSteps | Where-Object status -eq 'passed').Count
        componentsFailed = @($communicationsSteps | Where-Object status -eq 'failed').Count
        componentsNotRun = @($communicationsSteps | Where-Object status -eq 'not_run').Count
        tests = $total; passed = $passed; failed = $failed; skipped = $skipped
        cancelled = $cancelled; todo = $todo
        qtLifecycleInvocations = $lifecycle; functionalInvocations = $total - $lifecycle
        sdkChecks = $sdkChecks; sdkErrors = $sdkErrors; sdkWarnings = $sdkWarnings
    }
    $communicationsReport | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $communicationsReportPath -Encoding utf8
}

Save-CommunicationsReport
Write-Output "Communications report: $communicationsReportPath"
try {
    $overrides = @('QGC_TEST_FILTER', 'QGC_BRIDGE_SOURCE', 'QGC_SPP_SOURCE', 'QGC_TYPESCRIPT', 'QGC_ETS_SDK') |
        Where-Object { -not [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($_)) }
    if ($overrides) { throw "Formal runs reject overrides: $($overrides -join ', ')" }
    foreach ($step in $communicationsSteps) {
        $step.status = 'running'
        $step.startedUtc = [DateTime]::UtcNow.ToString('o')
        $timer = [Diagnostics.Stopwatch]::StartNew()
        Save-CommunicationsReport
        Write-Output "Running $($step.id)"
        try {
            if (-not (Test-Path -LiteralPath $step.runner -PathType Leaf)) { throw "Missing runner: $($step.runner)" }
            $before = @{}
            foreach ($path in $step.reportCandidates) {
                if (Test-Path -LiteralPath $path) { $before[$path] = (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks }
            }
            if ($step.kind -in @('node', 'sdk')) {
                $executable = $communicationsNode
                $arguments = @('--max-old-space-size=256', ('"' + $step.runner + '"'))
            } else {
                $executable = $communicationsShell
                $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $step.runner + '"'))
            }
            $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $communicationsProjectRoot `
                -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $step.stdoutLog -RedirectStandardError $step.stderrLog
            $step.exitCode = $process.ExitCode
            $output = (Get-Content -LiteralPath $step.stdoutLog -Raw) + "`n" + (Get-Content -LiteralPath $step.stderrLog -Raw)
            $step.sourceReports = @($step.reportCandidates | Where-Object {
                (Test-Path -LiteralPath $_) -and (-not $before.ContainsKey($_) -or
                    (Get-Item -LiteralPath $_).LastWriteTimeUtc.Ticks -ne $before[$_])
            })
            if ($step.kind -eq 'qt') { $step.testCounts = Read-QtCounts $step.sourceReports $output }
            elseif ($step.kind -in @('node', 'usb')) { $step.testCounts = Read-NodeCounts $output }
            if ($step.kind -in @('sdk', 'usb')) { $step.sdkCounts = Read-SdkCounts $output }
            if ($step.exitCode -ne 0) { throw "Child process exited with $($step.exitCode)" }
            if ($step.kind -ne 'sdk' -and $null -eq $step.testCounts) { throw 'No fresh test counts found in reports or output' }
            if ($step.kind -in @('sdk', 'usb') -and $null -eq $step.sdkCounts) { throw 'SDK check summary was not found' }
            if (($null -ne $step.testCounts -and ($step.testCounts.failed -gt 0 -or
                $step.testCounts.cancelled -gt 0 -or $step.testCounts.todo -gt 0)) -or
                ($null -ne $step.sdkCounts -and $step.sdkCounts.errors -gt 0)) { throw 'Reports contain test or SDK failures' }
            $step.status = 'passed'
        } catch {
            $step.status = 'failed'
            $step.failure = $_.Exception.Message
            throw
        } finally {
            $timer.Stop()
            $step.seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            $step.finishedUtc = [DateTime]::UtcNow.ToString('o')
            Save-CommunicationsReport
        }
        Write-Output "PASS $($step.id)"
    }
    $communicationsReport.status = 'passed'
} catch {
    $communicationsReport.status = 'failed'
    $communicationsReport.failure = $_.Exception.Message
    Write-Output "FAILED: $($communicationsReport.failure)"
} finally {
    $communicationsReport.finishedUtc = [DateTime]::UtcNow.ToString('o')
    Save-CommunicationsReport
    Write-Output "Communications report: $communicationsReportPath"
}
if ($communicationsReport.status -ne 'passed') { exit 1 }
exit 0
