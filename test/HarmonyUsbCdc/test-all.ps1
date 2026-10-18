param([switch]$IncludeUpstream)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ($env:QGC_TEST_FILTER -or $env:QGC_BRIDGE_SOURCE) {
    throw 'Run the full USB suite without QGC_TEST_FILTER or QGC_BRIDGE_SOURCE overrides.'
}
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
Push-Location $projectRoot
try {
    foreach ($script in @('cdc-descriptors-tests.cjs', 'cp210x-tests.cjs', 'ch34x-tests.cjs',
            'ftdi-tests.cjs', 'run-tests.cjs')) {
        & "$env:NODE_HOME/node.exe" --max-old-space-size=256 (Join-Path $PSScriptRoot $script)
        if ($LASTEXITCODE -ne 0) { throw "USB serial suite failed: $script" }
    }
    & "$env:NODE_HOME/node.exe" --max-old-space-size=256 (Join-Path $PSScriptRoot 'run-tests.cjs') --sdk-types
    if ($LASTEXITCODE -ne 0) { throw 'USB serial SDK declaration check failed.' }
    if ($IncludeUpstream) { & (Join-Path $PSScriptRoot 'run-upstream-tests.ps1') }
} finally {
    Pop-Location
}
