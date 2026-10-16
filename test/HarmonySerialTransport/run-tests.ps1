$ErrorActionPreference='Stop'
$transportProjectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace([char]92,[char]47)
. "$transportProjectRoot/deploy/harmonyos/deveco-env.ps1"
$qtRoot="$transportProjectRoot/tmp/toolchains/Qt/6.12.0/mingw_64"
$mingwRoot="$transportProjectRoot/tmp/toolchains/Qt/Tools/mingw1310_64/bin"
$buildRoot="$transportProjectRoot/tmp/build-harmony-serial-transport-test"
$cmake="$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/cmake.exe"
$env:PATH="$qtRoot/bin;$mingwRoot;"+$env:PATH
& $cmake -S $PSScriptRoot -B $buildRoot -G 'MinGW Makefiles' `
    '-DCMAKE_BUILD_TYPE=Debug' "-DQt6_DIR=$qtRoot/lib/cmake/Qt6" `
    "-DCMAKE_CXX_COMPILER=$mingwRoot/g++.exe" "-DCMAKE_MAKE_PROGRAM=$mingwRoot/mingw32-make.exe"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $cmake --build $buildRoot --parallel 1
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& "$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin/ctest.exe" --test-dir $buildRoot --parallel 1 --output-on-failure
exit $LASTEXITCODE
