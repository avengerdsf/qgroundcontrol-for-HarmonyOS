# Network configuration tests

```powershell
./test/NetworkConfiguration/run-tests.ps1
```

This Qt 6.12 Core/Network/Test target extracts the current production
`UDPClient`, `UDPConfiguration`, and `TCPConfiguration` declarations and
method bodies. CMake tracks the source files and regenerates extracted code
when they change. Their method bodies remain intact.

Minimal `LinkConfiguration`, `SettingsManager`, and `AutoConnectSettings`
fixtures supply the inherited name/auto-connect state and three UDP
preferences. The base fixture preserves virtual `setAutoConnect` dispatch in
`copyFrom`. Link ownership, application factories, and other settings are
outside this target. `QSettings` uses real temporary INI files, and
`QHostAddress`/`QHostInfo` remain real Qt implementations. Inputs use
`127.0.0.1` and `localhost`; no socket or external host is opened.

Coverage includes defaults, copy construction, replacement through
`copyFrom`, empty-copy host-list clearing, INI round trips, grouped settings,
UDP target parsing/removal/deduplication, repeated loads, automatic-connect
UDP preferences, and TCP endpoint persistence.

The fixed build directory is `tmp/build-network-configuration-test`.
Native reports are `result.txt` and `result.xml`; CTest writes
`test-results.xml`. `-Baseline` writes `baseline.txt`/`baseline.xml`.
Temporary INI directories are removed by `QTemporaryDir`.

This target covers configuration behavior in host Qt. Full LinkManager/QML
integration, platform networking, and radio or flight-controller behavior
require separate checks.
