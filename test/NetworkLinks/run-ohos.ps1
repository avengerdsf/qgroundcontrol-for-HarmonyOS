#requires -Version 7.0
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^127\.0\.0\.1:\d+$')]
    [string]$Device,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$networkProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$networkBuildRoot = Join-Path $networkProjectRoot 'tmp/build-network-links-ohos-x86_64'
$networkPackageRoot = Join-Path $networkBuildRoot 'harmonyos-build'
$networkBundle = 'org.mavlink.qgc.network.tests'
$networkPort = 0
if ($Device -cne $Device.Trim() -or -not [int]::TryParse($Device.Split(':')[1], [ref]$networkPort) -or
        $networkPort -lt 1 -or $networkPort -gt 65535) {
    throw 'Device must be an explicit 127.0.0.1 TCP port between 1 and 65535.'
}
. (Join-Path $networkProjectRoot 'deploy/harmonyos/deveco-env.ps1')
$networkHdc = Join-Path $env:OHOS_SDK_ROOT 'toolchains/hdc.exe'
$networkRunner = Join-Path $networkProjectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64/bin/harmonyostestrunner.exe'
$networkCli = 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1'
$networkReportRoot = Join-Path $networkProjectRoot "tmp/verification/network-links-ohos/run-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
$networkSlots = @(
    'udpBindReceiveAndAutomaticReply', 'udpManualTargetsAndLearnedDeduplication',
    'udpLearnsMultipleEndpoints', 'udpEmptyDatagramDoesNotBlockFollowingData',
    'udpLargeDatagramEmitsNoEmptyPayload', 'udpOccupiedPortReportsFailureAndCanRetry',
    'udpCloseReopenDiscardsPreviousTargets', 'udpDisconnectedWriteReportsError',
    'udpStaleReadyReadPreservesSocketProgress', 'udpReadRaceRecoversForFollowingDatagrams',
    'udpTemporaryErrorDoesNotMaskRealErrors',
    'tcpConnectFragmentedReceiveAndBidirectionalLargeData', 'tcpRefusedConnectionCanReconnect',
    'tcpRemoteDisconnectAndReconnect', 'tcpLocalDisconnectAndReconnect',
    'tcpInvalidWriteDoesNotPreventSubsequentTransfer'
)

function Assert-NetworkPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath((Join-Path $networkProjectRoot 'tmp'))
    if ($full -ne $allowed -and -not $full.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Network artifact escapes the project tmp directory: $full"
    }
    $current = $full
    while ($current.Length -ge $allowed.Length) {
        if ((Test-Path -LiteralPath $current) -and
                ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Network artifact must not traverse a link: $current"
        }
        if ($current -eq $allowed) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

Assert-NetworkPath $networkReportRoot
New-Item -ItemType Directory -Path $networkReportRoot -Force | Out-Null
$networkReportPath = Join-Path $networkReportRoot 'report.json'
$networkReport = [pscustomobject]@{
    schemaVersion = 1; scope = 'Real Qt Harmony UDP/TCP worker loopback; configuration fixtures; no flight application or radio'
    target = $Device; architecture = $null; bundleName = $networkBundle
    skipInstall = [bool]$SkipInstall; packageRoot = $networkPackageRoot
    nativeBinary = $null; hap = $null; hapSha256 = $null; sourceSha256 = @{}
    startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
    status = 'running'; operations = @(); expectedSlots = $networkSlots; testCounts = $null; failure = $null
}

function Save-NetworkReport {
    $networkReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $networkReportPath -Encoding utf8
}

function Invoke-NetworkProcess([string]$Id, [string]$Program, [string[]]$Arguments, [int]$TimeoutSeconds = 240,
        [switch]$AllowFailure) {
    $step = [pscustomobject]@{
        id = $Id; program = $Program; arguments = $Arguments
        startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
        stdoutLog = Join-Path $networkReportRoot "$Id.stdout.log"
        stderrLog = Join-Path $networkReportRoot "$Id.stderr.log"
        exitCode = $null; timedOut = $false
    }
    $networkReport.operations += $step
    Save-NetworkReport
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.WorkingDirectory = $networkPackageRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Cannot start network operation $Id" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $step.timedOut = $true
            $process.Kill($true)
            $process.WaitForExit()
        }
        $output = $stdout.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($step.stdoutLog, $output, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($step.stderrLog, $stderr.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
        $step.exitCode = $process.ExitCode
        if ($step.timedOut) { throw "Network operation $Id exceeded $TimeoutSeconds seconds" }
        if ($step.exitCode -ne 0 -and -not $AllowFailure) {
            throw "Network operation $Id failed with exit code $($step.exitCode); see $($step.stderrLog)"
        }
        return $output
    } finally {
        $step.finishedUtc = [DateTime]::UtcNow.ToString('o')
        $process.Dispose()
        Save-NetworkReport
    }
}

function Get-NetworkHap {
    Assert-NetworkPath $networkPackageRoot
    $deployment = Get-Content -LiteralPath (Join-Path $networkBuildRoot 'QGroundControl-harmony-deployment-settings.json') -Raw |
        ConvertFrom-Json
    $nativeBinary = [string]$deployment.'application-binary'
    if ([string]::IsNullOrWhiteSpace($nativeBinary)) { throw 'Deployment settings have no application-binary.' }
    if (-not [IO.Path]::IsPathRooted($nativeBinary)) { $nativeBinary = Join-Path $networkBuildRoot $nativeBinary }
    Assert-NetworkPath $nativeBinary
    if ([IO.Path]::GetFileName($nativeBinary) -cne 'libQGroundControl.so' -or
            -not (Test-Path -LiteralPath $nativeBinary -PathType Leaf)) {
        throw 'Network deployment must identify the built libQGroundControl.so.'
    }
    $networkReport.nativeBinary = $nativeBinary
    $app = (Get-Content -LiteralPath (Join-Path $networkPackageRoot 'AppScope/app.json5') -Raw | ConvertFrom-Json).app
    if ($app.bundleName -cne $networkBundle) { throw 'AppScope is not the fixed network test bundle.' }
    $metadata = @(Get-Content -LiteralPath (Join-Path $networkPackageRoot 'entry/build/default/intermediates/hap_metadata/default/output_metadata.json') -Raw |
        ConvertFrom-Json)
    if ($metadata.Count -ne 1) { throw 'Expected one network entry HAP metadata record.' }
    $name = [string]$metadata[0].hapName
    if ($name -notmatch '^[A-Za-z0-9_.-]+\.hap$' -or [IO.Path]::GetFileName($name) -cne $name) {
        throw 'Network HAP metadata has an invalid filename.'
    }
    $outputRoot = Join-Path $networkPackageRoot 'entry/build/default/outputs/default'
    $hap = Join-Path $outputRoot ($name -replace '-unsigned\.hap$', '-signed.hap')
    if (-not (Test-Path -LiteralPath $hap -PathType Leaf)) { $hap = Join-Path $outputRoot $name }
    Assert-NetworkPath $hap
    $archive = [IO.Compression.ZipFile]::OpenRead($hap)
    try {
        $manifestEntry = $archive.GetEntry('module.json')
        if (-not $manifestEntry) { throw 'Network HAP module.json is missing.' }
        $reader = [IO.StreamReader]::new($manifestEntry.Open())
        try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($manifest.app.bundleName -cne $networkBundle) { throw 'HAP is not the fixed network test bundle.' }
        if (-not (@($manifest.module.requestPermissions).name -contains 'ohos.permission.INTERNET')) {
            throw 'Network HAP must declare ohos.permission.INTERNET.'
        }
        if (-not $archive.GetEntry('libs/x86_64/libQGroundControl.so')) { throw 'Network x86_64 test library is missing.' }
        foreach ($entry in $archive.Entries | Where-Object { $_.FullName.EndsWith('.so', [StringComparison]::Ordinal) }) {
            if (-not $entry.FullName.StartsWith('libs/x86_64/', [StringComparison]::Ordinal)) {
                throw "Network HAP contains a non-x86_64 library: $($entry.FullName)"
            }
            $reader = [IO.BinaryReader]::new($entry.Open())
            try { $header = $reader.ReadBytes(20) } finally { $reader.Dispose() }
            if ($header.Length -ne 20 -or $header[0] -ne 0x7f -or $header[1] -ne 0x45 -or
                    $header[2] -ne 0x4c -or $header[3] -ne 0x46 -or $header[4] -ne 2 -or
                    $header[5] -ne 1 -or [BitConverter]::ToUInt16($header, 18) -ne 62) {
                throw "Network native library is not x86_64 ELF: $($entry.FullName)"
            }
        }
    } finally { $archive.Dispose() }
    return $hap
}

function Read-NetworkCounts([string]$Output) {
    $totals = [regex]::Matches($Output, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted')
    if ($totals.Count -ne 1) { throw 'Expected one complete Qt Test summary.' }
    $summary = $totals[0]
    $counts = [pscustomobject]@{
        passed = [int]$summary.Groups[1].Value; failed = [int]$summary.Groups[2].Value
        skipped = [int]$summary.Groups[3].Value; blacklisted = [int]$summary.Groups[4].Value
        functionalSlots = $networkSlots.Count; lifecycleInvocations = 2
    }
    $networkReport.testCounts = $counts
    if ([regex]::Matches($Output, '(?m)^\s*QGC_NETWORK_DONE exit=0\s*$').Count -ne 1) {
        throw 'Successful network completion marker is missing or ambiguous.'
    }
    foreach ($slot in @('initTestCase', 'cleanupTestCase') + $networkSlots) {
        if (-not [regex]::IsMatch($Output, ('(?m)^\s*PASS\s*:\s*NetworkLinksTest::' + [regex]::Escape($slot) + '\('))) {
            throw "Network stdout has no PASS for required slot $slot"
        }
    }
    $passLines = [regex]::Matches($Output, '(?m)^\s*PASS\s*:\s*').Count
    if ($counts.failed -ne 0 -or $counts.skipped -ne 0 -or $counts.blacklisted -ne 0 -or
            $counts.passed -ne $networkSlots.Count + 2 -or $passLines -ne $counts.passed) {
        throw 'Network tests have failures, skips, blacklisted cases, or inconsistent PASS counts.'
    }
    return $counts
}

Save-NetworkReport
try {
    foreach ($tool in @($networkHdc, $networkRunner, $networkCli)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Network tool is missing: $tool" }
    }
    if (-not (Test-Path -LiteralPath $networkPackageRoot -PathType Container)) { throw 'Build the network HAP first.' }
    $hap = Get-NetworkHap
    $networkReport.hap = $hap
    $networkReport.hapSha256 = (Get-FileHash -LiteralPath $hap -Algorithm SHA256).Hash
    foreach ($relative in @('test/NetworkLinks/CMakeLists.txt', 'test/NetworkLinks/NetworkLinksTest.cc',
            'test/NetworkLinks/Workers.h.in', 'test/NetworkLinks/Workers.cc.in',
            'src/Comms/UDPLink.cc', 'src/Comms/UDPLink.h', 'src/Comms/TCPLink.cc', 'src/Comms/TCPLink.h')) {
        $networkReport.sourceSha256[$relative] = (Get-FileHash -LiteralPath (Join-Path $networkProjectRoot $relative) -Algorithm SHA256).Hash
    }
    $abi = Invoke-NetworkProcess 'target-abi' $networkHdc @('-t', $Device, 'shell', 'param', 'get', 'const.product.cpu.abilist') 30
    if ($abi.Trim() -notmatch '^(?:const\.product\.cpu\.abilist\s*=\s*)?x86_64$') {
        throw 'Target ABI must be exactly x86_64; unknown and non-x86_64 targets are rejected.'
    }
    $networkReport.architecture = 'x86_64'
    if (-not $SkipInstall) {
        $pwsh = (Get-Process -Id $PID).Path
        $null = Invoke-NetworkProcess 'install-test-bundle' $pwsh @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', $networkCli, 'run', '--skip-build', '--device', $Device, '--module', 'entry',
            '--ability', 'QAbility', '--build-mode', 'debug') 180
    }
    $arguments = @('--device', $Device, '--bundle-name', $networkBundle, '--ability-name', 'QAbility',
        '--hdc', $networkHdc, '--timeout', '180', '--no-progress-timeout', '45',
        '--test-env', 'QGC_NETWORK_RUN=1', '--', 'libQGroundControl.so') + $networkSlots
    $output = Invoke-NetworkProcess 'loopback' $networkRunner $arguments 240 -AllowFailure
    $networkReport.testCounts = Read-NetworkCounts $output
    if ($networkReport.operations[-1].exitCode -ne 0) {
        throw "Network tests exited with code $($networkReport.operations[-1].exitCode)."
    }
    $networkReport.status = 'passed'
    Write-Output "Harmony network loopback passed: $($networkSlots.Count) functional and 2 lifecycle invocations."
} catch {
    $networkReport.status = 'failed'
    $networkReport.failure = $_.Exception.Message
    Write-Error -Message $networkReport.failure -ErrorAction Continue
} finally {
    $networkReport.finishedUtc = [DateTime]::UtcNow.ToString('o')
    Save-NetworkReport
    Write-Output "Network report: $networkReportPath"
}
if ($networkReport.status -ne 'passed') { exit 1 }
exit 0
