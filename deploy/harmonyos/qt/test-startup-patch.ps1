$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$sourceRoot = Join-Path $projectRoot 'tmp/qt-src/qt-everywhere-src-6.12.0'
$fixtureRoot = Join-Path $projectRoot "tmp/verification/qt-window-startup-patch-$PID"
$relativeRender = 'qtbase/src/plugins/platforms/ohos/render'
$fixtureRender = Join-Path $fixtureRoot $relativeRender
$relativeFixture = [IO.Path]::GetRelativePath($projectRoot, $fixtureRoot).Replace('\', '/')
$fileNames = @('qohosview.cpp', 'qohosview.h', 'qohoswindowproxy.cpp', 'qohoswindowproxy.h')
New-Item -ItemType Directory -Path $fixtureRender -Force | Out-Null
function FileSnapshot {
    return @($fileNames | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $fixtureRender $_) -Algorithm SHA256).Hash }) -join ','
}
try {
    foreach ($name in $fileNames) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $sourceRoot $relativeRender) $name) -Destination (Join-Path $fixtureRender $name)
    }
    $expected = FileSnapshot
    foreach ($name in @('0002-restore-tablet-startup-window-state.patch', '0001-respect-window-mode-on-system-sync.patch')) {
        & git -C $projectRoot apply --reverse "--directory=$relativeFixture" (Join-Path $PSScriptRoot $name)
        if ($LASTEXITCODE -ne 0) { throw "Preparing original Qt fixture failed: $name" }
    }
    $original = FileSnapshot
    & (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $fixtureRoot -CheckOnly
    if ((FileSnapshot) -cne $original) { throw 'CheckOnly changed the source fixture.' }
    Write-Output 'PASS Qt patch CheckOnly preserves the original source fixture'
    & (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $fixtureRoot
    if ((FileSnapshot) -cne $expected) { throw 'Qt patch application did not reproduce production source.' }
    Write-Output 'PASS ordered Qt patches reproduce all production source files'
    & (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $fixtureRoot -CheckOnly
    & (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $fixtureRoot
    if ((FileSnapshot) -cne $expected) { throw 'Repeated Qt patch application changed source.' }
    Write-Output 'PASS ordered Qt patch application and checks are idempotent'
    $brokenPath = Join-Path $fixtureRender 'qohosview.cpp'
    $broken = (Get-Content -LiteralPath $brokenPath -Raw).Replace('void QOhosView::showImmediate()', 'void QOhosView::unrecognizedShowImmediate()')
    Set-Content -LiteralPath $brokenPath -Value $broken -Encoding utf8 -NoNewline
    $beforeFailure = FileSnapshot
    $failed = $false
    try { & (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $fixtureRoot -CheckOnly } catch { $failed = $true }
    if (-not $failed -or (FileSnapshot) -cne $beforeFailure) { throw 'Mismatched Qt source was accepted or modified.' }
    Write-Output 'PASS mismatched Qt source is rejected without modification'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup target.' }
    if ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fixture cleanup cannot traverse a reparse point.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
