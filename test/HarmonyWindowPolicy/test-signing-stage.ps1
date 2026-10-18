$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'deploy/harmonyos/package.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { throw 'The package script must parse without errors.' }
foreach ($name in @('Get-QgcSigningSnapshot', 'Restore-QgcSigningSnapshot')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    if (-not $definition) { throw 'Packaging must preserve signing fields across Qt staging.' }
    Invoke-Expression $definition.Extent.Text
}
$fixtureRoot = Join-Path $projectRoot "tmp/verification/signing-stage-fixtures-$PID"
function Assert-NoReparse([string]$Path) {
    if (-not $Path.StartsWith($fixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Fixture path escaped the project tmp directory.'
    }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    if (($Actual | ConvertTo-Json -Depth 32 -Compress) -cne ($Expected | ConvertTo-Json -Depth 32 -Compress)) {
        throw $Message
    }
}
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    $profilePath = Join-Path $fixtureRoot 'build-profile.json5'
    $old = [pscustomobject]@{
        app = [pscustomobject]@{
            signingConfigs = @(
                [pscustomobject]@{ name = 'development'; material = [pscustomobject]@{ keyPassword = 'fixture-only-secret'; certpath = 'E:/fixture.cer' } },
                [pscustomobject]@{ name = 'release'; material = [pscustomobject]@{ keyPassword = 'fixture-only-release' } }
            )
            products = @(
                [pscustomobject]@{ name = 'alpha'; signingConfig = 'development'; compatibleSdkVersion = 'old-sdk' },
                [pscustomobject]@{ name = 'beta'; signingConfig = 'release' },
                [pscustomobject]@{ name = 'removed'; signingConfig = 'development' }
            )
        }
        modules = @([pscustomobject]@{ name = 'old-module'; srcPath = './old' })
    }
    $generated = [pscustomobject]@{
        app = [pscustomobject]@{
            signingConfigs = @()
            products = @(
                [pscustomobject]@{ name = 'beta'; signingConfig = ''; compatibleSdkVersion = 'new-sdk' },
                [pscustomobject]@{ name = 'alpha'; targetSdkVersion = 'new-target' },
                [pscustomobject]@{ name = 'new-product'; signingConfig = 'generated' },
                [pscustomobject]@{ name = 'Alpha'; signingConfig = 'case-sensitive' }
            )
            buildModeSet = @([pscustomobject]@{ name = 'debug'; generated = $true })
        }
        modules = @([pscustomobject]@{ name = 'entry'; srcPath = './new-entry'; generated = $true })
    }
    Set-Content -LiteralPath $profilePath -Value ($old | ConvertTo-Json -Depth 32) -Encoding utf8 -NoNewline
    $snapshot = Get-QgcSigningSnapshot $profilePath
    $generatedText = $generated | ConvertTo-Json -Depth 32
    Set-Content -LiteralPath $profilePath -Value $generatedText -Encoding utf8 -NoNewline
    $output = @(Restore-QgcSigningSnapshot $profilePath $snapshot)
    Assert-Equal $output.Count 0 'Signing restoration must not emit signing data.'
    $restored = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
    Assert-Equal $restored.app.signingConfigs $old.app.signingConfigs 'Signing configurations were not restored.'
    Assert-Equal $restored.app.products[0].signingConfig 'release' 'The beta signing reference was not restored by name.'
    Assert-Equal $restored.app.products[1].signingConfig 'development' 'The alpha signing reference was not restored by name.'
    Assert-Equal $restored.app.products[2].signingConfig 'generated' 'A new product inherited another product signing reference.'
    Assert-Equal $restored.app.products[3].signingConfig 'case-sensitive' 'Different product names were merged.'
    Assert-Equal $restored.app.products.Count 4 'A removed product was added back.'
    Assert-Equal $restored.app.products[0].compatibleSdkVersion 'new-sdk' 'Generated compatible SDK was overwritten.'
    Assert-Equal $restored.app.products[1].targetSdkVersion 'new-target' 'Generated target SDK was overwritten.'
    Assert-Equal $restored.modules $generated.modules 'Generated modules were overwritten.'
    Assert-Equal $restored.app.buildModeSet $generated.app.buildModeSet 'Generated build modes were overwritten.'
    Write-Output 'PASS staging restores signing fields by product name and preserves generated configuration'

    foreach ($oldText in @('{"app":{"products":[]}}', '{"app":{"signingConfigs":[],"products":[]}}')) {
        Set-Content -LiteralPath $profilePath -Value $oldText -Encoding utf8 -NoNewline
        $empty = Get-QgcSigningSnapshot $profilePath
        Assert-Equal $empty $null 'An unsigned profile must not create a signing snapshot.'
        Set-Content -LiteralPath $profilePath -Value $generatedText -Encoding utf8 -NoNewline
        Restore-QgcSigningSnapshot $profilePath $empty
        Assert-Equal (Get-Content -LiteralPath $profilePath -Raw) $generatedText 'An unsigned profile changed generated output.'
    }
    Assert-Equal (Get-QgcSigningSnapshot (Join-Path $fixtureRoot 'missing.json5')) $null 'A missing profile must not create a signing snapshot.'
    Write-Output 'PASS absent empty and missing signing configurations leave generated output unchanged'

    $json5Text = @'
{
  // A valid JSON5 signing fixture.
  app: {
    signingConfigs: [{ name: 'development', material: { keyPassword: 'fixture-only-secret', }, }],
    products: [{ name: 'alpha', signingConfig: 'development', }, ],
  },
}
'@
    Set-Content -LiteralPath $profilePath -Value $json5Text -Encoding utf8 -NoNewline
    $snapshot = Get-QgcSigningSnapshot $profilePath
    Set-Content -LiteralPath $profilePath -Value $generatedText -Encoding utf8 -NoNewline
    Restore-QgcSigningSnapshot $profilePath $snapshot
    $restored = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
    Assert-Equal $restored.app.signingConfigs.Count 1 'Valid JSON5 signing material was not restored.'
    Assert-Equal $restored.app.products[1].signingConfig 'development' 'A JSON5 product signing reference was not restored.'
    Write-Output 'PASS JSON5 comments single quotes unquoted keys and trailing commas are accepted'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp/verification')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
