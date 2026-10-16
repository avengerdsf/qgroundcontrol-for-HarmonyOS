param(
    [string]$DevEcoPath = 'D:/Progrem/Huawei/DevEco Studio'
)

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cacheRoot = Join-Path $projectRoot 'tmp/cache'
$userRoot = Join-Path $cacheRoot 'home'
$tempRoot = Join-Path $cacheRoot 'temp'
$cacheDirectories = @($userRoot, $tempRoot, (Join-Path $cacheRoot 'hvigor'),
        (Join-Path $cacheRoot 'npm'), (Join-Path $userRoot 'AppData/Roaming'),
        (Join-Path $userRoot 'AppData/Local'))
$cacheTmpRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'tmp'))
foreach ($path in $cacheDirectories + @((Join-Path $userRoot '.npmrc'))) {
    $currentPath = $cacheTmpRoot
    $relativePath = [IO.Path]::GetFullPath($path).Substring($cacheTmpRoot.Length).TrimStart('\')
    foreach ($part in @('') + @($relativePath.Split('\') | Where-Object { $_ })) {
        if ($part) { $currentPath = Join-Path $currentPath $part }
        try {
            $item = Get-Item -LiteralPath $currentPath -Force -ErrorAction Stop
        } catch [System.Management.Automation.ItemNotFoundException] {
            $item = $null
        }
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "The project cache path must not traverse a junction or symbolic link: $currentPath"
        }
    }
}
foreach ($directory in $cacheDirectories) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONUTF8 = '1'
$env:USERPROFILE = $userRoot
$env:APPDATA = Join-Path $userRoot 'AppData/Roaming'
$env:LOCALAPPDATA = Join-Path $userRoot 'AppData/Local'
$env:TEMP = $tempRoot
$env:TMP = $tempRoot
$env:TMPDIR = $tempRoot
$env:HVIGOR_USER_HOME = Join-Path $cacheRoot 'hvigor'
$env:npm_config_cache = Join-Path $cacheRoot 'npm'
$env:npm_config_userconfig = Join-Path $userRoot '.npmrc'
$env:NODE_OPTIONS = '--max-old-space-size=4096'
$env:JAVA_TOOL_OPTIONS = "-Duser.home=$($userRoot.Replace('\', '/')) -Djava.io.tmpdir=$($tempRoot.Replace('\', '/'))"
$env:DEVECO_PATH = $DevEcoPath
$env:NODE_HOME = Join-Path $DevEcoPath 'tools/node'
$env:JAVA_HOME = Join-Path $DevEcoPath 'jbr'
$env:OHOS_SDK_ROOT = Join-Path $DevEcoPath 'sdk/default/openharmony'
$env:PATH = "$env:NODE_HOME;$env:JAVA_HOME/bin;$env:OHOS_SDK_ROOT/native/build-tools/cmake/bin;" + $env:PATH
