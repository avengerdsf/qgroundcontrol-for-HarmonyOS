# Bluetooth settings host checks

```powershell
./test/HarmonyBluetoothSettings/run-tests.ps1
```

The target loads the current production `BluetoothSettings.qml` through Qt's
QML engine. Controlled configuration signals and basic Qt Controls isolate
the page from radios and unrelated application services. Discovery-error
checks emit the same `errorOccurred(QString)` signal as the production
configuration, require its exact backend message to appear as visible plain
text, then verify that a new Scan clears it, a later error replaces it, and
Stop remains usable. Existing UUID, preset, validation and selection checks
remain in the same suite.

For the error-feedback regression's preserved pre-fix source, root can run
the same compiled tests with this temporary source override:

```powershell
$env:QGC_BLUETOOTH_SETTINGS_SOURCE = 'E:/tmp/QGC/tmp/verification/BluetoothSettings-before-error-feedback.qml'
./test/HarmonyBluetoothSettings/run-tests.ps1
Remove-Item Env:QGC_BLUETOOTH_SETTINGS_SOURCE
./test/HarmonyBluetoothSettings/run-tests.ps1
```

The first run is expected to fail the two user-visible discovery-error cases;
preserve its reports before the second run overwrites `result.txt` and
`result.xml`. The default source has no override. The saved baseline can be
removed after root retains the RED results.

These are software QML checks. They do not establish that the unsupported
message has already been observed in the actual HarmonyOS application. Root
must separately open a Bluetooth configuration in the updated full test HAP,
press Scan on the unsupported emulator, verify the displayed backend error
and responsive Cancel/retry controls, and retain that UI evidence. No
successful radio connection is claimed.
