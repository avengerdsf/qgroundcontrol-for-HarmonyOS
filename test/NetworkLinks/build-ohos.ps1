#requires -Version 7.0
param(
    [switch]$SkipNativeBuild,
    [ValidateRange(1, 2147483647)]
    [long]$VersionCode = 5010513
)

$ErrorActionPreference = 'Stop'
$networkProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
$networkBuildRoot = "$networkProjectRoot/tmp/build-network-links-ohos-x86_64"
. "$networkProjectRoot/deploy/harmonyos/deveco-env.ps1"
$env:NODE_OPTIONS = '--max-old-space-size=512'
$env:_JAVA_OPTIONS = '-Xmx512m'
$cmake = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/cmake.exe"
$qtTarget = "$networkProjectRoot/tmp/toolchains/Qt/6.12.0/harmonyos_x86_64"
$qtHost = "$networkProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
foreach ($relative in @('', '/harmonyos-build')) {
    $candidate = $networkBuildRoot + $relative
    if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Network test output must not be a link: $candidate"
    }
}
if (-not $SkipNativeBuild) {
    & $cmake -S $PSScriptRoot -B $networkBuildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
        "-DCMAKE_TOOLCHAIN_FILE=$qtTarget/lib/cmake/Qt6/qt.toolchain.cmake" `
        "-DQT_HOST_PATH=$qtHost" "-DOHOS_SDK_ROOT=$env:OHOS_SDK_ROOT" '-DOHOS_ARCH=x86_64' `
        '-DQGC_ENABLE_BLUETOOTH=OFF' '-DQGC_NO_SERIAL_LINK=ON'
    if ($LASTEXITCODE -ne 0) { throw 'Network test configuration failed.' }
    & $cmake --build $networkBuildRoot --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'Network test native build failed.' }
}
& "$networkProjectRoot/deploy/harmonyos/package.ps1" -Architecture x86_64 -Action Stage `
    -BuildDirectory $networkBuildRoot -BundleName 'org.mavlink.qgc.network.tests' -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw 'Network test staging failed.' }
$packageRoot = "$networkBuildRoot/harmonyos-build"
$abilityPath = "$packageRoot/entry/src/main/ets/qability/QAbility.ets"
$ability = Get-Content -LiteralPath $abilityPath -Raw
$reuseDefault = "want.parameters['io.qt.useDefaultUiAbilityInstanceInQt'] = true;"
if (-not $ability.Contains($reuseDefault)) {
    $createCall = 'qpa.handleAbilityOnCreate(this, want, launchParam);'
    if ([regex]::Matches($ability, [regex]::Escape($createCall)).Count -ne 1) {
        throw 'Unexpected network test QAbility startup hook.'
    }
    $ability = $ability.Replace($createCall,
        "want.parameters = want.parameters || {};`n    $reuseDefault`n    this.launchWant = want;`n    $createCall")
    Set-Content -LiteralPath $abilityPath -Value $ability -Encoding utf8 -NoNewline
}
$configPath = "$packageRoot/hvigor/hvigor-config.json5"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$config.nodeOptions.maxOldSpaceSize = 512
$config.execution.daemon = $false
$config.execution.parallel = $false
$config | Add-Member -NotePropertyName javaOptions -NotePropertyValue @{ Xmx = 512 } -Force
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8
$env:NODE_OPTIONS = '--max-old-space-size=512'
$env:_JAVA_OPTIONS = '-Xmx512m'
Push-Location $packageRoot
try {
    & 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' build --modules entry --build-mode debug
    if ($LASTEXITCODE -ne 0) { throw 'Network test ArkTS/HAP build failed.' }
} finally {
    Pop-Location
}
Write-Output "Network test project: $packageRoot"
