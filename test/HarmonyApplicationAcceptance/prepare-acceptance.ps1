#requires -Version 7.0
param([switch]$CreateReport, [string]$Device)

$ErrorActionPreference = 'Stop'
$acceptanceProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$plan = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cases.json') -Raw | ConvertFrom-Json -Depth 12
if ($plan.bundleName -cne 'org.mavlink.qgroundcontrol.tests') {
    throw 'The acceptance plan must retain its independent test bundle.'
}
$ids = @($plan.cases | ForEach-Object id)
if ($ids.Count -ne @($ids | Select-Object -Unique).Count) { throw 'Acceptance case IDs must be unique.' }
foreach ($case in $plan.cases) {
    if ($case.id -notmatch '^[a-z0-9-]+$' -or @($case.steps).Count -eq 0 -or @($case.expected).Count -eq 0) {
        throw 'Every acceptance case needs an ID, actions, and observable expectations.'
    }
    foreach ($source in $case.source) {
        if (-not (Test-Path -LiteralPath (Join-Path $acceptanceProjectRoot $source) -PathType Leaf)) {
            throw "Acceptance source does not exist: $source"
        }
    }
}
$plan.cases | Select-Object id, title | Format-Table -AutoSize
if (-not $CreateReport) {
    Write-Output 'Plan only. No device command was executed. Use -CreateReport to prepare local evidence records.'
    exit 0
}
if ([string]::IsNullOrWhiteSpace($Device)) {
    throw '-CreateReport requires an explicitly selected -Device.'
}

$tmpRoot = Join-Path $acceptanceProjectRoot 'tmp'
$reportRoot = Join-Path $tmpRoot "verification/harmony-application/run-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
for ($path = $reportRoot; ; $path = [IO.Path]::GetDirectoryName($path)) {
    if ((Test-Path -LiteralPath $path) -and
            ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Acceptance output must not traverse a link: $path"
    }
    if ($path -eq $tmpRoot) { break }
}
New-Item -ItemType Directory -Path $reportRoot | Out-Null
$report = [pscustomobject]@{
    schemaVersion = 1; device = $Device; bundleName = $plan.bundleName
    createdUtc = [DateTime]::UtcNow.ToString('o'); status = 'not_run'
    qgcVersion = '5.1.5'; installedVersion = $null; installedHapSha256 = $null
    sources = @(); cases = @()
}
foreach ($relative in @('src/AppSettings/LinkConfigurationManager.qml', 'src/AppSettings/MockLinkSettings.qml',
        'src/AppSettings/MockLink.qml', 'src/MainWindow/MainWindow.qml', 'src/QmlControls/ParameterEditor.qml',
        'src/PlanView/PlanView.qml', 'src/AppSettings/AppLogging.qml', 'src/QmlControls/QGCFileDialog.qml')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $acceptanceProjectRoot $relative) -Algorithm SHA256).Hash
    $report.sources += [pscustomobject]@{ path = $relative; sha256 = $hash }
}
foreach ($case in $plan.cases) {
    $report.cases += [pscustomobject]@{
        id = $case.id; title = $case.title; status = 'not_run'
        startedUtc = $null; finishedUtc = $null; observations = @(); evidence = @(); failure = $null
    }
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $reportRoot 'report.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'cases.json') -Destination (Join-Path $reportRoot 'cases.json')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'initial-sandbox-seed.ini') -Destination (Join-Path $reportRoot 'initial-sandbox-seed.ini')
Write-Output "Local evidence records: $reportRoot"
Write-Output 'All cases remain not_run. No device command was executed.'
