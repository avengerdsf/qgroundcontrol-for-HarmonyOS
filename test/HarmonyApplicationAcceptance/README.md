# Full QGC application acceptance

```powershell
./test/HarmonyApplicationAcceptance/prepare-acceptance.ps1
./test/HarmonyApplicationAcceptance/prepare-acceptance.ps1 -CreateReport -Device '<HDC connection key>'
```

The first command validates and displays the cases. The second records the
explicit target, source hashes, and empty evidence records under
`tmp/verification/harmony-application/run-<UTC timestamp>/`. Every case starts
as `not_run`. These commands operate only on host files.

## Target and test bundle

Confirm target identity, ABI, and current availability immediately before
device operations. Install the matching full Debug package as the independent
bundle `org.mavlink.qgroundcontrol.tests`. Keep an existing emulator running.
Wake a physical device only for testing and put it to sleep when finished.

Launch the test bundle without `--unittest`, `--simple-boot-test`, or
settings-clearing arguments to exercise its normal UI. The cases cover a
persistent PX4 Mock Link, reconnection, parameter read/write/refresh, mission
upload/download, window state, language persistence, and Console export.
Use the saved Mock Link for parameter and mission operations.

## Initial sandbox settings

Apply `initial-sandbox-seed.ini` only inside the stopped test bundle sandbox
before its first normal UI launch. Preserve UI-created settings for restart
tests. Reapplying `LinkConfigurations/count=0` removes the saved link.

- Disable the six real-connection `AutoConnect` switches and set `nmeaSource=0`.
- Remove any legacy `LinkManager` group before applying the seed.
- Disable MAVLink forwarding and the ADSB server connection.
- Keep `SettingsVersion=9` and remove `DeleteAllSettingsNextBoot` if present.
- Enable automatic connection only for the UI-created `Acceptance-PX4-Mock`.

Serial enumeration can still occur with automatic connections disabled.
These cases do not select a physical USB port or send commands to a real
vehicle.

## Evidence

Use a fresh screenshot and current UI tree before each action. QML IDs are
source anchors; they are not UiTest resource IDs. Derive input positions from
the current display and record that screenshot. Check current visible text
when a page has been translated.

Record actual observations, before/after screenshots, relevant sandbox
settings, exported files, and Console messages. Mark a case passed only when
all expected observations have supporting evidence. Retain distinct
`not_run`, `blocked`, and `failed` states. This plan does not establish
physical flight-controller or radio compatibility.
