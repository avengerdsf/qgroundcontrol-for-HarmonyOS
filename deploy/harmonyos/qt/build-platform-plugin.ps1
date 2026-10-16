param(
    [string]$BuildDir = '',
    [switch]$PlanOnly,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot 'tmp/qt-build/x86_64' }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
function AssertProjectTmpPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($tmpRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "The Qt build and kit paths must be inside this project's tmp directory: $absolute"
    }
    $item = Get-Item -LiteralPath $absolute
    while ($true) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "The Qt path must not traverse a junction or symbolic link: $($item.FullName)"
        }
        if ($item.FullName -eq $tmpRoot) { break }
        $item = $item.Parent
    }
}
AssertProjectTmpPath $BuildDir
$cache = Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt')
function ReadCache([string]$Name) {
    $entry = $cache | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=(.*)$') }
    if (-not $entry) { throw "Missing CMake cache entry: $Name" }
    return $entry.Substring($entry.IndexOf('=') + 1)
}
$sourceRoot = ReadCache 'CMAKE_HOME_DIRECTORY'
$kitRoot = ReadCache 'CMAKE_INSTALL_PREFIX'
$ninja = ReadCache 'CMAKE_MAKE_PROGRAM'
$cmake = Join-Path (Split-Path $ninja) 'cmake.exe'
AssertProjectTmpPath $sourceRoot
AssertProjectTmpPath $kitRoot
& (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $sourceRoot -CheckOnly:$PlanOnly
. (Join-Path $PSScriptRoot '../deveco-env.ps1')
if ($PlanOnly) {
    & $ninja -C $BuildDir -n QOhosPlatformIntegrationPlugin
    if ($LASTEXITCODE -ne 0) { throw 'Qt platform plugin build planning failed.' }
    return
}
& $cmake --build $BuildDir --target QOhosPlatformIntegrationPlugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Qt platform plugin build failed.' }
$plugin = Join-Path $BuildDir 'qtbase/plugins/platforms/libqohos.so'
if (-not (Test-Path -LiteralPath $plugin)) { throw "The built plugin is missing: $plugin" }
if ($Install) {
    $installedPlugin = Join-Path $kitRoot 'plugins/platforms/libqohos.so'
    $backupDir = Join-Path $tmpRoot ('qt-plugin-backups/' + (Split-Path $BuildDir -Leaf) + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    Copy-Item -LiteralPath $installedPlugin -Destination (Join-Path $backupDir 'libqohos.so')
    Copy-Item -LiteralPath $plugin -Destination $installedPlugin
    Write-Output "Installed plugin: $installedPlugin"
    Write-Output "Original plugin: $(Join-Path $backupDir 'libqohos.so')"
}
Get-FileHash -LiteralPath $plugin -Algorithm SHA256
