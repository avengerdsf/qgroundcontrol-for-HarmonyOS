#requires -Version 7.0
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^127\.0\.0\.1:\d+$')]
    [string]$Device,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$nmeaProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$nmeaBuildRoot = Join-Path $nmeaProjectRoot 'tmp/build-nmea-serial-source-ohos-x86_64'
$nmeaPackageRoot = Join-Path $nmeaBuildRoot 'harmonyos-build'
$nmeaBundle = 'org.mavlink.qgc.nmea.tests'
$nmeaPort = 0
if ($Device -cne $Device.Trim() -or -not [int]::TryParse($Device.Split(':')[1], [ref]$nmeaPort) -or
        $nmeaPort -lt 1 -or $nmeaPort -gt 65535) {
    throw 'Device must be an explicit 127.0.0.1 TCP port between 1 and 65535.'
}
. (Join-Path $nmeaProjectRoot 'deploy/harmonyos/deveco-env.ps1')
$env:NODE_OPTIONS = '--max-old-space-size=512'
$env:_JAVA_OPTIONS = '-Xmx512m'
$env:Xmx = '512'
$nmeaHdc = Join-Path $env:OHOS_SDK_ROOT 'toolchains/hdc.exe'
$nmeaRunner = Join-Path $nmeaProjectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64/bin/harmonyostestrunner.exe'
$nmeaReportRoot = Join-Path $nmeaProjectRoot "tmp/verification/nmea-serial-source-ohos/run-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
$nmeaSlots = @(
    'configuredPortFeedsRealNmeaParser', 'baudChangesWhileAuthorizationIsPending',
    'disableCancelsPendingOpenAndLateGrant', 'denialIsVisibleAndOnlyExplicitReselectionRetries',
    'unplugAndUdpSwitchReleaseTheSerialDevice', 'oldReadsCannotReachAReplacementSource',
    'disableStopsPendingPositionUpdates', 'fifoLineReadsPreserveTheIncompleteTail',
    'oversizedUnterminatedInputFailsWithoutDroppingBytes'
)

function Assert-NmeaPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath((Join-Path $nmeaProjectRoot 'tmp'))
    if ($full -ne $allowed -and -not $full.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'NMEA artifact escapes the project tmp directory.'
    }
    $candidate = $full
    while ($candidate.Length -ge $allowed.Length) {
        if ((Test-Path -LiteralPath $candidate) -and
                ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'NMEA artifact must not traverse a link.'
        }
        if ($candidate -eq $allowed) { break }
        $candidate = [IO.Path]::GetDirectoryName($candidate)
    }
}
Assert-NmeaPath $nmeaReportRoot
New-Item -ItemType Directory -Path $nmeaReportRoot -Force | Out-Null
$nmeaReportPath = Join-Path $nmeaReportRoot 'report.json'
$nmeaReport = [pscustomobject]@{
    schemaVersion = 1; scope = 'Real Qt6.12 Harmony NMEA QIODevice/parser with controlled native serial provider; no USB NAPI or GNSS hardware test'
    target = $Device; architecture = $null; apiVersion = $null; bundleName = $nmeaBundle
    skipInstall = [bool]$SkipInstall; packageRoot = $nmeaPackageRoot
    nativeBinary = $null; nativeManifest = $null; sourceSnapshot = $null; sourceSha256 = @{}
    hap = $null; hapSha256 = $null; expectedSlots = $nmeaSlots; testCounts = $null
    startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
    status = 'running'; operations = @(); failure = $null
}
function Save-NmeaReport {
    $nmeaReport | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $nmeaReportPath -Encoding utf8
}
function Invoke-NmeaProcess([string]$Id, [string]$Program, [string[]]$Arguments,
        [int]$TimeoutSeconds = 240, [switch]$AllowFailure) {
    $step = [pscustomobject]@{
        id = $Id; program = $Program; arguments = $Arguments
        startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
        stdoutLog = Join-Path $nmeaReportRoot "$Id.stdout.log"
        stderrLog = Join-Path $nmeaReportRoot "$Id.stderr.log"
        exitCode = $null; timedOut = $false
    }
    $nmeaReport.operations += $step
    Save-NmeaReport
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.WorkingDirectory = $nmeaPackageRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Cannot start NMEA operation $Id" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $step.timedOut = $true
            $process.Kill()
            $process.WaitForExit()
        }
        $output = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($step.stdoutLog, $output, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($step.stderrLog, $stderr.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
        $step.exitCode = $process.ExitCode
        if ($step.timedOut) { throw "NMEA operation $Id exceeded $TimeoutSeconds seconds" }
        if ($step.exitCode -ne 0 -and -not $AllowFailure) { throw "NMEA operation $Id exited $($step.exitCode)." }
        return $output
    } finally {
        $step.finishedUtc = [DateTime]::UtcNow.ToString('o')
        $process.Dispose()
        Save-NmeaReport
    }
}
function Get-NmeaHap {
    Assert-NmeaPath $nmeaPackageRoot
    $manifestPath = Join-Path $nmeaBuildRoot 'nmea-native-inputs.json'
    $compiled = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($compiled.bundleName -cne $nmeaBundle -or $compiled.architecture -cne 'x86_64' -or
            $compiled.sourceSnapshot -notmatch '^[a-f0-9]{64}$') { throw 'NMEA native build manifest has the wrong identity.' }
    foreach ($property in $compiled.sourceSha256.PSObject.Properties) {
        $path = [IO.Path]::GetFullPath((Join-Path $nmeaProjectRoot $property.Name))
        if (-not $path.StartsWith($nmeaProjectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'NMEA source manifest escapes the project.'
        }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -cne $property.Value) { throw "NMEA native source changed since build: $($property.Name)" }
        $nmeaReport.sourceSha256[$property.Name] = $hash
    }
    if ($nmeaReport.sourceSha256.Count -lt 10) { throw 'NMEA native source manifest is incomplete.' }
    $deployment = Get-Content -LiteralPath (Join-Path $nmeaBuildRoot 'QGroundControl-harmony-deployment-settings.json') -Raw | ConvertFrom-Json
    $native = [string]$deployment.'application-binary'
    if (-not [IO.Path]::IsPathRooted($native)) { $native = Join-Path $nmeaBuildRoot $native }
    Assert-NmeaPath $native
    if (-not [IO.Path]::GetFullPath($native).StartsWith($nmeaBuildRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($native) -cne 'libQGroundControl.so') {
        throw 'NMEA deployment must identify this target libQGroundControl.so.'
    }
    if ((Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant() -cne $compiled.nativeSha256) {
        throw 'NMEA native binary changed after its build manifest.'
    }
    $nmeaReport.nativeBinary = $native
    $nmeaReport.nativeManifest = $manifestPath
    $nmeaReport.sourceSnapshot = $compiled.sourceSnapshot
    $app = (Get-Content -LiteralPath (Join-Path $nmeaPackageRoot 'AppScope/app.json5') -Raw | ConvertFrom-Json).app
    if ($app.bundleName -cne $nmeaBundle) { throw 'AppScope is not the fixed NMEA test bundle.' }
    $utils = Get-Content -LiteralPath (Join-Path $nmeaPackageRoot 'entry/src/main/ets/qability/QtUtils.ets') -Raw
    if ($utils -match 'factories\s*\[\s*[''"]QGC(?:Serial|Bluetooth|Ble)Bridge[''"]\s*\]') {
        throw 'Controlled NMEA package must not register real USB or Bluetooth ETS factories.'
    }
    $metadata = @(Get-Content -LiteralPath (Join-Path $nmeaPackageRoot 'entry/build/default/intermediates/hap_metadata/default/output_metadata.json') -Raw | ConvertFrom-Json)
    if ($metadata.Count -ne 1) { throw 'Expected one NMEA entry HAP metadata record.' }
    $name = [string]$metadata[0].hapName
    if ($name -notmatch '^[A-Za-z0-9_.-]+\.hap$' -or [IO.Path]::GetFileName($name) -cne $name) { throw 'Invalid NMEA HAP filename.' }
    $outputRoot = Join-Path $nmeaPackageRoot 'entry/build/default/outputs/default'
    $hap = Join-Path $outputRoot ($name -replace '-unsigned\.hap$', '-signed.hap')
    if (-not (Test-Path -LiteralPath $hap -PathType Leaf)) { $hap = Join-Path $outputRoot $name }
    Assert-NmeaPath $hap
    $archive = [IO.Compression.ZipFile]::OpenRead($hap)
    try {
        $entry = $archive.GetEntry('module.json')
        if (-not $entry) { throw 'NMEA HAP module.json is missing.' }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($manifest.app.bundleName -cne $nmeaBundle) { throw 'HAP is not the fixed NMEA test bundle.' }
        if (-not (@($manifest.module.requestPermissions).name -contains 'ohos.permission.INTERNET')) {
            throw 'NMEA UDP switch test requires INTERNET permission.'
        }
        if (-not $archive.GetEntry('libs/x86_64/libQGroundControl.so')) { throw 'NMEA x86_64 test library is missing.' }
        foreach ($library in $archive.Entries | Where-Object {$_.FullName.EndsWith('.so', [StringComparison]::Ordinal)}) {
            if (-not $library.FullName.StartsWith('libs/x86_64/', [StringComparison]::Ordinal)) { throw 'NMEA HAP contains a non-x86_64 library.' }
            $reader = [IO.BinaryReader]::new($library.Open())
            try { $header = $reader.ReadBytes(20) } finally { $reader.Dispose() }
            if ($header.Length -ne 20 -or $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
                    $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or $header[4] -ne 2 -or
                    $header[5] -ne 1 -or [BitConverter]::ToUInt16($header, 18) -ne 62) {
                throw "NMEA native library is not x86_64 ELF: $($library.FullName)"
            }
        }
    } finally { $archive.Dispose() }
    return $hap
}
function Read-NmeaCounts([string]$Output) {
    $ready = [regex]::Matches($Output, '(?m)^\s*QGC_NMEA_READY mode=controlled-provider qt=6\.12\.0 snapshot=([a-f0-9]{64})\s*$')
    if ($ready.Count -ne 1 -or $ready[0].Groups[1].Value -cne $nmeaReport.sourceSnapshot) {
        throw 'Qt6.12 NMEA READY marker or compiled source snapshot is missing or inconsistent.'
    }
    if ([regex]::Matches($Output, '(?m)^\s*QGC_NMEA_DONE exit=0\s*$').Count -ne 1) { throw 'NMEA DONE marker is missing or ambiguous.' }
    $totals = [regex]::Matches($Output, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted')
    if ($totals.Count -ne 1) { throw 'Expected one complete NMEA Qt Test summary.' }
    $counts = [pscustomobject]@{passed=[int]$totals[0].Groups[1].Value;failed=[int]$totals[0].Groups[2].Value;
        skipped=[int]$totals[0].Groups[3].Value;blacklisted=[int]$totals[0].Groups[4].Value;
        functionalSlots=$nmeaSlots.Count;lifecycleInvocations=2}
    foreach ($slot in @('initTestCase','cleanupTestCase') + $nmeaSlots) {
        if (-not [regex]::IsMatch($Output, ('(?m)^\s*PASS\s*:\s*NmeaSerialSourceTest::' + [regex]::Escape($slot) + '\('))) {
            throw "NMEA stdout has no PASS for required slot $slot"
        }
    }
    if ($counts.failed -or $counts.skipped -or $counts.blacklisted -or $counts.passed -ne 11 -or
            [regex]::Matches($Output, '(?m)^\s*PASS\s*:\s*').Count -ne 11) { throw 'NMEA results contain failures, skips or inconsistent counts.' }
    return $counts
}

Save-NmeaReport
try {
    foreach ($tool in @($nmeaHdc, $nmeaRunner)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "NMEA tool is missing: $tool" }
    }
    $hap = Get-NmeaHap
    $nmeaReport.hap = $hap
    $nmeaReport.hapSha256 = (Get-FileHash -LiteralPath $hap -Algorithm SHA256).Hash.ToLowerInvariant()
    $abi = Invoke-NmeaProcess 'target-abi' $nmeaHdc @('-t', $Device, 'shell', 'param', 'get', 'const.product.cpu.abilist') 30
    if ($abi.Trim() -notmatch '^(?:const\.product\.cpu\.abilist\s*=\s*)?x86_64$') { throw 'Target ABI must be exactly x86_64.' }
    $nmeaReport.architecture = 'x86_64'
    $api = Invoke-NmeaProcess 'target-api' $nmeaHdc @('-t', $Device, 'shell', 'param', 'get', 'const.ohos.apiversion') 30
    if ($api.Trim() -notmatch '^(?:const\.ohos\.apiversion\s*=\s*)?24$') { throw 'This acceptance entry requires the API24 emulator.' }
    $nmeaReport.apiVersion = 24
    if (-not $SkipInstall) {
        $installed = Invoke-NmeaProcess 'install-test-bundle' $nmeaHdc @('-t', $Device, 'install', $hap) 180
        if ($installed -notmatch '(?i)\bsuccess(?:fully)?\b' -or $installed -match '(?i)\b(?:error|failed|failure)\b|\[fail\]') {
            throw 'NMEA HAP installation did not report success.'
        }
    }
    $arguments = @('--device', $Device, '--bundle-name', $nmeaBundle, '--ability-name', 'QAbility',
        '--hdc', $nmeaHdc, '--timeout', '180', '--no-progress-timeout', '45',
        '--test-env', 'QGC_NMEA_RUN=1', '--', 'libQGroundControl.so') + $nmeaSlots
    $output = Invoke-NmeaProcess 'nmea' $nmeaRunner $arguments 240 -AllowFailure
    $nmeaReport.testCounts = Read-NmeaCounts $output
    if ($nmeaReport.operations[-1].exitCode -ne 0) { throw "NMEA tests exited $($nmeaReport.operations[-1].exitCode)." }
    $nmeaReport.status = 'passed'
    Write-Output 'Harmony NMEA passed: 9 functional and 2 lifecycle invocations.'
} catch {
    $nmeaReport.status = 'failed'
    $nmeaReport.failure = $_.Exception.Message
    Write-Error -Message $nmeaReport.failure -ErrorAction Continue
} finally {
    $nmeaReport.finishedUtc = [DateTime]::UtcNow.ToString('o')
    Save-NmeaReport
    Write-Output "NMEA report: $nmeaReportPath"
}
if ($nmeaReport.status -ne 'passed') { exit 1 }
exit 0
