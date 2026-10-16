# Serial baud-rate differential test

```powershell
./test/SerialBaudRates/run-tests.ps1 -Baseline
./test/SerialBaudRates/run-tests.ps1
```

This small Qt 6.12 Core/Test target extracts the current production
`SerialConfiguration::supportedBaudRates()` and the same function from
`git show v5.0.8:src/Comms/SerialLink.cc`. The reference changes only its class
qualifier to `UpstreamSerialConfiguration`; both method bodies remain intact.
CMake reports the reference revision and tracks the production source for
regeneration. The existing Harmony bridge rate function is also extracted
from production, so the baseline uses its actual fixed list.

Both implementations compile together on the Windows Qt host with
`Q_OS_HARMONY` additionally defined. The reference uses its original Windows
defaults and an empty Qt list: these are the 19 official v5.0.8 choices shown
in the release's Windows UI. Production must retain those exact choices when
Harmony Qt supplies an empty list, duplicates, its 30 SDK rates, or arbitrary
extra rates. A separate check verifies that the native bridge accepts every
official UI rate, including 14400, 56000, 128000, and 256000.
There is no hardware, QtSerialPort library, GUI, or device operation.

The fixed build directory is `tmp/build-serial-baud-rates-test`; the runner
compiles one process at a time. `-Baseline` writes separate
`baseline.txt`/`baseline.xml` reports. The default reports are
`result.txt`/`result.xml`, with `test-results.xml` from CTest. Both modes use
the current production source. All generated source and build files stay in
the build directory.

This test establishes exact official Windows UI-list compatibility for the
Harmony port. It does not require Unix Qt enumeration to match Windows.
Target-library exports, actual Harmony platform rate lists,
serial protocol encoding, and physical baud accuracy have separate checks.
