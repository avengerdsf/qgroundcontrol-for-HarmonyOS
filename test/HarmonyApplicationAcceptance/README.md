# Full QGC application acceptance plan

```powershell
./test/HarmonyApplicationAcceptance/prepare-acceptance.ps1
./test/HarmonyApplicationAcceptance/prepare-acceptance.ps1 -CreateReport
```

Both commands operate only on host files. The first displays the cases. The
second prepares source hashes and empty evidence records under
`tmp/verification/harmony-application/run-<UTC timestamp>/`. Every case starts
as `not_run`; creating a report establishes no pass result. There are no HDC,
UI-input, installation, app-lifecycle, or emulator commands in this script.

Device operations belong to root's interactive schedule. The only target is
the existing `127.0.0.1:5555` x86_64 emulator and the independent full Debug
bundle `org.mavlink.qgroundcontrol.tests`. Keep the emulator running. Launch
this bundle without `--unittest`, `--simple-boot-test`, or settings-clearing
arguments. `src/main.cc:75,184,209` selects the normal application path unless
unit testing is explicitly requested. Merely compiling `QGC_UNITTEST_BUILD`
does not clear normal UI settings (`src/QGCApplication.cc:134,167`).

The ordered cases in `cases.json` cover persistent Mock Link creation,
automatic connection after app relaunch, parameter read/write/refresh,
mission upload/download, tablet window presentation, language persistence,
and the default Console export filename. Use the saved Mock Link for the
parameter and mission cases. The Debug settings page `Mock Link` also has a
`PX4 Vehicle` button for a shorter temporary vehicle check, but that dynamic
link does not establish saved-configuration recovery.

## Initial sandbox settings

`initial-sandbox-seed.ini` records the initial values for root to apply only
inside the stopped test bundle's sandbox. It is not pushed automatically.
Apply it before the first normal UI launch; retain the UI-created settings
for subsequent restart tests. Reapplying `count=0` would remove the saved
configuration being tested.

- Disable all six `AutoConnect` booleans and use the exact string
  `autoConnectNmeaPort=Disabled`.
- Set `LinkConfigurations/count=0` initially. Existing saved configurations
  can independently reconnect Serial, UDP, TCP, or Bluetooth through their
  `auto` flag (`LinkManager.cc:690`); there is no global Bluetooth auto-connect
  Fact. Later only the UI-created Mock Link receives `auto=true`.
- Remove a deprecated `LinkManager` group before seeding. Its constructor
  migration copies old values into `AutoConnect` and can overwrite the seed
  (`AutoConnectSettings.cc:19`).
- Disable root-level `forwardMavlink` and
  `ADSBVehicleManager/adsbServerConnectEnabled`.
- Keep the current settings version, presently `SettingsVersion=9`. Remove
  `DeleteAllSettingsNextBoot` entirely if it exists; its presence triggers
  clearing even when the stored value is false (`QGCApplication.cc:165`).

These settings prevent automatic vehicle connections. They do not eliminate
serial enumeration: `LinkManager.cc:529,824` still calls `availablePorts`
once per timer update even with all serial auto-connect options disabled.
The existing CLI has no switch that disables that enumeration. The permitted
target remains the emulator; no host or physical-device USB interface is
selected by this plan.

## Locating the current UI

English labels in the case list come from current QML. For Chinese, use the
current translated text on screen. The language choices themselves include
`English` and `中文 (Chinese)` (`AppSettings.cc:45`).

QML IDs such as `nameField`, `firmwareTypeCombo`, `searchText`, `editorMap`,
and `filenameTextField` are source anchors, not UiTest resource IDs. Existing
tablet layout captures expose no Qt text/IDs; desktop captures expose system
window IDs such as `EnhanceMaximizeBtn`. At every step, inspect a fresh
screenshot and current layout tree. Use a unique current text/control region
when visible. Record the screenshot used to derive any input position.
Ambiguous or absent controls leave the case unexecuted until inspected.

Current-version visual references are
`tmp/verification/v5.0.8-ui/mock-page.jpeg` and `plan-loaded.jpeg`, plus the
later `tmp/verification/qgc-pad-final-settings.jpeg` and
`qgc-pad-final-export-dialog.jpeg`. They help identify the page, but their
coordinates are not reusable. The 10/9 parameter/plan screenshots predate the
current UI. No reusable Harmony screenshot-action script was found.

For each executed case, root records actual steps, visible observations,
before/after screenshots, applicable sandbox INI or exported files, and
Console messages. Mark a case passed only when all its expected observations
are supported. Pending, blocked, or failed cases remain distinct. This plan
does not launch SITL or establish physical flight-controller, radio, or USB
compatibility.
