# Application settings warning regression

Run `./test/AppSettingsWarnings/run-tests.ps1` from the repository root.

The test loads the production PX4 log transfer page in Qt Quick. It checks disconnected creation, repeated vehicle connection and disconnection, manager and vehicle destruction, wind/rating synchronization and user selection, form saving, start/stop dispatch, and confirmation callbacks after disconnection. QML warnings fail the test. UI controls use Qt Quick Controls fixtures; device rendering is verified separately on Pad.
