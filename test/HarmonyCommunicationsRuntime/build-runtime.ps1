param([switch]$SkipNativeBuild)

$ErrorActionPreference = 'Stop'
$runtimeProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
$runtimeBuildRoot = "$runtimeProjectRoot/tmp/build-harmony-communications-runtime-x86_64"
. "$runtimeProjectRoot/deploy/harmonyos/deveco-env.ps1"
$env:NODE_OPTIONS = '--max-old-space-size=1536'
$cmake = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/cmake.exe"
$runtimeDependencyRoot = "$runtimeProjectRoot/tmp"
if (-not (Test-Path -LiteralPath "$runtimeDependencyRoot/toolchains/Qt")) {
    $runtimeDependencyRoot = [IO.Path]::GetFullPath((Join-Path $runtimeProjectRoot '../../tmp')).Replace('\', '/')
}
$qtTarget = "$runtimeDependencyRoot/toolchains/Qt/6.12.0/harmonyos_x86_64"
$qtHost = "$runtimeDependencyRoot/toolchains/Qt/6.12.0/mingw_64"
$env:PATH = "$qtHost/bin;$runtimeDependencyRoot/toolchains/Qt/Tools/mingw1310_64/bin;" + $env:PATH
foreach ($relative in @('', '/harmonyos-build')) {
    $candidate = $runtimeBuildRoot + $relative
    if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Runtime output must not be a link: $candidate"
    }
}
if (-not $SkipNativeBuild) {
    & $cmake -S $PSScriptRoot -B $runtimeBuildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
        "-DCMAKE_TOOLCHAIN_FILE=$qtTarget/lib/cmake/Qt6/qt.toolchain.cmake" `
        "-DQT_HOST_PATH=$qtHost" "-DOHOS_SDK_ROOT=$env:OHOS_SDK_ROOT" '-DOHOS_ARCH=x86_64' `
        "-DNodeAddonApi_INCLUDE_DIR=$runtimeDependencyRoot/vcpkg/installed/x64-ohos/include"
    if ($LASTEXITCODE -ne 0) { throw 'Runtime test configuration failed.' }
    & $cmake --build $runtimeBuildRoot --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'Runtime test native build failed.' }
}
& "$runtimeProjectRoot/deploy/harmonyos/package.ps1" -Architecture x86_64 -Action Stage `
    -BuildDirectory $runtimeBuildRoot -BundleName 'org.mavlink.qgc.runtime.tests' -VersionCode 5011501
if ($LASTEXITCODE -ne 0) { throw 'Runtime test staging failed.' }
$packageRoot = "$runtimeBuildRoot/harmonyos-build"
$abilityRoot = "$packageRoot/entry/src/main/ets/qability"
$abilityPath = "$abilityRoot/QAbility.ets"
$ability = Get-Content -LiteralPath $abilityPath -Raw
$reuseDefault = "want.parameters['io.qt.useDefaultUiAbilityInstanceInQt'] = true;"
if (-not $ability.Contains($reuseDefault)) {
    $createCall = 'qpa.handleAbilityOnCreate(this, want, launchParam);'
    if ([regex]::Matches($ability, [regex]::Escape($createCall)).Count -ne 1) {
        throw 'Unexpected runtime QAbility startup hook.'
    }
    $ability = $ability.Replace($createCall,
        "want.parameters = want.parameters || {};`n    $reuseDefault`n    this.launchWant = want;`n    $createCall")
    Set-Content -LiteralPath $abilityPath -Value $ability -Encoding utf8 -NoNewline
}
Copy-Item -LiteralPath "$PSScriptRoot/fixtures/RuntimeFactories.ets" -Destination "$abilityRoot/RuntimeFactories.ets" -Force
$utilsPath = "$abilityRoot/QtUtils.ets"
$utils = Get-Content -LiteralPath "$qtTarget/src/harmonyos/templates/entry/src/main/ets/qability/QtUtils.ets" -Raw
$utils = "import { getQGCBluetoothBridge as getRuntimeSppBridge, getQGCBleBridge as getRuntimeBleBridge, getRuntimeControl } from './RuntimeFactories';`n" + $utils
if ([regex]::Matches($utils, 'return factories;').Count -ne 1) { throw 'Unexpected Qt factory registration.' }
$utils = $utils.Replace('return factories;', "factories['QGCBluetoothBridge'] = getRuntimeSppBridge;`n    factories['QGCBleBridge'] = getRuntimeBleBridge;`n    factories['RuntimeControl'] = getRuntimeControl;`n    return factories;")
Set-Content -LiteralPath $utilsPath -Value $utils -Encoding utf8 -NoNewline
$configPath = "$packageRoot/hvigor/hvigor-config.json5"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$config.nodeOptions.maxOldSpaceSize = 1536
$config.execution.daemon = $false
$config.execution.parallel = $false
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8
Push-Location $packageRoot
try {
    & 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' build --modules entry --build-mode debug
    if ($LASTEXITCODE -ne 0) { throw 'Runtime test ArkTS/HAP build failed.' }
} finally {
    Pop-Location
}
Write-Output "Runtime test project: $packageRoot"
