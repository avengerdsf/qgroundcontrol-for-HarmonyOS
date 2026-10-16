$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw ($parseErrors | Out-String) }
$hookFunction = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-QgcWindowPolicyHooks'
}, $false)
if (-not $hookFunction) { throw 'Packaging must define Set-QgcWindowPolicyHooks for reproducible QAbility integration.' }
Invoke-Expression $hookFunction.Extent.Text
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Fixture write escaped its project tmp directory.'
    }
}
$fixtureRoot = Join-Path $projectRoot "tmp/verification/window-policy-fixtures-$PID"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures/QAbility.ets') -Raw
    $abilityPath = Join-Path $fixtureRoot 'QAbility.ets'
    Set-Content -LiteralPath $abilityPath -Value $source -Encoding utf8 -NoNewline
    Set-QgcWindowPolicyHooks $abilityPath
    $once = Get-Content -LiteralPath $abilityPath -Raw
    Set-QgcWindowPolicyHooks $abilityPath
    if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $once) { throw 'Window policy staging must be idempotent.' }
    if ([regex]::Matches($once, "import \{ QGCWindowPolicy \}").Count -ne 1 -or
        [regex]::Matches($once, 'new QGCWindowPolicy\(\)').Count -ne 1 -or
        [regex]::Matches($once, 'qgcWindowPolicy.prepare\(windowStage,').Count -ne 2 -or
        [regex]::Matches($once, 'qgcWindowPolicy.destroy\(\)').Count -ne 2 -or
        [regex]::Matches($once, 'qgcWindowPolicy.foreground\(\)').Count -ne 1 -or
        [regex]::Matches($once, 'qgcWindowPolicy.background\(\)').Count -ne 1) {
        throw 'Lifecycle hooks are missing or duplicated.'
    }
    if ([regex]::Matches($once, 'handleAbilityOnWindowStageCreate\(this, windowStage\);\n    \}, this.context\);').Count -ne 1 -or
        [regex]::Matches($once, 'handleAbilityOnWindowStageRestore\(this, windowStage\);\n    \}\);').Count -ne 1) {
        throw 'Native create and restore must preserve the saved Qt window state.'
    }
    foreach ($ending in @('});', '}, true);')) {
        $previous = $once.Replace("qpa.handleAbilityOnWindowStageCreate(this, windowStage);`n    }, this.context);",
            "qpa.handleAbilityOnWindowStageCreate(this, windowStage);`n    $ending")
        $previous = $previous.Replace("    this.qgcWindowPolicy.background();`n", '')
        Set-Content -LiteralPath $abilityPath -Value $previous -Encoding utf8 -NoNewline
        Set-QgcWindowPolicyHooks $abilityPath
        if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $once) { throw 'The previous complete create hook must upgrade exactly.' }
        Set-QgcWindowPolicyHooks $abilityPath
        if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $once) { throw 'An upgraded create hook must remain idempotent.' }
    }
    Write-Output 'PASS previous complete create hook upgrades exactly once'
    $legacyBackground = $once.Replace("    this.qgcWindowPolicy.background();`n", '')
    Set-Content -LiteralPath $abilityPath -Value $legacyBackground -Encoding utf8 -NoNewline
    Set-QgcWindowPolicyHooks $abilityPath
    if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $once) { throw 'Legacy background hook must upgrade exactly.' }
    Set-QgcWindowPolicyHooks $abilityPath
    if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $once) { throw 'Upgraded background hook must remain idempotent.' }
    $withSuper = $source.Replace('    qpa.handleAbilityOnBackground(this);', "    super.onBackground();`n    qpa.handleAbilityOnBackground(this);")
    Set-Content -LiteralPath $abilityPath -Value $withSuper -Encoding utf8 -NoNewline
    Set-QgcWindowPolicyHooks $abilityPath
    $stagedWithSuper = Get-Content -LiteralPath $abilityPath -Raw
    if (-not $stagedWithSuper.Contains("    super.onBackground();`n    this.qgcWindowPolicy.background();`n    qpa.handleAbilityOnBackground(this);")) {
        throw 'Background staging must preserve superclass and Qt lifecycle calls.'
    }
    Write-Output 'PASS background hook upgrades preserve superclass and Qt lifecycle calls'
    Set-Content -LiteralPath (Join-Path $projectRoot 'tmp/verification/window-policy-QAbility.ets') -Value $once -Encoding utf8 -NoNewline
    Write-Output 'PASS QAbility lifecycle hooks stage exactly once'
    $badTemplates = @(
        $source.Replace('    qpa.handleAbilityOnWindowStageCreate(this, windowStage);', ''),
        $source.Replace('    qpa.handleAbilityOnForeground(this);', "    qpa.handleAbilityOnForeground(this);`n    qpa.handleAbilityOnForeground(this);"),
        $once.Replace('qgcWindowPolicy.prepare(windowStage,', 'qgcWindowPolicy.prepare(this.launchWant,'),
        $source.Replace('    qpa.handleAbilityOnBackground(this);', ''),
        $once.Replace('    this.qgcWindowPolicy.background();', "    this.qgcWindowPolicy.background();`n    this.qgcWindowPolicy.background();"),
        $once.Replace("    this.qgcWindowPolicy.background();`n    qpa.handleAbilityOnBackground(this);", "    this.qgcWindowPolicy.background();`n    super.onBackground();`n    qpa.handleAbilityOnBackground(this);")
    )
    foreach ($bad in $badTemplates) {
        Set-Content -LiteralPath $abilityPath -Value $bad -Encoding utf8 -NoNewline
        $failed = $false
        try { Set-QgcWindowPolicyHooks $abilityPath } catch { $failed = $true }
        if (-not $failed) { throw 'Missing, duplicate or partial hooks must fail staging.' }
        if ((Get-Content -LiteralPath $abilityPath -Raw) -cne $bad) { throw 'Invalid template was partially written.' }
    }
    Write-Output 'PASS missing duplicate and partial Qt template hooks fail without writing'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
