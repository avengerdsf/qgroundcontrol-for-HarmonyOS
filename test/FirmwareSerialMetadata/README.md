# Firmware serial metadata regression

Run `./test/FirmwareSerialMetadata/run-tests.ps1 -Baseline` before the type fix,
then `./test/FirmwareSerialMetadata/run-tests.ps1` for GREEN. Both runs use current
production source; RED XML is kept separately in
`tmp/build-firmware-serial-metadata-test/baseline.xml`, with GREEN in `result.xml`
and CTest `test-results.xml`. The runner builds one small Qt Core/Test target
serially and keeps all generated files/cache inside project tmp.

CMake extracts complete production `_foundBoard` and `_buildAPMFirmwareNames`
methods, QGCSerialPortInfo constructors/enumeration, plus the production slot
declaration, member type, QObject properties and accessors. The hardware port
enumeration and settings are controlled boundaries; QtSerialPort is an empty
metadata surface, matching Harmony's empty Qt base object. It never opens a port
or starts a network request. The hardware enum shell reuses SerialBoardInfo's
existing fixture. This is a behavior test, with no replacement matching method.

Four functional cases verify retained QObject board properties, a matching
bootloader's best firmware index/URL, and two unchanged manual-selection
fallbacks. The full QGC `HarmonyRtkFirmwareTest` separately tests the actual
controller object with a controlled HarmonySerialProvider. Its unit-test-only
constructor guard skips the two online metadata downloads; all other normal
construction remains. These cases do not call `startBoardSearch` or `flash` and
assert zero provider I/O operations. Physical firmware flashing is outside this
test's scope.
