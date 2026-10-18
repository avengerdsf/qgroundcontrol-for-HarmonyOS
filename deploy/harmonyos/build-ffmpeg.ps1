param(
    [ValidateSet('x86_64', 'arm64')]
    [string]$Architecture = 'x86_64',
    [ValidateRange(1, 16)]
    [int]$Parallel = 1,
    [string]$DependencyDirectory
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/deveco-env.ps1"
$qgcRoot = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\', '/')
if ([string]::IsNullOrWhiteSpace($DependencyDirectory)) {
    $DependencyDirectory = "$qgcRoot/tmp"
    if (-not (Test-Path -LiteralPath "$DependencyDirectory/toolchains/Qt")) {
        $DependencyDirectory = [IO.Path]::GetFullPath((Join-Path $qgcRoot '../../tmp'))
    }
}
$dependencyRoot = (Resolve-Path -LiteralPath $DependencyDirectory).Path.Replace('\', '/')
$ffmpegRoot = "$qgcRoot/tmp/ffmpeg/9.0.1"
$sourceRoot = "$ffmpegRoot/source"
$sourceDirectory = "$sourceRoot/ffmpeg-9.0.1"
$archive = "$sourceRoot/ffmpeg-9.0.1.tar.gz"
$expectedHash = '657DBF258CCE6C0681714B6E40B8CA69E988C67ECFC2F7AAB47574E6EBACEB38'
$buildDirectory = "$ffmpegRoot/$Architecture/build"
$installDirectory = "$ffmpegRoot/$Architecture/install"
$nativeRoot = "$dependencyRoot/ohos-sdk/native"
$compilerRoot = "$nativeRoot/llvm/bin"
$hostRoot = "$dependencyRoot/toolchains/Qt/Tools/mingw1310_64/bin"
$gitRoot = Split-Path (Split-Path (Get-Command git.exe).Source)
$bash = Join-Path $gitRoot 'bin/bash.exe'
$shell = (Join-Path $gitRoot 'usr/bin/sh.exe').Replace('\', '/')
$make = "$hostRoot/mingw32-make.exe"
foreach ($tool in @($bash, $shell, $make, "$compilerRoot/clang.exe", "$hostRoot/gcc.exe")) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required build tool not found: $tool" }
}
New-Item -ItemType Directory -Force -Path $sourceRoot, $buildDirectory, $installDirectory | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    $sharedArchive = "$dependencyRoot/ffmpeg/9.0.1/source/ffmpeg-9.0.1.tar.gz"
    if (Test-Path -LiteralPath $sharedArchive) { Copy-Item -LiteralPath $sharedArchive -Destination $archive }
}
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.gz' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'FFmpeg 9.0.1 source archive checksum mismatch'
}
if (-not (Test-Path -LiteralPath "$sourceDirectory/configure")) {
    & tar.exe -xzf $archive -C $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract FFmpeg source' }
}
$target = if ($Architecture -eq 'arm64') { 'aarch64-linux-ohos' } else { 'x86_64-linux-ohos' }
$ffmpegArch = if ($Architecture -eq 'arm64') { 'aarch64' } else { 'x86_64' }
$configureArguments = @(
    "--prefix=$installDirectory",
    '--enable-cross-compile', '--target-os=linux', "--arch=$ffmpegArch",
    "--cc=$compilerRoot/clang.exe", "--ar=$compilerRoot/llvm-ar.exe",
    "--nm=$compilerRoot/llvm-nm.exe", "--ranlib=$compilerRoot/llvm-ranlib.exe",
    "--strip=$compilerRoot/llvm-strip.exe", "--host-cc=$hostRoot/gcc.exe",
    "--extra-cflags=--target=$target --sysroot=$nativeRoot/sysroot -D__OHOS__ -fPIC",
    "--extra-ldflags=--target=$target --sysroot=$nativeRoot/sysroot",
    "--tempprefix=$buildDirectory/configure-test", '--pkg-config=false',
    '--disable-autodetect', '--disable-programs', '--disable-doc', '--disable-debug',
    '--disable-shared', '--enable-static', '--enable-pic', '--disable-asm',
    '--disable-everything', '--enable-avcodec', '--enable-avformat', '--enable-avutil',
    '--enable-swscale', '--disable-swresample', '--disable-avfilter', '--disable-avdevice',
    '--enable-decoder=h264,hevc,mjpeg,mpeg2video', '--enable-parser=h264,hevc,mjpeg,mpegvideo',
    '--enable-demuxer=sdp,rtsp,mpegts,h264,hevc,mov,matroska,flv,mjpeg',
    '--enable-muxer=matroska,mov,mp4', '--enable-protocol=file,http,tcp,udp,rtp,data',
    '--enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,extract_extradata'
)
function Quote-BashArgument([string]$value) {
    return "'" + $value.Replace("'", "'\''") + "'"
}
$configureCommand = 'cd ' + (Quote-BashArgument $buildDirectory) + ' && ' +
    (Quote-BashArgument "$sourceDirectory/configure") + ' ' +
    (($configureArguments | ForEach-Object { Quote-BashArgument $_ }) -join ' ')
$env:PATH = "$gitRoot/usr/bin;$hostRoot;$compilerRoot;" + $env:PATH
$env:SHELL = $shell
& $bash --noprofile --norc -c $configureCommand 2>&1 | Tee-Object "$buildDirectory/configure-output.log"
if ($LASTEXITCODE -ne 0) { throw 'FFmpeg configure failed' }
$msysSource = '/' + $sourceDirectory.Substring(0, 1).ToLowerInvariant() + $sourceDirectory.Substring(2)
foreach ($generatedFile in @("$buildDirectory/Makefile", "$buildDirectory/ffbuild/config.mak")) {
    $generatedText = [IO.File]::ReadAllText($generatedFile).Replace($msysSource, $sourceDirectory)
    [IO.File]::WriteAllText($generatedFile, $generatedText, [Text.UTF8Encoding]::new($false))
}
Push-Location $buildDirectory
try {
    & $make "SHELL=$shell" "-j$Parallel" 2>&1 | Tee-Object "$buildDirectory/build-output.log"
    if ($LASTEXITCODE -ne 0) { throw 'FFmpeg build failed' }
} finally {
    Pop-Location
}
New-Item -ItemType Directory -Force -Path "$installDirectory/lib", "$installDirectory/include" | Out-Null
foreach ($library in @('avformat', 'avcodec', 'avutil', 'swscale')) {
    $includeDirectory = "$installDirectory/include/lib$library"
    New-Item -ItemType Directory -Force -Path $includeDirectory | Out-Null
    Copy-Item -LiteralPath "$buildDirectory/lib$library/lib$library.a" -Destination "$installDirectory/lib/lib$library.a"
    Get-ChildItem -LiteralPath "$sourceDirectory/lib$library" -Filter '*.h' -File |
        Copy-Item -Destination $includeDirectory
}
Copy-Item -LiteralPath "$buildDirectory/libavutil/avconfig.h", "$buildDirectory/libavutil/ffversion.h" -Destination "$installDirectory/include/libavutil"
Copy-Item -LiteralPath "$sourceDirectory/COPYING.LGPLv2.1" -Destination "$installDirectory/COPYING.LGPLv2.1"
[ordered]@{
    version = '9.0.1'; architecture = $Architecture; sourceSha256 = $expectedHash
    sourceUrl = 'https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.gz'
    configureArguments = $configureArguments; installDirectory = $installDirectory
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$installDirectory/build-manifest.json" -Encoding utf8
Write-Output "QGC_FFMPEG_ROOT=$installDirectory"
