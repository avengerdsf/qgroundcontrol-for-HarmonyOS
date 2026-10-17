param([string]$QtRoot)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. "$projectRoot/deploy/harmonyos/deveco-env.ps1"
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64' }
$buildRoot = Join-Path $projectRoot 'tmp/build-appsettings-warnings-test'
$compilerDirectory = Join-Path $projectRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$cmakeDirectory = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin'
$env:PATH = "$(Join-Path $QtRoot 'bin');$compilerDirectory;" + $env:PATH
$env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
& "$cmakeDirectory/cmake.exe" -S $PSScriptRoot -B $buildRoot -G Ninja `
    "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$compilerDirectory/g++.exe" `
    "-DCMAKE_MAKE_PROGRAM=$cmakeDirectory/ninja.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$cmakeDirectory/cmake.exe" --build $buildRoot --parallel 1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$cmakeDirectory/ctest.exe" --test-dir $buildRoot --output-on-failure
exit $LASTEXITCODE
