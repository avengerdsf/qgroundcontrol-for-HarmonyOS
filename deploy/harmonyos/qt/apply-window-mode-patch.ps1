param(
    [string]$SourceRoot = '',
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $SourceRoot) {
    $SourceRoot = Join-Path $projectRoot 'tmp/qt-src/qt-everywhere-src-6.12.0'
}
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
if (-not $SourceRoot.StartsWith($tmpRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The Qt source directory must be inside this project''s tmp directory.'
}
$sourceItem = Get-Item -LiteralPath $SourceRoot
while ($true) {
    if ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "The Qt source path must not traverse a junction or symbolic link: $($sourceItem.FullName)"
    }
    if ($sourceItem.FullName -eq $tmpRoot) { break }
    $sourceItem = $sourceItem.Parent
}
$relativeSource = [IO.Path]::GetRelativePath($projectRoot, $SourceRoot).Replace('\', '/')
foreach ($patchName in @('0001-respect-window-mode-on-system-sync.patch', '0002-restore-tablet-startup-window-state.patch')) {
    $patch = Join-Path $PSScriptRoot $patchName
    $check = & git -C $projectRoot apply --check "--directory=$relativeSource" $patch 2>&1
    if ($LASTEXITCODE -eq 0) {
        if ($CheckOnly) {
            Write-Output "Qt patch can be applied: $patchName"
            continue
        }
        & git -C $projectRoot apply "--directory=$relativeSource" $patch
        if ($LASTEXITCODE -ne 0) { throw "Applying the Qt patch failed: $patchName" }
        Write-Output "Qt patch applied: $patchName"
        continue
    }
    $reverseCheck = & git -C $projectRoot apply --reverse --check "--directory=$relativeSource" $patch 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Output "Qt patch is already applied: $patchName"
        continue
    }
    throw "The Qt sources do not match patch $patchName.`n$check`n$reverseCheck"
}
