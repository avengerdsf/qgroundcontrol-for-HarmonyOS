# Harmony serial NMEA source regression

```powershell
./test/NmeaSerialSource/run-tests.ps1 -Baseline
./test/NmeaSerialSource/run-tests.ps1
```

This small Qt 6.8.3 host target extracts the complete production
`LinkManager::_updateAutoConnectLinks`, `_addSerialAutoConnectLink`, the
Harmony asynchronous QIODevice class, and
`QGCPositionManager::setNmeaSourceDevice` without changing their bodies.
The existing C++ `HarmonySerialBridge` and `UdpIODevice` compile directly.
Only the generated LinkManager translation unit defines `Q_OS_HARMONY`;
the bridge uses its existing unit-test provider instead of Harmony NAPI.

The controlled port-info fixture inherits a real, empty QSerialPortInfo
base and supplies Harmony metadata separately, matching the migration
boundary. The old branch therefore invokes a real QSerialPort with an empty
native definition. QtSerialPort, QNmeaPositionInfoSource and its parser remain
real implementations. Minimal settings and unrelated auto-connect fixtures
provide the two NMEA preferences and avoid application services or discovery.
The PositionManager dispatch fixture starts the real NMEA source without
initializing internal GPS, permissions or the global application manager.

Checks cover configured port identity and 4800 baud, fragmented RMC/GGA
positions, nonblocking open, baud changes during pending authorization,
permission denial without timer retries, explicit reselection, Disabled,
UDP switching, unplug/replug, stale grants/reads, stopping pending position
updates across two timer periods, FIFO line semantics and
64KiB overflow. FIFO-only cases temporarily block the device's readyRead
signal to model a stalled consumer; parser integration uses normal signals.
UDP switching binds only an ephemeral local host port and sends no packets.

The target uses the existing complete host Qt 6.8.3 kit. The partial host
Qt 6.12 kit has no Positioning/SerialPort modules. Target Qt 6.12 compilation,
the existing native HarmonyNmeaTest cases and physical USB permissions remain
separate acceptance steps. No device, HDC, emulator or full application build
is invoked.

Build and reports stay in `tmp/build-nmea-serial-source-test`. Builds use one
worker. Baseline reports are `baseline.txt`/`baseline.xml`; ordinary reports
are `result.txt`/`result.xml` and CTest's `test-results.xml`.

## HarmonyOS Qt 6.12 runtime entry

Prepare the independent x86_64 Debug package with PowerShell 7:

```powershell
./test/NmeaSerialSource/build-ohos.ps1
```

The fixed bundle is `org.mavlink.qgc.nmea.tests`, with label `QGC NMEA Tests`,
version 5.0.8 and minimum API23. Output is
`tmp/build-nmea-serial-source-ohos-x86_64`. The target links real HarmonyOS
Qt 6.12 Core, Gui, Network, SerialPort, Positioning and Test libraries.
Native compilation uses one worker; Node and Java heaps are limited to
512MiB. The shared packaging template and official DevEco CLI create the HAP.
`-SkipNativeBuild` requires a matching previous native-input manifest;
`-VersionCode` selects the independent test package version code.

The exact same nine test bodies, production methods, asynchronous adapter
and C++ serial bridge are used. Each test installs the existing controlled
native provider before creating a serial bridge. No actual USB NAPI session
or GNSS receiver is exercised. Application logging and unrelated
PositionManager dispatch remain small fixtures. The package excludes real
USB/Bluetooth ETS factories; `QGC_NO_SERIAL_LINK` is a staging-only CMake
option here and is not a compiler definition for the extracted serial path.
INTERNET is declared for the real ephemeral UDP switch case. The tests do
not initialize an internal position provider or request radio permissions.

After root has started the authorized API24 x86_64 emulator, run:

```powershell
./test/NmeaSerialSource/run-ohos.ps1 -Device '127.0.0.1:5555'
./test/NmeaSerialSource/run-ohos.ps1 -Device '127.0.0.1:5555' -SkipInstall
```

The runner accepts only an explicit localhost TCP key, then verifies exact
x86_64 ABI and API24. Installation uses the SDK HDC with the explicit
`-t` target and the fixed, locally checked NMEA bundle. No bundle/HAP override,
uninstall or reset option is exposed. Every direct HDC command has `-t`.
Qt's official harmonyostestrunner receives the same explicit device,
bundle and nine slot names. It may stop/relaunch only this test bundle.
Neither script creates, configures, starts or stops an emulator or service.

Ordinary startup shows an idle QWindow. Tests run only with
`QGC_NMEA_RUN=1`, after the window is exposed. Success requires the
`QGC_NMEA_READY` marker reporting Qt 6.12.0 and the compiled input snapshot,
all nine functional plus both lifecycle PASS lines, one consistent
11-PASS summary, zero failures/skips/blacklisted cases, exit code zero and
`QGC_NMEA_DONE exit=0`.

Fresh evidence is stored in
`tmp/verification/nmea-serial-source-ohos/run-<UTC timestamp>/report.json`,
including operation arguments, stdout/stderr, HAP/native/source hashes,
compiled source snapshot and actual Qt Test counts. Changed native inputs
or binaries are rejected before installation. These results establish the
target Qt asynchronous device and NMEA parsing contracts with a controlled
serial provider; physical USB authorization, real NAPI callbacks and GNSS
accuracy have separate acceptance steps.
