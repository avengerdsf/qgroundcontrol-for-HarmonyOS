[CmdletBinding()]
param(
    [string]$JavaHome = 'D:/Progrem/Huawei/DevEco Studio/jbr',
    [string]$NodePath = 'D:/Progrem/Huawei/DevEco Studio/tools/node/node.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$workRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "tmp/upstream-usb-serial-$PID"))
if (-not $workRoot.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Upstream test files must stay inside the project.'
}
if (Test-Path -LiteralPath $workRoot) { throw 'The upstream test directory must be new and owned by this run.' }
$java = Join-Path $JavaHome 'bin/java.exe'
$javac = Join-Path $JavaHome 'bin/javac.exe'
foreach ($tool in @($java, $javac, $NodePath)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required tool is missing: $tool" }
}

$maven = 'https://repo.maven.apache.org/maven2'
$artifacts = @(
    @('v3.8.1.zip', 'https://codeload.github.com/mik3y/usb-serial-for-android/zip/refs/tags/v3.8.1', '096BF4E839E2756E4C573D51D55960B6B98A249905A43C2BF48CC0545F4F738F'),
    @('jars/junit-4.13.2.jar', "$maven/junit/junit/4.13.2/junit-4.13.2.jar", '8E495B634469D64FB8ACFA3495A065CBACC8A0FFF55CE1E31007BE4C16DC57D3'),
    @('jars/hamcrest-core-1.3.jar', "$maven/org/hamcrest/hamcrest-core/1.3/hamcrest-core-1.3.jar", '66FDEF91E9739348DF7A096AA384A5685F4E875584CCE89386A7A47251C4D8E9'),
    @('jars/mockito-core-5.12.0.jar', "$maven/org/mockito/mockito-core/5.12.0/mockito-core-5.12.0.jar", '4A2EB29237050DA749E90A46F948BCE7E26EC22B671E41F59B1AC6F4B6408229'),
    @('jars/byte-buddy-1.14.15.jar', "$maven/net/bytebuddy/byte-buddy/1.14.15/byte-buddy-1.14.15.jar", '7472E3961992C12A9FD4F6D67C21DE4280ABE18F292704DD49D7338289F8ACF5'),
    @('jars/byte-buddy-agent-1.14.15.jar', "$maven/net/bytebuddy/byte-buddy-agent/1.14.15/byte-buddy-agent-1.14.15.jar", '3EF6EC7175801361170067CC6902969F966D37C72BF9353D9CD282263B184064'),
    @('jars/objenesis-3.3.jar', "$maven/org/objenesis/objenesis/3.3/objenesis-3.3.jar", '02DFD0B0439A5591E35B708ED2F5474EB0948F53ABF74637E959B8E4EF69BFEB'),
    @('jars/android-all-5.0.2_r3-robolectric-r0.jar', "$maven/org/robolectric/android-all/5.0.2_r3-robolectric-r0/android-all-5.0.2_r3-robolectric-r0.jar", '5E63D4C7F2C691AFED648BF0675E0B0A76D19F0E23D93705F4FAF9ED3B2734DE'),
    @('jars/annotation-jvm-1.8.0.jar', 'https://dl.google.com/dl/android/maven2/androidx/annotation/annotation-jvm/1.8.0/annotation-jvm-1.8.0.jar', '9AAB326D9492800991854360AC248F493CE7F7C3183519309B78ACE9E240F6F6'),
    @('jars/kotlin-stdlib-1.7.10.jar', "$maven/org/jetbrains/kotlin/kotlin-stdlib/1.7.10/kotlin-stdlib-1.7.10.jar", 'E771FE74250A943E8F6346713201FF1D8CB95C3A5D1A91A22B65A9E04F6A8901')
)
$priorTemp = $env:TEMP
$priorTmp = $env:TMP
try {
    $javaTmp = Join-Path $workRoot 'java-tmp'
    New-Item -ItemType Directory -Path $javaTmp -Force | Out-Null
    $env:TEMP = $javaTmp
    $env:TMP = $javaTmp
    $manifestPath = Join-Path $workRoot 'downloads.json'
    ConvertTo-Json -InputObject $artifacts -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $downloader = @'
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const { Readable } = require('node:stream'), { pipeline } = require('node:stream/promises');
async function digest(file) {
  const hash = crypto.createHash('sha256');
  for await (const bytes of fs.createReadStream(file)) hash.update(bytes);
  return hash.digest('hex').toUpperCase();
}
(async () => {
  const root = path.dirname(process.argv[1]);
  const manifest = JSON.parse(fs.readFileSync(process.argv[1], 'utf8').replace(/^\uFEFF/, ''));
  for (const [name, url, sha] of manifest) {
    const target = path.join(root, name);
    if (fs.existsSync(target) && await digest(target) === sha) {
      console.log('Verified ' + name); continue;
    }
    fs.mkdirSync(path.dirname(target), { recursive: true });
    for (let attempt = 0; ; attempt++) {
      try {
        const response = await fetch(url, { signal: AbortSignal.timeout(30000) });
        if (!response.ok) throw Error('HTTP ' + response.status);
        await pipeline(Readable.fromWeb(response.body), fs.createWriteStream(target));
        if (await digest(target) !== sha) throw Error('SHA256 mismatch: ' + name);
        console.log('Downloaded and verified ' + name); break;
      } catch (error) {
        fs.rmSync(target, { force: true });
        if (attempt >= 2) throw error;
      }
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
'@
    & $NodePath --max-old-space-size=256 -e $downloader $manifestPath
    if ($LASTEXITCODE -ne 0) { throw 'Could not obtain the pinned upstream sources and test dependencies.' }

    Expand-Archive -LiteralPath (Join-Path $workRoot 'v3.8.1.zip') -DestinationPath (Join-Path $workRoot 'source') -Force
    $sourceRoot = Join-Path $workRoot 'source/usb-serial-for-android-3.8.1/usbSerialForAndroid'
    $classes = Join-Path $workRoot 'classes'
    $generated = Join-Path $workRoot 'generated/com/hoho/android/usbserial'
    New-Item -ItemType Directory -Path $classes, $generated -Force | Out-Null
    # BuildConfig is normally generated by the Android Gradle plugin. The upstream sources stay untouched.
    $buildConfig = Join-Path $generated 'BuildConfig.java'
    Set-Content -LiteralPath $buildConfig -Encoding utf8 -Value 'package com.hoho.android.usbserial; public final class BuildConfig { public static final boolean DEBUG = false; }'
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'src/main/java') -Recurse -Filter '*.java' -File | ForEach-Object FullName)
    $sources += @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'src/test/java/android') -Recurse -Filter '*.java' -File | ForEach-Object FullName)
    $sources += $buildConfig
    $sources += Join-Path $sourceRoot 'src/test/java/com/hoho/android/usbserial/driver/CdcAcmSerialDriverTest.java'
    $sources += Join-Path $sourceRoot 'src/test/java/com/hoho/android/usbserial/driver/FtdiSerialDriverTest.java'
    $classpath = ($artifacts | Where-Object { $_[0].EndsWith('.jar') } | ForEach-Object { Join-Path $workRoot $_[0] }) -join [IO.Path]::PathSeparator
    & $javac '-J-Xmx256m' '-J-Duser.language=en' "-J-Djava.io.tmpdir=$javaTmp" "-J-Duser.home=$workRoot" -encoding UTF-8 --release 8 -Xlint:-options -cp $classpath -d $classes @sources
    if ($LASTEXITCODE -ne 0) { throw 'Compilation of the original upstream tests failed.' }

    # Mockito 5.12 pins Byte Buddy 1.14.15; experimental mode supports this host's JDK 25 class format.
    Write-Host 'Running original v3.8.1 CDC tests (8) and FTDI readFilter (1); Java heap limit 256 MiB.'
    & $java '-Xmx256m' '-Duser.language=en' "-Djava.io.tmpdir=$javaTmp" "-Duser.home=$workRoot" '-Dnet.bytebuddy.experimental=true' "-javaagent:$workRoot/jars/byte-buddy-agent-1.14.15.jar" -cp "$classes$([IO.Path]::PathSeparator)$classpath" org.junit.runner.JUnitCore com.hoho.android.usbserial.driver.CdcAcmSerialDriverTest com.hoho.android.usbserial.driver.FtdiSerialDriverTest
    if ($LASTEXITCODE -ne 0) { throw 'The original upstream tests failed.' }
} finally {
    $env:TEMP = $priorTemp
    $env:TMP = $priorTmp
    if (Test-Path -LiteralPath $workRoot) {
        $cleanupTarget = Get-Item -LiteralPath $workRoot
        if ($cleanupTarget.FullName -ne $workRoot -or
            ($cleanupTarget.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Refusing cleanup outside the verified project test directory.'
        }
        $pendingDirectories = [Collections.Generic.Stack[string]]::new()
        $pendingDirectories.Push($workRoot)
        while ($pendingDirectories.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pendingDirectories.Pop() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'The upstream test cleanup tree contains a junction or symbolic link.'
                }
                if ($item.PSIsContainer) { $pendingDirectories.Push($item.FullName) }
            }
        }
        Remove-Item -LiteralPath $cleanupTarget.FullName -Recurse -Force
    }
}
