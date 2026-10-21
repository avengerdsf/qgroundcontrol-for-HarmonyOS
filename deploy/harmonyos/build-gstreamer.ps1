param(
    [ValidateSet('x86_64', 'arm64')]
    [string]$Architecture = 'x86_64',
    [string]$DependencyDirectory,
    [string]$Component = 'all'
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/deveco-env.ps1"
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path
if ([string]::IsNullOrWhiteSpace($DependencyDirectory)) {
    $DependencyDirectory = Join-Path $projectRoot 'tmp'
    if (-not (Test-Path -LiteralPath (Join-Path $DependencyDirectory 'toolchains/Qt'))) {
        $DependencyDirectory = [IO.Path]::GetFullPath((Join-Path $projectRoot '../../tmp'))
    }
}
$dependencyRoot = (Resolve-Path -LiteralPath $DependencyDirectory).Path
$python = Join-Path $dependencyRoot 'venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python.exe -ErrorAction Stop).Source }
$workRoot = Join-Path $projectRoot 'tmp/gstreamer/1.28.4'
$prefix = Join-Path $workRoot "$Architecture/install"
$logs = Join-Path $workRoot "$Architecture/logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$codecs = @('ffmpeg', 'openh264', 'vpx', 'x264', 'x265', 'openssl')
$components = @('libffi', 'glib', 'gstreamer') + $codecs +
    @('gst-plugins-base', 'gst-plugins-good', 'gst-plugins-bad', 'gst-plugins-ugly',
      'gst-libav', 'gst-rtsp-server', 'glib-networking', 'manifest')
if ($Component -ne 'all') {
    if ($Component -notin $components) { throw "Unknown GStreamer component: $Component" }
    $components = @($Component)
}
foreach ($name in $components) {
    $arguments = if ($name -in $codecs) {
        @('-B', "$PSScriptRoot/gstreamer/codecs.py", '--architecture', $Architecture,
          '--codec', $name, '--prefix', $prefix, '--work-root', $workRoot,
          '--dependency-root', $dependencyRoot, '--jobs', '1')
    } else {
        @('-B', "$PSScriptRoot/gstreamer/build.py", '--architecture', $Architecture,
          '--dependency-root', $dependencyRoot, '--component', $name)
    }
    Write-Output "Building $name for $Architecture (one compiler job)"
    & $python @arguments *> (Join-Path $logs "$name-driver.log")
    if ($LASTEXITCODE -ne 0) { throw "GStreamer component failed: $name; see $logs/$name-driver.log" }
}
Write-Output "GStreamer_ROOT_DIR=$prefix"
