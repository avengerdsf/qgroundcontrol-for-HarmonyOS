# Network link loopback tests

Run from the repository with PowerShell:

```powershell
./test/NetworkLinks/run-tests.ps1
```

The standalone Qt Test target extracts `UDPWorker`, `TCPWorker`, `UDPClient`,
and their helper functions from the current production headers and source.
Source changes trigger regeneration. Configuration fixtures supply the port,
loopback TCP host, and manual UDP destinations; settings storage, logging
registration, LinkInterface, and the flight application are outside this target.

The host entry builds two small executables and runs them sequentially. The
default Worker retains its normal platform branch and 13 functional slots.
The Harmony Worker executable defines Q_OS_HARMONY only for its generated
Worker source and runs 16 functional slots; the host Qt socket backend remains
unchanged. Its test entry uses a separate flag, so it does not load the
Harmony GUI entry point on Windows.

`QUdpSocket`, `QTcpSocket`, and `QTcpServer` perform real local socket I/O.
Peers bind random ports, and application payloads target `127.0.0.1`.
The worker retains its production multicast join/leave calls, which depend on
the host network environment. Zeroconf is disabled. Multicast application-data
delivery is outside this suite. Worker method bodies remain intact.

The suite covers UDP incoming data, automatic return targets, manual targets,
multiple endpoints, target deduplication, empty and 48 KiB datagrams, occupied
ports, close/reopen, and disconnected writes. TCP coverage includes connection,
fragmented incoming data, exact bidirectional 256 KiB transfers, unavailable
listeners, remote/local disconnect, reconnect, and recovery after invalid writes.
Warnings in occupied-port, unavailable-listener, and remote-close cases are
expected error-path observations.

UDP readiness regressions cover a stale notification and a real socket race:
the test consumes one loopback datagram with native nonblocking `recv` before
the Worker reads, leaving Qt's readiness state unchanged. It checks no
empty-payload signal and subsequent valid loopback receive/reply. Qt's Unix
backend maps the resulting EAGAIN to TemporaryError, which the Harmony Worker
does not forward. Host Windows Qt maps WSAEWOULDBLOCK to NetworkError; that real
error is expected and remains reported. A separate controlled TemporaryError
signal checks suppression, subsequent real loopback data, and preservation of
later real-error reporting and deduplication. Existing empty-datagram and
occupied-port cases retain real socket coverage.

Builds, temporary environment, and reports stay under the project `tmp/` tree.
Reports stay in `tmp/build-network-links-test/`. `result-default.xml/.txt` and
`result-harmony.xml/.txt` retain each variant's native results;
`variant-counts.json` records actual functional and lifecycle invocation counts.
`result.xml` combines the native suites for the communications aggregator:
33 native invocations, including four lifecycle and 29 functional invocations.
`test-results.xml` contains only the two CTest wrappers and is not added to the
native counts.
These checks validate the worker software on host Qt. HarmonyOS networking,
Wi-Fi radio behavior, permissions at runtime, and a flight-controller connection
still require platform or hardware verification.

## HarmonyOS socket runtime

Build the independent x86_64 Debug HAP with PowerShell 7:

```powershell
./test/NetworkLinks/build-ohos.ps1
```

The build uses Qt 6.12.0's HarmonyOS kit, a single native build worker, the
shared `package.ps1` staging path, and official `devecocli build`. Its fixed
bundle is `org.mavlink.qgc.network.tests`, with label `QGC Network Tests`,
version 5.0.8, and minimum API23. Bluetooth and USB bridges are excluded.
The build directory is `tmp/build-network-links-ohos-x86_64`.
`-SkipNativeBuild` restages the existing native binary; `-VersionCode` sets
the independent package's version code.

On an already running x86_64 emulator authorized for testing, run:

```powershell
./test/NetworkLinks/run-ohos.ps1 -Device '127.0.0.1:5555'
./test/NetworkLinks/run-ohos.ps1 -Device '127.0.0.1:5555' -SkipInstall
```

Pass the emulator's actual connection key. The runner accepts only explicit
`127.0.0.1:<port>` keys and verifies the target ABI is exactly x86_64.
It checks the staged and embedded bundle identity, INTERNET permission, and
every native library's x86_64 ELF header before installation. There is no
bundle or HAP override. Installation uses official `devecocli run --skip-build`
against the fixed project. Qt's official `harmonyostestrunner` then runs all
16 test slots, using the explicit target and bundle.

The HAP links real HarmonyOS Qt Core/Gui/Network/Test. All test method bodies
and extracted production Worker methods are shared with the host target;
`QUdpSocket`, `QTcpSocket`, and `QTcpServer` use the HarmonyOS socket backend.
The small configuration fixtures described above remain in use. The test
wrapper restores normal first-Ability reuse and shows a QWindow; ordinary
installation launches an idle window. Tests start only when the official
runner supplies `QGC_NETWORK_RUN=1`. No Bluetooth permission dialog is needed.

Fresh reports are written to
`tmp/verification/network-links-ohos/run-<UTC timestamp>/report.json`, with
package hash, current source hashes, selected slots, operation arguments,
stdout/stderr, and native Qt Test counts. Passing requires all 16 functional
PASS lines plus both lifecycle PASS lines, one consistent 18-PASS summary,
zero failed/skipped/blacklisted cases, exit code zero, and the stdout marker
`QGC_NETWORK_DONE exit=0`. Expected network error-path warnings are retained.
The recorded source hashes describe the inputs present when the runner starts;
associate them with the native and HAP build records when archiving a release.

The scripts do not create, configure, start, or stop an emulator, restart HDC,
or deploy production QGC. These loopback checks establish Qt Harmony worker
socket behavior only. Full LinkManager/configuration integration, multicast
payload delivery, external network peers, Wi-Fi hardware, and flight-controller
communication require their own acceptance evidence.
