# HarmonyOS device UI host tests

Run from the repository root with Python, CMake, a host C++17 compiler, and Node:

```powershell
python -B test/HarmonyDeviceUi/run_tests.py --cmake <cmake.exe> --cxx <g++.exe> --node <node.exe> -v
```

The suite compiles the production controller header and extracted device-query methods with controlled device-info and application inputs. It covers phone, tablet, 2in1, unknown and empty device types, NAPI errors, one shared cached read, desktop mobile simulation, and Android/iOS behavior. It also executes the production CMake metadata and evaluates the production QML feature and cancellation bindings.

Qt/NAPI dependencies are replaced at the host boundary. Full HAP compilation and visual device validation remain separate checks. Test fixtures and compiler outputs are created in the repository's `tmp/` directory and removed when the suite exits.
