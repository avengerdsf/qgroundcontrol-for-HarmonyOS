param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
$fixtureRoot = Join-Path $projectRoot "tmp/verification/back-press-fixtures-$PID"
if (Test-Path -LiteralPath $fixtureRoot) { throw 'Back fixtures must use a new owned directory.' }
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
function Assert-NoReparse([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($fixtureRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            ((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Back fixture path escaped its owned project tmp directory.'
    }
}
try {
    $source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/MainWindowNativeNode.ets')).Replace("`r`n", "`n")
    $page = Join-Path $fixtureRoot 'MainWindowNativeNode.ets'
    Set-Content -LiteralPath $page -Value $source -Encoding utf8 -NoNewline
    if ($Baseline) {
        & "$env:NODE_HOME/node.exe" --max-old-space-size=256 (Join-Path $PSScriptRoot 'check-callback.cjs') $page
        if ($LASTEXITCODE -ne 0) { throw 'Baseline Qt main page leaves system Back to default ArkUI handling.' }
        throw 'Expected the unmodified Qt page to fail the Back regression check.'
    }
    $tokens = $null; $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw 'Package script failed PowerShell parsing.' }
    $hook = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcMainPageBackPress'
    }, $false)
    if (-not $hook) { throw 'Production packaging must stage the main page Back handler.' }
    . ([scriptblock]::Create($hook.Extent.Text))
    Set-QgcMainPageBackPress $page
    $once = [IO.File]::ReadAllText($page)
    & "$env:NODE_HOME/node.exe" --max-old-space-size=256 (Join-Path $PSScriptRoot 'check-callback.cjs') $page
    if ($LASTEXITCODE -ne 0) { throw 'Staged Back callback regression failed.' }
    Set-QgcMainPageBackPress $page
    if ([IO.File]::ReadAllText($page) -cne $once) { throw 'Back staging must be idempotent.' }
    Write-Output 'PASS existing staged page retains one handler on restage'
    $handler = [regex]::Match($once, '(?m)^  onBackPress\([^\n]*\)\s*:\s*boolean\s*\{[\s\S]*?^  \}\n\n').Value
    if (-not $handler -or $once.Replace($handler, '') -cne $source) { throw 'Back staging changed the Qt surface, callbacks, or other page source.' }
    Write-Output 'PASS staging preserves every other Qt page method and surface binding'
    Set-Content -LiteralPath $page -Value $source.Replace("`n", "`r`n") -Encoding utf8 -NoNewline
    Set-QgcMainPageBackPress $page
    if ([IO.File]::ReadAllText($page) -cne $once) { throw 'CRLF Qt template did not stage equivalently.' }
    Write-Output 'PASS CRLF and LF Qt pages receive the same handler'
    $anchor = '  aboutToAppear(): void {'
    $badPages = @(
        $source.Replace($anchor, '  changedLifecycle(): void {'),
        $source.Replace($anchor, "$anchor`n$anchor"),
        $source.Replace($anchor, "  onBackPress(): boolean { return false; }`n`n$anchor"),
        $once.Replace('return true;', 'return false;'),
        $source.Replace('struct WindowNativeNode {', 'struct UnexpectedPage {')
    )
    foreach ($bad in $badPages) {
        Set-Content -LiteralPath $page -Value $bad -Encoding utf8 -NoNewline
        $rejected = $false
        try { Set-QgcMainPageBackPress $page } catch { $rejected = $true }
        if (-not $rejected) { throw 'An unsupported or conflicting Qt page was accepted.' }
        if ([IO.File]::ReadAllText($page) -cne $bad) { throw 'A rejected Qt page was partially written.' }
    }
    Write-Output 'PASS five missing, duplicate, conflicting, partial and wrong-page inputs fail without writing'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
            ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Invalid Back fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
