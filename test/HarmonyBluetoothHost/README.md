# Harmony Bluetooth bridge host tests

```powershell
./test/HarmonyBluetoothHost/run-tests.ps1
```

This small Qt Core/Test target compiles the production `HarmonyBluetoothBridge.cc`
with a controlled provider. It exercises classic defaults, BLE UUID forwarding,
independent scan/socket transport selection, binary partial writes and ordering,
the 256 KiB transmit limit, receive errors, oversized reads, and callbacks arriving
after close or transport changes. It never creates a Bluetooth adapter connection.

The HarmonyOS N-API provider is compiled in the ARM64 application build. Its
platform API calls are outside this host target; the separate direct ETS tests
exercise each production SPP/GATT backend with controlled SDK objects.

Results are written to `tmp/build-bluetooth-host-test/result.txt`, `result.xml`,
and `test-results.xml`. Builds use one compiler process and project-local caches.

The runner accepts QtRoot, ToolchainRoot and BuildDirectory. Its Python parent
sets the Windows error mode before subprocesses, verifies a single Qt kit and
imported DLL symbols, and runs CTest with bounded timeouts and project-local caches.
