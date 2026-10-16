#requires -Version 7.0
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^127\.0\.0\.1:\d+$')]
    [string]$Device,
    [ValidateSet('core', 'grant', 'deny')]
    [string]$Phase = 'core',
    [switch]$SkipInstall,
    [switch]$ResetTestBundle
)

$ErrorActionPreference = 'Stop'
$runtimeProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$runtimeBuildRoot = Join-Path $runtimeProjectRoot 'tmp/build-harmony-communications-runtime-x86_64'
$runtimePackageRoot = Join-Path $runtimeBuildRoot 'harmonyos-build'
$runtimeBundle = 'org.mavlink.qgc.runtime.tests'
$runtimeLabel = 'QGC Runtime Tests'
$runtimePort = 0
if ($Device -cne $Device.Trim() -or -not [int]::TryParse($Device.Split(':')[1], [ref]$runtimePort) -or $runtimePort -lt 1 -or $runtimePort -gt 65535) {
    throw 'Device must be an explicit 127.0.0.1 TCP port between 1 and 65535.'
}
if ($ResetTestBundle -and ($SkipInstall -or $Phase -eq 'core')) {
    throw '-ResetTestBundle requires installation and an explicit grant or deny phase.'
}
. (Join-Path $runtimeProjectRoot 'deploy/harmonyos/deveco-env.ps1')
$runtimeHdc = Join-Path $env:OHOS_SDK_ROOT 'toolchains/hdc.exe'
$runtimeRunner = Join-Path $runtimeProjectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64/bin/harmonyostestrunner.exe'
$runtimeReportRoot = Join-Path $runtimeProjectRoot "tmp/verification/harmony-runtime/run-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"

function Assert-RuntimePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath((Join-Path $runtimeProjectRoot 'tmp'))
    if ($full -ne $allowed -and -not $full.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Runtime artifact escapes the project tmp directory: $full"
    }
    $current = $full
    while ($current.Length -ge $allowed.Length) {
        if ((Test-Path -LiteralPath $current) -and
                ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Runtime artifact must not traverse a link: $current"
        }
        if ($current -eq $allowed) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

Assert-RuntimePath $runtimeReportRoot
New-Item -ItemType Directory -Path $runtimeReportRoot -Force | Out-Null
$runtimeReportPath = Join-Path $runtimeReportRoot 'report.json'
$runtimeReport = [pscustomobject]@{
    schemaVersion = 1; target = $Device; architecture = $null
    bundleName = $runtimeBundle; label = $runtimeLabel; phase = $Phase
    skipInstall = [bool]$SkipInstall; resetTestBundle = [bool]$ResetTestBundle
    packageRoot = $runtimePackageRoot; nativeBinary = $null; hap = $null; hapSha256 = $null
    startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
    status = 'running'; operations = @(); testCounts = $null; expectedSlots = @(); failure = $null
}

function Save-RuntimeReport {
    $runtimeReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $runtimeReportPath -Encoding utf8
}

function Invoke-RuntimeProcess([string]$Id, [string]$Program, [string[]]$Arguments, [int]$TimeoutSeconds = 240, [switch]$AllowFailure) {
    $stdoutPath = Join-Path $runtimeReportRoot "$Id.stdout.log"
    $stderrPath = Join-Path $runtimeReportRoot "$Id.stderr.log"
    $step = [pscustomobject]@{
        id = $Id; program = $Program; arguments = $Arguments
        startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
        stdoutLog = $stdoutPath; stderrLog = $stderrPath; exitCode = $null; timedOut = $false
    }
    $runtimeReport.operations += $step
    [IO.File]::WriteAllText($stdoutPath, '', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, '', [Text.UTF8Encoding]::new($false))
    Save-RuntimeReport
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.WorkingDirectory = $runtimePackageRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Cannot start runtime operation $Id" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $step.timedOut = $true
            $process.Kill($true)
            $process.WaitForExit()
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($stdoutPath, $output, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($stderrPath, $errors, [Text.UTF8Encoding]::new($false))
        $step.exitCode = $process.ExitCode
        if ($step.timedOut) { throw "Runtime operation $Id exceeded $TimeoutSeconds seconds" }
        if ($step.exitCode -ne 0 -and -not $AllowFailure) { throw "Runtime operation $Id failed with exit code $($step.exitCode); see $stderrPath" }
        return $output
    } finally {
        $step.finishedUtc = [DateTime]::UtcNow.ToString('o')
        $process.Dispose()
        Save-RuntimeReport
    }
}

function Get-RuntimeHap {
    Assert-RuntimePath $runtimePackageRoot
    $deploymentPath = Join-Path $runtimeBuildRoot 'QGroundControl-harmony-deployment-settings.json'
    Assert-RuntimePath $deploymentPath
    $deployment = Get-Content -LiteralPath $deploymentPath -Raw | ConvertFrom-Json
    $nativeBinary = [string]$deployment.'application-binary'
    if ([string]::IsNullOrWhiteSpace($nativeBinary)) { throw 'Runtime deployment settings have no application-binary.' }
    if (-not [IO.Path]::IsPathRooted($nativeBinary)) { $nativeBinary = Join-Path $runtimeBuildRoot $nativeBinary }
    Assert-RuntimePath $nativeBinary
    if ([IO.Path]::GetFileName($nativeBinary) -cne 'libQGroundControl.so' -or -not (Test-Path -LiteralPath $nativeBinary -PathType Leaf)) {
        throw 'Runtime deployment settings must identify the built libQGroundControl.so.'
    }
    $runtimeReport.nativeBinary = $nativeBinary
    $appPath = Join-Path $runtimePackageRoot 'AppScope/app.json5'
    Assert-RuntimePath $appPath
    $app = (Get-Content -LiteralPath $appPath -Raw | ConvertFrom-Json).app
    if ($app.bundleName -cne $runtimeBundle) { throw 'Staged AppScope is not the fixed runtime test bundle.' }
    $metadataPath = Join-Path $runtimePackageRoot 'entry/build/default/intermediates/hap_metadata/default/output_metadata.json'
    Assert-RuntimePath $metadataPath
    $metadata = @(Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json)
    if ($metadata.Count -ne 1) { throw 'Expected one runtime entry HAP metadata record.' }
    $name = [string]$metadata[0].hapName
    if ($name -notmatch '^[A-Za-z0-9_.-]+\.hap$' -or [IO.Path]::GetFileName($name) -cne $name) {
        throw 'Runtime HAP metadata has an invalid filename.'
    }
    $outputRoot = Join-Path $runtimePackageRoot 'entry/build/default/outputs/default'
    $signed = $name -replace '-unsigned\.hap$', '-signed.hap'
    $hap = Join-Path $outputRoot $signed
    if (-not (Test-Path -LiteralPath $hap -PathType Leaf)) { $hap = Join-Path $outputRoot $name }
    Assert-RuntimePath $hap
    $archive = [IO.Compression.ZipFile]::OpenRead($hap)
    try {
        $manifestEntry = $archive.GetEntry('module.json')
        if (-not $manifestEntry) { throw 'Runtime HAP module.json is missing.' }
        $reader = [IO.StreamReader]::new($manifestEntry.Open())
        try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($manifest.app.bundleName -cne $runtimeBundle) { throw 'Runtime HAP bundle does not match the fixed test bundle.' }
        if (-not $archive.GetEntry('libs/x86_64/libQGroundControl.so')) { throw 'Runtime x86_64 test library is missing.' }
        foreach ($entry in $archive.Entries | Where-Object { $_.FullName.EndsWith('.so', [StringComparison]::Ordinal) }) {
            if (-not $entry.FullName.StartsWith('libs/x86_64/', [StringComparison]::Ordinal)) {
                throw "Runtime HAP contains a non-x86_64 library: $($entry.FullName)"
            }
            $reader = [IO.BinaryReader]::new($entry.Open())
            try { $header = $reader.ReadBytes(20) } finally { $reader.Dispose() }
            if ($header.Length -ne 20 -or $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
                    $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or $header[4] -ne 2 -or
                    $header[5] -ne 1 -or [BitConverter]::ToUInt16($header, 18) -ne 62) {
                throw "Runtime native library is not x86_64 ELF: $($entry.FullName)"
            }
        }
    } finally { $archive.Dispose() }
    return $hap
}

function Read-RuntimeCounts([string]$Output, [string]$SelectedPhase, [string[]]$Slots) {
    $totals = [regex]::Matches($Output, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted')
    if ($totals.Count -ne 1) { throw 'Expected one complete Qt Test summary in runtime stdout.' }
    $summary = $totals[0]
    $counts = [pscustomobject]@{
        passed = [int]$summary.Groups[1].Value; failed = [int]$summary.Groups[2].Value
        skipped = [int]$summary.Groups[3].Value; blacklisted = [int]$summary.Groups[4].Value
        functionalSlots = $Slots.Count; phase = $SelectedPhase
    }
    $runtimeReport.testCounts = $counts
    $done = [regex]::Matches($Output, ('QGC_RUNTIME_DONE phase=' + [regex]::Escape($SelectedPhase) + ' exit=(-?\d+)\b'))
    if ($done.Count -ne 1 -or $done[0].Groups[1].Value -ne '0') { throw 'Successful runtime completion marker is missing or ambiguous.' }
    foreach ($slot in $Slots) {
        if (-not [regex]::IsMatch($Output, ('(?m)^\s*PASS\s*:\s*[^\r\n]*::' + [regex]::Escape($slot) + '\('))) {
            throw "Runtime stdout has no PASS for required slot $slot"
        }
    }
    $passLines = [regex]::Matches($Output, '(?m)^\s*PASS\s*:\s*').Count
    if ($counts.failed -ne 0 -or $counts.skipped -ne 0 -or $counts.blacklisted -ne 0 -or
            $counts.passed -lt $Slots.Count -or $passLines -ne $counts.passed) {
        throw 'Runtime phase has failures, skips, blacklisted cases, or an inconsistent PASS summary.'
    }
    return $counts
}

Save-RuntimeReport
try {
    foreach ($tool in @($runtimeHdc, $runtimeRunner)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Runtime tool is missing: $tool" }
    }
    if (-not (Test-Path -LiteralPath $runtimePackageRoot -PathType Container)) { throw 'Build the independent runtime test package first.' }
    Assert-RuntimePath $runtimePackageRoot
    $abi = Invoke-RuntimeProcess 'target-abi' $runtimeHdc @('-t', $Device, 'shell', 'param', 'get', 'const.product.cpu.abilist') 30
    if ($abi.Trim() -notmatch '^(?:const\.product\.cpu\.abilist\s*=\s*)?x86_64$') {
        throw 'Target ABI must be exactly x86_64; unknown or non-x86_64 targets are rejected.'
    }
    $runtimeReport.architecture = 'x86_64'
    $hap = Get-RuntimeHap
    $runtimeReport.hap = $hap
    $runtimeReport.hapSha256 = (Get-FileHash -LiteralPath $hap -Algorithm SHA256).Hash
    Save-RuntimeReport
    if ($ResetTestBundle) {
        $removed = Invoke-RuntimeProcess 'reset-test-bundle' $runtimeHdc @('-t', $Device, 'uninstall', $runtimeBundle) 90
        if ($removed -notmatch '(?i)\bsuccess(?:fully)?\b' -or $removed -match '(?i)\b(?:error|failed|failure)\b|\[fail\]') {
            throw 'The explicit test-bundle reset did not report successful uninstallation.'
        }
    }
    if (-not $SkipInstall) {
        $installed = Invoke-RuntimeProcess 'install-test-bundle' $runtimeHdc @('-t', $Device, 'install', $hap) 180
        if ($installed -notmatch '(?i)\bsuccess(?:fully)?\b' -or $installed -match '(?i)\b(?:error|failed|failure)\b|\[fail\]') {
            throw 'Runtime HAP installation did not report success.'
        }
    }
    $slots = if ($Phase -eq 'core') {
        @('factoriesReady', 'sppParametersAndBinary', 'bleParametersAndBinary', 'errorPropagation', 'lateCloseIsolation', 'productionSnapshot')
    } else { @('permissionRequest', 'permissionRepeat') }
    $runtimeReport.expectedSlots = $slots
    $dialogs = @()
    if ($Phase -ne 'core') {
        $button = if ($Phase -eq 'grant') { '^(?:Allow|允许)$' } else { "^(?:Deny|Don't allow|拒绝|不允许)$" }
        $dialogs = @(@{
            name = "runtime-bluetooth-$Phase"
            prompt = '^(?=[\s\S]*QGC Runtime Tests)(?=[\s\S]*(?:Bluetooth|蓝牙|nearby devices|附近(?:的)?设备))[\s\S]+$'
            buttons = @($button)
        })
    }
    $configPath = Join-Path $runtimeReportRoot "$Phase.test-config.json"
    @{ blockingTestDialogs = $dialogs } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $configPath -Encoding utf8
    $testEnvironment = "QGC_RUNTIME_PHASE=$Phase;QGC_RUNTIME_EXPECT_DIALOG=$([int][bool]$ResetTestBundle)"
    $arguments = @('--device', $Device, '--bundle-name', $runtimeBundle, '--ability-name', 'QAbility',
        '--hdc', $runtimeHdc, '--timeout', '180', '--no-progress-timeout', '45',
        '--test-env', $testEnvironment, '--test-config', $configPath, '--', 'libQGroundControl.so') + $slots
    $output = Invoke-RuntimeProcess $Phase $runtimeRunner $arguments 240 -AllowFailure
    $runtimeReport.testCounts = Read-RuntimeCounts $output $Phase $slots
    if ($runtimeReport.operations[-1].exitCode -ne 0) { throw "Runtime phase exited with code $($runtimeReport.operations[-1].exitCode)." }
    $runtimeReport.status = 'passed'
    Write-Output "Runtime $Phase passed: $($runtimeReport.testCounts.passed) PASS, 0 failed/skipped/blacklisted."
} catch {
    $runtimeReport.status = 'failed'
    $runtimeReport.failure = $_.Exception.Message
    Write-Error -Message $runtimeReport.failure -ErrorAction Continue
} finally {
    $runtimeReport.finishedUtc = [DateTime]::UtcNow.ToString('o')
    Save-RuntimeReport
    Write-Output "Runtime report: $runtimeReportPath"
}
if ($runtimeReport.status -ne 'passed') { exit 1 }
exit 0
