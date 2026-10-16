$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw 'The package script must parse without errors.' }
$definition = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcStartWindow'
}, $false)
if (-not $definition) { throw 'Packaging must stage the official hidden starting-window profile.' }
Invoke-Expression $definition.Extent.Text
$fixtureRoot = Join-Path $projectRoot "tmp/verification/start-window-fixtures-$PID"
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture escaped project tmp.' }
}
New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'resources/base/profile') -Force | Out-Null
try {
    $modulePath = Join-Path $fixtureRoot 'module.json5'
    $source = '{"module":{"abilities":[{"name":"QAbility","icon":"$media:qgc_official","startWindowIcon":"$media:default","startWindowBackground":"$color:start_window_background"}]}}'
    Set-Content -LiteralPath $modulePath -Value $source -Encoding utf8 -NoNewline
    Set-QgcStartWindow $modulePath
    $once = Get-Content -LiteralPath $modulePath -Raw
    Set-QgcStartWindow $modulePath
    if ((Get-Content -LiteralPath $modulePath -Raw) -cne $once) { throw 'Starting-window staging must be idempotent.' }
    $ability = ($once | ConvertFrom-Json).module.abilities[0]
    if ($ability.startWindow -cne '$profile:qgc_start_window' -or $ability.startWindowIcon -cne $ability.icon -or
        $ability.startWindowBackground -cne '$color:start_window_background') { throw 'Module must retain official icon/background fallback.' }
    $profile = Get-Content -LiteralPath (Join-Path $fixtureRoot 'resources/base/profile/qgc_start_window.json') -Raw | ConvertFrom-Json
    if ($profile.startWindowType -cne 'REQUIRED_HIDE' -or $profile.startWindowAppIcon -cne $ability.icon -or
        $profile.startWindowBackgroundColor -cne $ability.startWindowBackground) { throw 'Hidden starting-window profile requires valid icon/background resources.' }
    Write-Output 'PASS hidden starting-window profile retains official icon and required background'
    foreach ($bad in @($source.Replace('"startWindowBackground":"$color:start_window_background"', '"startWindowBackground":""'),
        $source.Replace('"startWindowBackground":"$color:start_window_background"', '"startWindowBackground":"$media:wrong"'))) {
        Set-Content -LiteralPath $modulePath -Value $bad -Encoding utf8 -NoNewline
        $failed = $false
        try { Set-QgcStartWindow $modulePath } catch { $failed = $true }
        if (-not $failed -or (Get-Content -LiteralPath $modulePath -Raw) -cne $bad) { throw 'Invalid startup resources must fail before writing.' }
    }
    Write-Output 'PASS invalid starting-window backgrounds fail before writing'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    if (-not $resolved.StartsWith([IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
