# Harmony Bluetooth configuration tests

```powershell
./test/HarmonyBluetoothConfiguration/run-tests.ps1
```

This standalone target extracts the current production `BluetoothData` and
`BluetoothConfiguration`/`BluetoothWorker` declarations and method bodies.
Only the generated configuration translation units select `Q_OS_HARMONY`.
The production `HarmonyBluetoothBridge` is compiled with its host branch and
uses the existing test-provider interface for controlled discovery results.

The target uses the installed Qt 6.8.3 Core/Test/Bluetooth host kit, including
real Bluetooth address and device-info value types. The configuration takes
the Harmony discovery branch. It creates no Windows discovery agent and
performs no Bluetooth radio operations. Minimal LinkConfiguration and device
availability fixtures isolate unrelated application services.

Coverage includes SPP/NUS defaults, QML-facing properties, UUID normalization,
copy construction and copyFrom, INI persistence, legacy settings, SPP/BLE mode
switch cleanup, duplicate-name selection by address, unique raw-name
compatibility, and transport options passed to discovery.
Worker integration covers SPP and BLE open options, asynchronous connected
state, binary receive/send signals, close, read errors, and an empty selection.

Build files and reports are under
`tmp/build-harmony-bluetooth-configuration-test/`. Temporary INI directories
are created there and removed by QTemporaryDir. `-Baseline` saves a separate
baseline result; the default command runs CTest and writes `test-results.xml`.

This target validates host configuration behavior. Harmony NAPI, platform
permissions, radio discovery, and flight-controller communication require
separate platform verification.
