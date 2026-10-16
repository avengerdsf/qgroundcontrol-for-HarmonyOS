#requires -Version 7.0
param([switch]$CreateReport)

$ErrorActionPreference = 'Stop'
$acceptanceProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$plan = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cases.json') -Raw | ConvertFrom-Json -Depth 12
if ($plan.device -cne '127.0.0.1:5555' -or $plan.bundleName -cne 'org.mavlink.qgroundcontrol.tests') {
    throw 'The acceptance plan must retain its fixed emulator and independent test bundle.'
}
$ids = @($plan.cases | ForEach-Object id)
if ($ids.Count -ne @($ids | Select-Object -Unique).Count) { throw 'Acceptance case IDs must be unique.' }
foreach ($case in $plan.cases) {
    if ($case.id -notmatch '^[a-z0-9-]+$' -or @($case.steps).Count -eq 0 -or @($case.expected).Count -eq 0) {
        throw 'Every acceptance case needs an ID, actions, and observable expectations.'
    }
}
$plan.cases | Select-Object id, title | Format-Table -AutoSize
if (-not $CreateReport) {
    Write-Output 'Plan only. No device command was executed. Use -CreateReport to prepare local evidence records.'
    exit 0
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
    schemaVersion = 1; device = $plan.device; bundleName = $plan.bundleName
    createdUtc = [DateTime]::UtcNow.ToString('o'); status = 'not_run'
    installedVersion = $null; installedHapSha256 = $null
    sources = @(); cases = @()
}
foreach ($relative in @('src/UI/AppSettings/LinkSettings.qml', 'src/UI/AppSettings/MockLinkSettings.qml',
        'src/UI/AppSettings/MockLink.qml', 'src/UI/MainWindow.qml', 'src/QmlControls/ParameterEditor.qml',
        'src/QmlControls/PlanView.qml', 'src/QmlControls/AppMessages.qml', 'src/QmlControls/QGCFileDialog.qml')) {
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
