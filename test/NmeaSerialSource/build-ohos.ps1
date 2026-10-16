#requires -Version 7.0
param(
    [switch]$SkipNativeBuild,
    [ValidateRange(1, 2147483647)]
    [long]$VersionCode = 5010513
)

$ErrorActionPreference = 'Stop'
$nmeaProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
$nmeaBuildRoot = "$nmeaProjectRoot/tmp/build-nmea-serial-source-ohos-x86_64"
$nmeaPackageRoot = "$nmeaBuildRoot/harmonyos-build"
$nmeaBundle = 'org.mavlink.qgc.nmea.tests'
. "$nmeaProjectRoot/deploy/harmonyos/deveco-env.ps1"

function Set-NmeaMemoryLimits {
    $env:NODE_OPTIONS = '--max-old-space-size=512'
    $env:_JAVA_OPTIONS = '-Xmx512m'
    $env:Xmx = '512'
    $env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
    $env:CTEST_PARALLEL_LEVEL = '1'
}
Set-NmeaMemoryLimits

function Assert-NmeaOutput([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath("$nmeaProjectRoot/tmp")
    if (-not $full.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'NMEA test output must stay inside the project tmp directory.'
    }
    $candidate = $full
    while ($candidate.Length -ge $allowed.Length) {
        if ((Test-Path -LiteralPath $candidate) -and
                ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'NMEA test output must not traverse a link.'
        }
        if ($candidate -eq $allowed) { break }
        $candidate = [IO.Path]::GetDirectoryName($candidate)
    }
}
function Get-NmeaInputs {
    $paths = @('src/Comms/LinkManager.cc', 'src/Comms/LinkManager.h',
        'src/PositionManager/PositionManager.cpp', 'src/Comms/HarmonySerialBridge.cc',
        'src/Comms/HarmonySerialBridge.h', 'src/Comms/UdpIODevice.cc', 'src/Comms/UdpIODevice.h')
    $paths += Get-ChildItem -LiteralPath $PSScriptRoot -File | ForEach-Object {
        'test/NmeaSerialSource/' + $_.Name
    }
    $hashes = [ordered]@{}
    foreach ($relative in $paths | Sort-Object -Unique) {
        $hashes[$relative] = (Get-FileHash -LiteralPath (Join-Path $nmeaProjectRoot $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $hashes
}

Assert-NmeaOutput $nmeaBuildRoot
Assert-NmeaOutput $nmeaPackageRoot
$cmake = "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/cmake.exe"
$qtTarget = "$nmeaProjectRoot/tmp/toolchains/Qt/6.12.0/harmonyos_x86_64"
$qtHost = "$nmeaProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$manifestPath = "$nmeaBuildRoot/nmea-native-inputs.json"
$inputs = Get-NmeaInputs
if (-not $SkipNativeBuild) {
    & $cmake -S $PSScriptRoot -B $nmeaBuildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
        "-DCMAKE_TOOLCHAIN_FILE=$qtTarget/lib/cmake/Qt6/qt.toolchain.cmake" `
        "-DQT_HOST_PATH=$qtHost" "-DOHOS_SDK_ROOT=$env:OHOS_SDK_ROOT" '-DOHOS_ARCH=x86_64' `
        '-DQGC_ENABLE_BLUETOOTH=OFF' '-DQGC_NO_SERIAL_LINK=ON'
    if ($LASTEXITCODE -ne 0) { throw 'NMEA test native configuration failed.' }
    & $cmake --build $nmeaBuildRoot --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'NMEA test native build failed.' }
} else {
    if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'A native build manifest is required for -SkipNativeBuild.' }
    $previous = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($previous.bundleName -cne $nmeaBundle -or $previous.architecture -cne 'x86_64') {
        throw 'The previous NMEA native manifest has the wrong identity.'
    }
    foreach ($relative in $inputs.Keys) {
        if ($previous.sourceSha256.$relative -cne $inputs[$relative]) {
            throw "The native test inputs changed; rebuild before staging: $relative"
        }
    }
}
$deployment = Get-Content -LiteralPath "$nmeaBuildRoot/QGroundControl-harmony-deployment-settings.json" -Raw | ConvertFrom-Json
if ($deployment.'harmonyos-app-bundle-name' -cne $nmeaBundle) { throw 'Unexpected NMEA deployment bundle.' }
$native = [string]$deployment.'application-binary'
if (-not [IO.Path]::IsPathRooted($native)) { $native = Join-Path $nmeaBuildRoot $native }
Assert-NmeaOutput $native
if (-not [IO.Path]::GetFullPath($native).StartsWith([IO.Path]::GetFullPath($nmeaBuildRoot) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($native) -cne 'libQGroundControl.so') {
    throw 'NMEA native output must be this target libQGroundControl.so.'
}
$snapshot = (Get-Content -LiteralPath "$nmeaBuildRoot/nmea-source-snapshot.txt" -Raw).Trim()
if ($snapshot -notmatch '^[a-f0-9]{64}$') { throw 'NMEA source snapshot is invalid.' }
$nativeHash = (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant()
if ($SkipNativeBuild -and ($previous.nativeSha256 -cne $nativeHash -or $previous.sourceSnapshot -cne $snapshot)) {
    throw 'The native test binary or snapshot differs from the previous build manifest.'
}
$finalInputs = Get-NmeaInputs
foreach ($relative in $inputs.Keys) {
    if ($finalInputs[$relative] -cne $inputs[$relative]) { throw "Source changed during the NMEA build: $relative" }
}
@{ bundleName = $nmeaBundle; architecture = 'x86_64'; nativeBinary = $native; nativeSha256 = $nativeHash;
    sourceSnapshot = $snapshot; sourceSha256 = $inputs; recordedUtc = [DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8

& "$nmeaProjectRoot/deploy/harmonyos/package.ps1" -Architecture x86_64 -Action Stage `
    -BuildDirectory $nmeaBuildRoot -BundleName $nmeaBundle -VersionCode $VersionCode
if ($LASTEXITCODE -ne 0) { throw 'NMEA test staging failed.' }
Set-NmeaMemoryLimits
$abilityPath = "$nmeaPackageRoot/entry/src/main/ets/qability/QAbility.ets"
$ability = Get-Content -LiteralPath $abilityPath -Raw
$reuseDefault = "want.parameters['io.qt.useDefaultUiAbilityInstanceInQt'] = true;"
if (-not $ability.Contains($reuseDefault)) {
    $anchor = 'qpa.handleAbilityOnCreate(this, want, launchParam);'
    if ([regex]::Matches($ability, [regex]::Escape($anchor)).Count -ne 1) { throw 'Unexpected NMEA QAbility startup hook.' }
    $ability = $ability.Replace($anchor,
        "want.parameters = want.parameters || {};`n    $reuseDefault`n    this.launchWant = want;`n    $anchor")
    Set-Content -LiteralPath $abilityPath -Value $ability -Encoding utf8 -NoNewline
}
$configPath = "$nmeaPackageRoot/hvigor/hvigor-config.json5"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$config.nodeOptions.maxOldSpaceSize = 512
$config.execution.daemon = $false
$config.execution.parallel = $false
$config | Add-Member -NotePropertyName javaOptions -NotePropertyValue @{ Xmx = 512 } -Force
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8
Push-Location $nmeaPackageRoot
try {
    & 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' build --modules entry --build-mode debug
    if ($LASTEXITCODE -ne 0) { throw 'NMEA test ArkTS/HAP build failed.' }
} finally {
    Pop-Location
}
Write-Output "NMEA test project: $nmeaPackageRoot"
