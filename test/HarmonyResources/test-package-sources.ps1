$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$scriptPath = Join-Path $projectRoot 'deploy/harmonyos/package.ps1'
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw 'The package script must parse without errors.' }
$function = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Remove-QgcLooseQmlSources'
}, $false)
if (-not $function) { throw 'Packaging must remove the loose application source tree after dependency deployment.' }
Invoke-Expression $function.Extent.Text
$guard = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-NoReparse'
}, $false)
Invoke-Expression $guard.Extent.Text
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
$hvigorDependenciesPattern = '(?!)'
$fixtureRoot = Join-Path $tmpRoot "verification/package-sources-$PID"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $loose = Join-Path $fixtureRoot 'entry/src/main/resources/rawfile/qml'
    $keep = @{
        'entry/libs/arm64-v8a/libQGroundControl.so' = 'compiled application and resources'
        'entry/libs/arm64-v8a/libQt6Quick.so' = 'Qt dependency'
        'entry/src/main/resources/resfile/qml/QtQuick/qmldir' = 'Qt QML module'
        'entry/src/main/resources/rawfile/third_party/ffmpeg/COPYING.LGPLv2.1' = 'license'
        'entry/src/main/resources/base/profile/qgc_start_window.json' = 'startup'
    }
    foreach ($relative in @('src/main.cc', 'MainWindow.qml', 'FirmwarePlugin/APM/apm.pdef.xml', 'Viewer3D/node.mesh')) {
        $path = Join-Path $loose $relative
        New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
        Set-Content -LiteralPath $path -Value 'loose duplicate'
    }
    foreach ($relative in $keep.Keys) {
        $path = Join-Path $fixtureRoot $relative
        New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
        Set-Content -LiteralPath $path -Value $keep[$relative] -NoNewline
    }
    Remove-QgcLooseQmlSources $fixtureRoot
    Remove-QgcLooseQmlSources $fixtureRoot
    if (Test-Path -LiteralPath $loose) { throw 'Loose application files remained in the package.' }
    foreach ($relative in $keep.Keys) {
        if ((Get-Content -LiteralPath (Join-Path $fixtureRoot $relative) -Raw) -cne $keep[$relative]) {
            throw "Required runtime content changed: $relative"
        }
    }
    Write-Output 'PASS loose source removal is idempotent and preserves libraries, QML imports, licenses and startup resources'

    # A junction must fail before deletion, leaving its target intact.
    New-Item -ItemType Directory -Path $loose -Force | Out-Null
    $target = Join-Path $fixtureRoot 'protected-target'
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $target 'keep.txt') -Value 'keep'
    $link = Join-Path $loose 'linked'
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    try {
        $rejected = $false
        try { Remove-QgcLooseQmlSources $fixtureRoot } catch { $rejected = $true }
        if (-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $target 'keep.txt'))) {
            throw 'Source cleanup must reject reparse points without touching the target.'
        }
    } finally { [IO.Directory]::Delete($link) }
    Write-Output 'PASS reparse-point cleanup is rejected before mutation'
    $rejected = $false
    try { Remove-QgcLooseQmlSources $projectRoot } catch { $rejected = $true }
    if (-not $rejected) { throw 'Source cleanup must reject paths outside project tmp.' }
    Write-Output 'PASS cleanup rejects the repository source tree'

    $source = Get-Content -LiteralPath $scriptPath -Raw
    $deploy = $source.IndexOf('& $harmonyDeployQt @stageArguments')
    $cleanup = $source.IndexOf('Remove-QgcLooseQmlSources $packageRoot')
    $build = $source.IndexOf('& $DevEcoCli build')
    if ($deploy -lt 0 -or $cleanup -le $deploy -or $build -le $cleanup) {
        throw 'Cleanup must run after dependency scanning/deployment and before the HAP compiler.'
    }
    Write-Output 'PASS dependency deployment precedes source cleanup and HAP compilation'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    if (-not $resolved.StartsWith($tmpRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
