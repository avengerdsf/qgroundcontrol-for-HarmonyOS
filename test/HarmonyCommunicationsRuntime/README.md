# Harmony communications runtime checks

Use PowerShell 7 with an already running x86_64 emulator that the user has
authorized for testing. The runner requires an explicit HDC connection key
matching `127.0.0.1:<port>` and verifies that target's ABI is exactly
`x86_64`. Unknown ABIs and physical-device addresses fail before installation.
It does not create, configure, start, or stop an emulator.

Build the independent test package with `build-runtime.ps1`, then run:

```powershell
./test/HarmonyCommunicationsRuntime/run-runtime.ps1 -Device '127.0.0.1:5555'
./test/HarmonyCommunicationsRuntime/run-runtime.ps1 -Device '127.0.0.1:5555' -Phase core -SkipInstall
```

The connection key above is an example; pass the existing emulator's actual
key. The default `core` phase exercises controlled ETS factories through
real Qt/NAPI. It checks factory registration, SPP/BLE arguments and binary
bytes, errors, closing with late callbacks, and the production factory
snapshot. This phase does not request Bluetooth permission or use a radio.

The independent test wrapper restores Qt's normal first-Ability reuse setting.
The official runner otherwise disables it and creates a second foreground
Ability while the default context remains attached to a hidden window.
Reusing the first Ability matches ordinary application startup and keeps the
real permission context in the foreground. Production QGC is unchanged.

The `grant` and `deny` phases exercise the platform's real
`ohos.permission.ACCESS_BLUETOOTH` request separately. Start with the test
bundle installed, then explicitly reset only this independent bundle when a
fresh permission decision is required:

```powershell
./test/HarmonyCommunicationsRuntime/run-runtime.ps1 -Device '127.0.0.1:5555' -Phase grant -ResetTestBundle
./test/HarmonyCommunicationsRuntime/run-runtime.ps1 -Device '127.0.0.1:5555' -Phase deny -ResetTestBundle
```

`-ResetTestBundle` uninstalls and reinstalls only
`org.mavlink.qgc.runtime.tests` on the verified emulator. It requires an
explicit permission phase and cannot be combined with `-SkipInstall`.
The default run retains the installed bundle and its permission state.
An already granted or denied state can be rerun with `-SkipInstall`.
With `-ResetTestBundle`, the first permission request must report a real dialog;
the repeat request must report no dialog. The runtime test checks these SDK
fields in addition to the permission result and the access-token state.

Installation reads only the fixed
`tmp/build-harmony-communications-runtime-x86_64/harmonyos-build` project.
The staged AppScope and the HAP's embedded manifest must both identify
`org.mavlink.qgc.runtime.tests`. Every native library must be x86_64 ELF,
including `libs/x86_64/libQGroundControl.so`. The bundle is fixed in the
script. There is no bundle or HAP override parameter.
The native binary path comes from
`QGroundControl-harmony-deployment-settings.json`'s `application-binary`;
no build-configuration subdirectory is assumed.

Every SDK HDC invocation includes `-t` and the validated connection key.
Installation uses that explicit SDK target; test execution uses Qt's official
`harmonyostestrunner` with the same `--device`, fixed bundle, `QAbility`,
180-second timeout, and 45-second progress timeout. Qt may force-stop and
launch the independent test bundle between phases.

Permission-dialog declarations require one prompt text containing both the
exact label `QGC Runtime Tests` and Bluetooth/蓝牙 or nearby devices/附近的设备.
Grant accepts only the complete button label `Allow` or `允许`; deny accepts
only `Deny`, `Don't allow`, `拒绝`, or `不允许`. No position is configured.
A split prompt or unmatched system wording is reported as a failed run;
inspect the actual dialog before changing a declaration.

Each invocation writes a fresh directory under
`tmp/verification/harmony-runtime/run-<UTC timestamp>/` containing target,
bundle, phase, package hash, command arguments, stdout/stderr and exit status
for each operation, dialog JSON, and `report.json`. A passed result requires
exit code zero, a matching successful `QGC_RUNTIME_DONE` marker, every
requested slot's actual PASS line, and a complete Qt summary with zero failed,
skipped, or blacklisted cases. Missing or inconsistent results fail.

The controlled ETS/NAPI checks and real system authorization checks are
separate results. These checks do not establish Bluetooth radio discovery,
SPP pairing, remote GATT behavior, flight-controller communication, or physical
device compatibility. Project environment, caches, and reports stay under
the project's `tmp` directory.
