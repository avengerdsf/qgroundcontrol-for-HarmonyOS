# Harmony Bluetooth configuration tests

```powershell
./test/HarmonyBluetoothConfiguration/run-tests.ps1 -ToolchainRoot E:/tmp/QGC -QtRoot E:/tmp/QGC/tmp/toolchains/Qt/6.11.1/mingw_64
```

This target compiles the current production BluetoothConfiguration, BluetoothWorker,
HarmonyBluetoothWorker and HarmonyBluetoothBridge. The first three select the Harmony
branch; the bridge uses a controlled host provider. A minimal LinkConfiguration base
and availability/logging fixtures isolate unrelated application services. No radio,
HDC, emulator or device is used.

Coverage includes legacy lowEnergy/ble*Uuid settings migration, new-key precedence,
UUID normalization, transport switching and copyFrom cancellation, explicit SPP/BLE
scan types, same-name devices selected by address, scan RSSI, denied permissions,
per-transport capabilities, system-managed pairing/power and factory binary I/O.

The runner uses a single Qt kit, clears stale CMake configuration, verifies all Qt
module paths and executable DLL imports, and suppresses Windows loader/crash dialogs
before launching subprocesses. QtRoot, ToolchainRoot and BuildDirectory are explicit
parameters. BuildDirectory must be inside this project's tmp directory.

Reports are result.txt, result.xml and test-results.xml in the build directory.
Platform NAPI, native permissions and flight-controller communication require the
separate Harmony application build and authorized hardware verification.