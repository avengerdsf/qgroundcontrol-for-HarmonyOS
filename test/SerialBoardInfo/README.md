# Serial board safety regression

Run `./test/SerialBoardInfo/run-tests.ps1` from the repository root. The small Qt
6.12 host target builds serially in `tmp/build-serial-board-info-test`. It does
not build the full application, open a serial device, or invoke HDC.

CMake extracts the current production QGCSerialPortInfo constructors,
availablePorts and isBlackCube methods, uses the actual production header, and
compiles the complete APMAutoPilotPlugin CubeBlack warning method. The native
enumeration result and vehicle/parameter/application service boundaries are
controlled fixtures. The host Qt installation has no SerialPort module; small
hardware interface shells supply portName and the enum types used by the real
Harmony metadata header. The SerialLink fixture has no Qt serial port, matching
the Harmony native serial backend.

Cases verify the official case-sensitive CubeBlack description rule, exact
configured-port selection, absent/non-serial links, missing ports/parameters,
and the original INS_ACC3_ID/INS_GYR3_ID/INS_ENABLE_MASK warning conditions.
They observe whether the production method delivers the service bulletin.
Physical CubeBlack identification and platform enumeration remain integration
checks for an installed application.

`-Baseline` writes separate baseline XML/text results for the intentional RED
run. Source bodies are always extracted from the working tree.
