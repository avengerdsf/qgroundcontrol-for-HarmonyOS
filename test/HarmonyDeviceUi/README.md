# HarmonyOS device UI host tests

Run from the repository root with Python, CMake, a host C++17 compiler, and Node:

```powershell
python -B test/HarmonyDeviceUi/run_tests.py --cmake <cmake.exe> --cxx <g++.exe> --node <node.exe> -v
```

The suite compiles the production controller header and extracted device-query
methods with controlled device-info and application inputs. It covers phone,
tablet, 2in1, unknown and empty device types, NAPI errors, one shared cached read,
desktop mobile simulation, and Android/iOS behavior.

It evaluates current production bindings in VehicleConfigView, the General
SettingsUI schema, RemoteIDGpsLocation, SerialSettings and LinkConfigurationManager.
Checks retain firmware feature/search guards, the serial capability needed for
Harmony phone USB paths, desktop NMEA UDP, and the separate APM advanced-tuning
component's unrestricted entry. It also executes production CMake device metadata.

The Python parent disables Windows loader/crash dialogs and verifies the MinGW
runtime and cc1plus frontend using the same PATH before compilation. TEMP, TMP and
TMPDIR point inside tmp/cache/host-tests. Test fixtures and compiler outputs are
removed when the suite exits.

Qt/NAPI dependencies are replaced at the host boundary. Full HAP compilation and
visual device validation remain separate checks; this suite performs no device,
HDC or emulator operation.
