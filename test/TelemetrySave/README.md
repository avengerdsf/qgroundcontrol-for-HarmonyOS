# Telemetry save I/O regression

```powershell
./test/TelemetrySave/run-tests.ps1 -Baseline
./test/TelemetrySave/run-tests.ps1
```

This Qt 6.12 Core/Test target extracts the actual production
`MAVLinkProtocol::_saveTelemetryLog` and `_checkTelemetrySavePath` bodies.
The generated translation unit defines `Q_OS_HARMONY`. Minimal fixtures
provide the save directory, application error messages and a fixed clock.

File operations use real Qt files. The copy boundary can return the observed
EOVERFLOW failure; read/write overrides inject failures after data has moved,
short writes, zero writes, flush failure and a destination race. Tests compare
full-file SHA256 and sizes, check source retention and include hidden output
files when checking cleanup. Four host backups in
`tmp/verification/upload-pad-telemetry-backup` are read without modification.
All mutable test files are owned by QTemporaryDir under the build directory.

The fixed build directory is `tmp/build-telemetry-save-test`; builds use one
worker. Reports are `baseline.txt`/`baseline.xml` or `result.txt`/`result.xml`,
with the CTest wrapper report in `test-results.xml`. The suite performs no
device or application build operation. Filesystem behavior on the Pad is
verified separately by the target application.
