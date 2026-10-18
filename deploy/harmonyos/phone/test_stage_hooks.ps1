$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $repository 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$errors)
if ($errors) { throw ($errors | Out-String) }
$function = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcWindowPolicyHooks'
}, $false)
if (-not $function) { throw 'Phone packaging lifecycle function is missing.' }
Invoke-Expression $function.Extent.Text
$fixtureRoot = Join-Path $repository "tmp/verification/phone-stage-hooks-$PID"
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Fixture write escaped its project tmp directory.'
    }
}
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
try {
    $path = Join-Path $fixtureRoot 'QAbility.ets'
    $source = Get-Content (Join-Path $repository 'test/HarmonyWindowPolicy/fixtures/QAbility.ets') -Raw
    Set-Content -LiteralPath $path -Value $source -Encoding utf8 -NoNewline
    Set-QgcWindowPolicyHooks $path
    $once = Get-Content -LiteralPath $path -Raw
    if ([regex]::Matches($once, 'qgcWindowPolicy.background\(\);').Count -ne 1 -or
        -not $once.Contains("this.qgcWindowPolicy.background();`n    qpa.handleAbilityOnBackground(this);")) {
        throw 'Phone background callback must release its keep-screen-on flag before Qt background handling.'
    }
    Set-QgcWindowPolicyHooks $path
    if ((Get-Content -LiteralPath $path -Raw) -cne $once) { throw 'Phone lifecycle staging is not idempotent.' }
    $legacy = $once.Replace("    this.qgcWindowPolicy.background();`n", '')
    Set-Content -LiteralPath $path -Value $legacy -Encoding utf8 -NoNewline
    Set-QgcWindowPolicyHooks $path
    if ((Get-Content -LiteralPath $path -Raw) -cne $once) { throw 'Previously staged QAbility did not upgrade exactly.' }
    $invalid = $source.Replace('qpa.handleAbilityOnBackground(this);', '')
    Set-Content -LiteralPath $path -Value $invalid -Encoding utf8 -NoNewline
    $failed = $false
    try { Set-QgcWindowPolicyHooks $path } catch { $failed = $true }
    if (-not $failed -or (Get-Content -LiteralPath $path -Raw) -cne $invalid) {
        throw 'A missing background anchor must fail without writing a partial stage.'
    }
    Write-Output 'PASS phone background hooks: fresh, repeated, legacy upgrade and invalid template'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $repository 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
