# HarmonyOS window policy tests

Run from the repository root in PowerShell:

```powershell
. ./deploy/harmonyos/deveco-env.ps1
./test/HarmonyWindowPolicy/test-stage-hooks.ps1
./test/HarmonyWindowPolicy/test-device-types.ps1
./test/HarmonyWindowPolicy/test-start-window-stage.ps1
& "$env:NODE_HOME/node.exe" --max-old-space-size=256 ./test/HarmonyWindowPolicy/run-tests.cjs --ability-fixture
& "$env:NODE_HOME/node.exe" --max-old-space-size=256 ./test/HarmonyWindowPolicy/run-tests.cjs --sdk-types
& "$env:NODE_HOME/node.exe" --test ./deploy/harmonyos/phone/test_screen_policy.cjs
./deploy/harmonyos/phone/test_stage_hooks.ps1
```

The suite runs the production ETS policy and staged QAbility with a mock window. Cold creation reads `MainWindowState` from the existing application Qt INI before loading Qt content. It covers saved Windowed, Maximized and FullScreen states, device defaults, invalid records, matching native modes, stage replacement, delayed requests, destruction, and API failures. Stage restoration retains the current system mode. The tablet system-bar tests cover full screen, ordinary windows and foreground transitions. Staging tests check repeated application, reject incomplete lifecycle hooks, and require the hidden starting-window profile with valid icon/background resource references.

Phone startup applies sensor-driven landscape orientation before loading Qt content. Its foreground and background callbacks control the keep-screen-on flag. Tests cover orientation and screen API failures, pending operations, stage replacement, destruction, and superclass/Qt lifecycle preservation. Deployment metadata requires exactly phone, tablet and 2in1.

Runtime verification covers visible startup frames, system-bar visibility, edge gestures, foreground restoration, and window-mode changes. `REQUIRED_HIDE` applies to 2in1 and tablets in free-window mode; other configurations retain the official QGC startup icon and background. A hidden startup window may retain native geometry before the application becomes visible. The application is packaged through `deploy/harmonyos/package.ps1`.
