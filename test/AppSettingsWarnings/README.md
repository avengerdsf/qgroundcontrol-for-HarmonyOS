# Application settings warning regression

Run `./test/AppSettingsWarnings/run-tests.ps1` from the repository root.

The test loads the production `PX4LogControl`, `PX4LogUploadSettings`, and `PX4LogFileManager` components. It checks disconnected creation, repeated vehicle connection and disconnection, manager and vehicle destruction, wind/rating synchronization, form saving, start/stop dispatch, and confirmation callbacks after disconnection. QML warnings fail the test. Visual controls and native confirmation windows use local fixtures. Results are written to the test build directory in `result.txt` and `test-results.xml`.
