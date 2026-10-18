$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw ($parseErrors | Out-String) }
$validators = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.IfStatementAst] -and
        $node.Extent.Text -match '^if \(\(\$(deviceTypes|stagedTypes) -join'
}, $false))
if ($validators.Count -ne 2) { throw 'Both deployment and staged-module device types must be validated.' }
$cases = @(
    @{ Types = @('tablet', 'phone', '2in1'); Valid = $true },
    @{ Types = @('tablet', '2in1'); Valid = $false },
    @{ Types = @('phone'); Valid = $false },
    @{ Types = @('phone', 'tablet', '2in1', 'phone'); Valid = $false },
    @{ Types = @('phone', 'tablet', '2in1', 'tv'); Valid = $false }
)
foreach ($validator in $validators) {
    foreach ($case in $cases) {
        $deviceTypes = @($case.Types | Sort-Object)
        $stagedTypes = $deviceTypes
        $accepted = $true
        try { Invoke-Expression $validator.Extent.Text } catch { $accepted = $false }
        if ($accepted -ne $case.Valid) { throw "Incorrect package acceptance for device types $($case.Types -join ',')." }
    }
}
Write-Output 'PASS deployment and staged metadata require exactly phone, tablet and 2in1'
