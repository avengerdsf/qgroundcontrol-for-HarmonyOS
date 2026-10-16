$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw ($parseErrors | Out-String) }
foreach ($functionName in @('Set-QgcWindowCloseHooks', 'Set-QgcWindowPolicyHooks')) {
    $functionAst = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
    }, $false)
    if (-not $functionAst) { throw "Missing staging function: $functionName" }
    Invoke-Expression $functionAst.Extent.Text
}
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture write escaped project tmp.' }
}
$fixtureRoot = Join-Path $projectRoot "tmp/verification/window-close-fixtures-$PID"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $source = Join-Path $projectRoot 'tmp/toolchains/Qt/6.12.0/harmonyos_arm64_v8a/src/harmonyos/templates/entry/src/main/ets/qability/QAbility.ets'
    $ability = Join-Path $fixtureRoot 'QAbility.ets'
    Copy-Item -LiteralPath $source -Destination $ability
    Set-QgcWindowPolicyHooks $ability
    Set-QgcWindowCloseHooks $ability
    $once = Get-Content -LiteralPath $ability -Raw
    Set-QgcWindowPolicyHooks $ability
    Set-QgcWindowCloseHooks $ability
    if ((Get-Content -LiteralPath $ability -Raw) -cne $once) { throw 'Policy and close-request staging must remain idempotent together.' }
    Write-Output 'PASS close-request staging preserves existing policy hooks and is idempotent'
    & 'D:/Progrem/Huawei/DevEco Studio/tools/node/node.exe' --max-old-space-size=512 (Join-Path $PSScriptRoot 'test-close-ability.cjs') $ability
    if ($LASTEXITCODE -ne 0) { throw 'Production staged QAbility close-request runtime tests failed.' }
    $broken = $once.Replace('this.context.eventHub.off(QGC_WINDOW_CLOSE_REQUEST, this.qgcCloseRequested);', '')
    Set-Content -LiteralPath $ability -Value $broken -Encoding utf8 -NoNewline
    $failed = $false
    try { Set-QgcWindowCloseHooks $ability } catch { $failed = $true }
    if (-not $failed -or (Get-Content -LiteralPath $ability -Raw) -cne $broken) { throw 'Partial close-request hooks were accepted or modified.' }
    Write-Output 'PASS partial close-request hooks fail before modification'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup target.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
