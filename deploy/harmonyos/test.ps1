param(
    [ValidateSet('List', 'Run')]
    [string]$Action = 'Run',
    [string]$BuildDirectory = 'tmp/build-v5.0.8-harmonyos-tests-x86_64',
    [string]$Device,
    [string]$TestRegex,
    [string]$DevEcoPath = 'D:/Progrem/Huawei/DevEco Studio'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}
$tmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp')).TrimEnd('\', '/')
if (-not $buildRoot.StartsWith($tmpRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The test build must be inside the project tmp directory.'
}
$testDirectory = Join-Path $buildRoot 'test'
$temporaryDirectory = Join-Path $testDirectory 'Testing/Temporary'
$resultsPath = Join-Path $buildRoot 'test-results.xml'
foreach ($outputPath in @($buildRoot, $temporaryDirectory, $resultsPath)) {
    for ($currentPath = $outputPath; ; $currentPath = Split-Path $currentPath -Parent) {
        $entry = try { Get-Item -LiteralPath $currentPath -Force -ErrorAction Stop }
            catch [System.Management.Automation.ItemNotFoundException] { $null }
        if ($entry -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'The test build and output paths must not traverse a junction or symbolic link.'
        }
        if ($currentPath -eq $tmpRoot) { break }
    }
}
if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
    $outputDirectories = [Collections.Generic.Stack[string]]::new()
    $outputDirectories.Push($temporaryDirectory)
    while ($outputDirectories.Count) {
        foreach ($entry in Get-ChildItem -LiteralPath $outputDirectories.Pop() -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'CTest temporary output must not contain a junction or symbolic link.'
            }
            if ($entry.PSIsContainer) { $outputDirectories.Push($entry.FullName) }
        }
    }
}
. (Join-Path $PSScriptRoot 'deveco-env.ps1') -DevEcoPath $DevEcoPath

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw "Configure a Debug HarmonyOS build with QGC_BUILD_TESTING=ON first: $buildRoot"
}
$cache = Get-Content -LiteralPath $cachePath -Raw
if ($cache -notmatch '(?m)^QGC_BUILD_TESTING:BOOL=ON\r?$' -or
        $cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Debug\r?$') {
    throw 'The test runner requires QGC_BUILD_TESTING=ON and CMAKE_BUILD_TYPE=Debug.'
}
$settingsPath = Join-Path $buildRoot 'QGroundControl-harmony-deployment-settings.json'
$settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if ($settings.'harmonyos-app-bundle-name' -ne 'org.mavlink.qgroundcontrol.tests') {
    throw 'The test package must use the separate org.mavlink.qgroundcontrol.tests bundle.'
}

$ctest = Join-Path $env:OHOS_SDK_ROOT 'native/build-tools/cmake/bin/ctest.exe'
$ctestArguments = @('--test-dir', $testDirectory, '--no-tests=error')
if ($TestRegex) { $ctestArguments += @('--tests-regex', $TestRegex) }
if ($Action -eq 'List') {
    & $ctest @ctestArguments -N
    exit $LASTEXITCODE
}
if ([string]::IsNullOrWhiteSpace($Device)) {
    throw 'Run requires -Device with an explicit device serial.'
}
if (-not (Test-Path -LiteralPath $settings.'application-binary')) {
    throw 'Build and install the separate test HAP before running the tests.'
}

$qtHost = [regex]::Match($cache, '(?m)^QT_HOST_PATH:[^=]+=([^\r\n]+)').Groups[1].Value
$mingwRuntime = Join-Path $projectRoot 'tmp/toolchains/Qt/Tools/mingw1310_64/bin'
$env:PATH = "$qtHost/bin;$mingwRuntime;" + $env:PATH
$env:QT_HARMONYOS_DEVICE = $Device
$env:QT_HARMONYOS_HDC = Join-Path $env:OHOS_SDK_ROOT 'toolchains/hdc.exe'
& $ctest @ctestArguments --parallel 1 --output-on-failure --output-junit $resultsPath `
    --test-output-size-passed 10485760 --test-output-size-failed 10485760
$testExit = $LASTEXITCODE
Write-Output "CTest report: $resultsPath"
exit $testExit
