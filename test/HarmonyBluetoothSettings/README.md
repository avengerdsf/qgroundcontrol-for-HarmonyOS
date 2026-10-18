# Bluetooth settings host checks

```powershell
./test/HarmonyBluetoothSettings/run-tests.ps1 -ToolchainRoot E:/tmp/QGC -QtRoot E:/tmp/QGC/tmp/toolchains/Qt/6.12.0/mingw_64
```

The target loads src/AppSettings/BluetoothSettings.qml through Qt's QML engine,
using controlled configuration signals and basic Qt Controls. It checks the new
mode/devicesModel/UUID contract, address-preserving RSSI sorting, UUID validation
and saves, system-managed Harmony pairing and exact backend permission feedback.
Upstream adapter, pairing and signal-strength controls remain on the production page.

The shared Python runner suppresses Windows loader/crash dialogs and verifies that
CMake modules and executable imports use the selected kit before running CTest.
QtRoot, ToolchainRoot and BuildDirectory are explicit parameters; all caches and
reports remain in the project tmp directory. Tests run offscreen with software Qt
Quick rendering, without any radio or device interaction.
The actual LinkConfigurationManager page and QGCPopupDialogFactory are also loaded.
Checks cover a missing page, invalid settings, a rejected save, successful saves,
and legacy pages whose saveSettings returns undefined or is absent.
