# FFmpeg receiver verification

The standalone project compiles the production FFmpegVideoReceiver and the original
FFmpegVideoReceiverTest with QGC_FFMPEG_NATIVE_TEST. Native targets use the same Qt
6.12 HarmonyOS kit and FFmpeg 9.0.1 package as the application. They include the MP4
fixture and request INTERNET for loopback RTSP/RTP tests.

```powershell
./test/HarmonyFFmpeg/build-native.ps1 -Architecture x86_64 -ToolchainRoot E:/tmp/QGC
./test/HarmonyFFmpeg/build-native.ps1 -Architecture arm64-v8a -ToolchainRoot E:/tmp/QGC
```

These commands configure and build with one compiler worker. They do not install,
launch, select or stop a device or emulator. Output is inside this project's tmp:

- tmp/build-harmony-ffmpeg-runtime-x86_64/
- tmp/build-harmony-ffmpeg-runtime-arm64-v8a/

Each output contains libQGroundControl.so and
QGroundControl-harmony-deployment-settings.json. The latter can be passed to the
existing deploy/harmonyos/package.ps1 Stage workflow when a test HAP is requested.
Use Architecture arm64 for packaging the arm64-v8a build directory. The standalone
bundle is org.mavlink.qgc.ffmpeg.tests.

Required existing dependencies under ToolchainRoot:

- tmp/toolchains/Qt/6.12.0/harmonyos_x86_64 or harmonyos_arm64_v8a
- tmp/toolchains/Qt/6.12.0/mingw_64 for host Qt tools
- tmp/ffmpeg/9.0.1/x86_64/install or arm64/install
- tmp/vcpkg-downloads/tools/python/python-3.14.2-x64-1/python.exe
- the configured DevEco SDK's native CMake, Ninja and Clang tools

The equivalent configure options are CMAKE_TOOLCHAIN_FILE pointing to the target
kit's lib/cmake/Qt6/qt.toolchain.cmake, QT_HOST_PATH pointing to the host kit,
OHOS_ARCH=x86_64 or arm64-v8a, and QGC_FFMPEG_ROOT pointing to the matching install.

Native test coverage includes invalid URLs, cancellation while opening, local H.264
decode, QVideoSink frames, MP4 recording/replay, PNG screenshots, RTP/H.264 UDP
loopback, and RTSP UDP/TCP transport negotiation. The four VideoManager stop/source
regressions remain in the complete application test target, where SettingsManager,
Facts and the application's video factory are available.

The complete application suite also exercises RTSP transport loss with automatic
reconnect disabled, disabling reconnect during backoff, and user restart/URI
changes while automatic reconnect is disabled. These cases instantiate the real
VideoManager, Facts, FFmpeg receiver and loopback RTSP server. The standalone
receiver target does not include those application services. Its compiled native
test library therefore does not establish a runtime result for manager behavior.
The application target registers FFmpegVideoReceiverTest with the production unit
test registry. Its original manager cases are disabledStreamDoesNotRestart,
explicitStopDoesNotRestart, disabledFactStopsVideo and sourceDisabledThenEnabledStarts.
The added manager cases are autoReconnectPolicyAfterTransportLoss (three rows) and
autoReconnectDisabledAllowsExplicitRestart (two rows). Run the registered class
with --unittest:FFmpegVideoReceiverTest --allow-multiple after the complete test
application is available; device execution requires its normal authorized runner.

For a Windows compile check without linking foreign FFmpeg libraries:

```powershell
./test/HarmonyFFmpeg/check-interface.ps1 -QtRoot E:/tmp/QGC/tmp/toolchains/Qt/6.11.1/mingw_64 -ToolchainRoot E:/tmp/QGC -FFmpegRoot E:/tmp/QGC/tmp/ffmpeg/9.0.1/x86_64/install
```

The existing FFmpeg archives are HarmonyOS ELF objects. The Windows Qt kits carry
FFmpeg 7.1 DLLs with a different ABI from the project's FFmpeg 9.0.1 headers. The
Windows target therefore produces objects only and executes no receiver test.
Native compilation and linking establish interface compatibility; runtime codec,
network, recording and screenshot results require running the native QtTest app.

All Python build parents set the Windows error mode before subprocesses. Build
caches and temporary files use the project's tmp directory.
