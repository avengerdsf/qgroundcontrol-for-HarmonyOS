param(
    [string]$QtRoot,
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $projectRoot 'deploy/harmonyos/deveco-env.ps1')
$env:NODE_OPTIONS = '--max-old-space-size=256'
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot 'tmp/toolchains/Qt/6.12.0/mingw_64' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'tmp/build-harmony-bluetooth-settings-test' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$temporaryRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
$temporaryPrefix = $temporaryRoot + [IO.Path]::DirectorySeparatorChar
if (-not $buildRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The test build directory must be inside this project tmp directory.'
}
$candidate = $buildRoot
while ($candidate -and ($candidate -eq $temporaryRoot -or $candidate.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase))) {
    if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "The test build directory must not traverse a link: $candidate"
    }
    $candidate = [IO.Path]::GetDirectoryName($candidate)
}
$compilerDirectory = Join-Path $projectRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$cmakeDirectory = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin'
$cmake = Join-Path $cmakeDirectory 'cmake.exe'
$ctest = Join-Path $cmakeDirectory 'ctest.exe'
$ninja = Join-Path $cmakeDirectory 'ninja.exe'
$compiler = Join-Path $compilerDirectory 'g++.exe'
foreach ($required in @($QtRoot, $cmake, $ctest, $ninja, $compiler)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required host test dependency is missing: $required" }
}
$env:PATH = "$(Join-Path $QtRoot 'bin');$compilerDirectory;" + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:QT_QUICK_CONTROLS_STYLE = 'Basic'
$env:QML_DISABLE_DISK_CACHE = '1'
if ($env:WINDIR -and (Test-Path -LiteralPath (Join-Path $env:WINDIR 'Fonts'))) {
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
}

& $cmake -S $PSScriptRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Debug' `
    "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$ninja"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build $buildRoot --parallel 1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $ctest --test-dir $buildRoot --output-on-failure --parallel 1 --output-junit (Join-Path $buildRoot 'test-results.xml')
$testExit = $LASTEXITCODE
$nativeResult = Join-Path $buildRoot 'result.txt'
if (Test-Path -LiteralPath $nativeResult) { Get-Content -LiteralPath $nativeResult }
exit $testExit
