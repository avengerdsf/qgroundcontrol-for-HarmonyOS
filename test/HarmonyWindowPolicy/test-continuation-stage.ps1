$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw ($parseErrors | Out-String) }
$functionAst = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcContinuationDisabled'
}, $false)
if (-not $functionAst) { throw 'Packaging must explicitly disable application continuation.' }
Invoke-Expression $functionAst.Extent.Text
$fixtureRoot = Join-Path $projectRoot "tmp/verification/continuation-fixtures-$PID"
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture write escaped project tmp.' }
}
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $module = Join-Path $fixtureRoot 'module.json5'
    foreach ($value in @('true', 'false')) {
        $source = '{ "module": { "abilities": [{ "name": "QAbility", "continuable": ' + $value + ' }] } }'
        Set-Content -LiteralPath $module -Value $source -Encoding utf8 -NoNewline
        Set-QgcContinuationDisabled $module
        $once = Get-Content -LiteralPath $module -Raw
        if (($once | ConvertFrom-Json).module.abilities[0].continuable -ne $false) { throw 'Continuation remains enabled.' }
        Set-QgcContinuationDisabled $module
        if ((Get-Content -LiteralPath $module -Raw) -cne $once) { throw 'Continuation staging must be idempotent.' }
    }
    Write-Output 'PASS enabled and disabled Qt continuation declarations stage to false idempotently'
    foreach ($source in @('{ "module": { "abilities": [] } }', '{ "continuable": true, "continuable": true }')) {
        Set-Content -LiteralPath $module -Value $source -Encoding utf8 -NoNewline
        $failed = $false
        try { Set-QgcContinuationDisabled $module } catch { $failed = $true }
        if (-not $failed -or (Get-Content -LiteralPath $module -Raw) -cne $source) { throw 'Unknown continuation template was accepted or changed.' }
    }
    Write-Output 'PASS missing and duplicate continuation declarations fail before modification'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
