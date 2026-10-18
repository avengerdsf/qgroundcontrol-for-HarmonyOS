param(
    [switch]$ConfigureOnly,
    [switch]$PlanOnly,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
$sourceRoot = Join-Path $tmpRoot 'qt-src/qt-everywhere-src-6.12.0'
$kitRoot = Join-Path $tmpRoot 'toolchains/Qt/6.12.0/harmonyos_arm64_v8a'
$hostRoot = Join-Path $tmpRoot 'toolchains/Qt/6.12.0/mingw_64'
$dependencyInclude = Join-Path $tmpRoot 'vcpkg/installed/x64-ohos/include'
$buildDir = Join-Path $tmpRoot 'qt-platform-plugin-arm64'
foreach ($path in @($sourceRoot, $kitRoot, $hostRoot, $dependencyInclude, $buildDir)) {
    $current = $path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "The standalone Qt path must not traverse a junction or symbolic link: $current"
            }
        }
        if ($current -eq $tmpRoot) { break }
        $current = Split-Path $current -Parent
    }
}
. (Join-Path $PSScriptRoot '../deveco-env.ps1')
& (Join-Path $PSScriptRoot 'apply-window-mode-patch.ps1') -SourceRoot $sourceRoot -CheckOnly:($ConfigureOnly -or $PlanOnly)
$cmake = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin/cmake.exe'
$ninja = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin/ninja.exe'
$sdkToolchain = Join-Path $env:OHOS_SDK_ROOT 'native/build/cmake/ohos.toolchain.cmake'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$toolchain = Join-Path $buildDir 'ohos-plugin-toolchain.cmake'
$toolchainContent = 'include("' + $sdkToolchain.Replace('\', '/') + '")' + "`n" +
    'include("' + (Join-Path $sourceRoot 'qtbase/cmake/QtOHOSToolchainFixes.cmake').Replace('\', '/') + '")' + "`n"
[IO.File]::WriteAllText($toolchain, $toolchainContent, [Text.UTF8Encoding]::new($false))
& $cmake -S (Join-Path $PSScriptRoot 'standalone') -B $buildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$kitRoot/lib/cmake/Qt6/qt.toolchain.cmake" `
    "-DQT_CHAINLOAD_TOOLCHAIN_FILE=$toolchain" `
    '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' `
    '-DOHOS_ARCH=arm64-v8a' '-DOHOS_STL=c++_shared' "-DOHOS_SDK_ROOT=$($env:OHOS_SDK_ROOT)" `
    "-DQT_HOST_PATH=$hostRoot" "-DCMAKE_PREFIX_PATH=$kitRoot" `
    "-DQT_SOURCE_ROOT=$sourceRoot" "-DQOHOS_DEPENDENCY_INCLUDE_DIR=$dependencyInclude"
if ($LASTEXITCODE -ne 0) { throw 'Standalone ARM64 Qt platform plugin configuration failed.' }
if ($ConfigureOnly) { return }
if ($PlanOnly) {
    & $ninja -C $buildDir -n QOhosPlatformIntegrationPlugin
    if ($LASTEXITCODE -ne 0) { throw 'Standalone ARM64 Qt plugin build planning failed.' }
    return
}
& $cmake --build $buildDir --target QOhosPlatformIntegrationPlugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Standalone ARM64 Qt platform plugin build failed.' }
$plugin = Join-Path $buildDir 'plugins/platforms/libqohos.so'
if ($Install) {
    $installedPlugin = Join-Path $kitRoot 'plugins/platforms/libqohos.so'
    $backupDir = Join-Path $tmpRoot ('qt-plugin-backups/arm64-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    Copy-Item -LiteralPath $installedPlugin -Destination (Join-Path $backupDir 'libqohos.so')
    Copy-Item -LiteralPath $plugin -Destination $installedPlugin
    Write-Output "Installed ARM64 plugin: $installedPlugin"
    Write-Output "Original plugin: $(Join-Path $backupDir 'libqohos.so')"
}
Get-FileHash -LiteralPath $plugin -Algorithm SHA256
