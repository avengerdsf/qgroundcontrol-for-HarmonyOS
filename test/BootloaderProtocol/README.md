# Bootloader command framing regression

`./test/BootloaderProtocol/run-tests.ps1 -Baseline` records current-source RED in
`tmp/build-bootloader-protocol-test/baseline.xml`; the default command records
GREEN in `result.xml` and CTest `test-results.xml`. Builds are serial and use only
Qt Core/Test, without QGC, NAPI, a HAP or a device. All generated files stay in
project tmp.

CMake extracts the complete production `_binProgram`, both binary `_write`
overloads, `_getCommandResponse`, protocol constants and actual QGC CRC32 code.
QFile reads a real 65540-byte nonrepeating image. The controlled device boundary
queues public writes immediately and models downstream transfer completions with
a virtual 60ms clock. Peer command-to-length acceptance is limited to 50ms,
payload byte gaps to 1000ms and EOC to 200ms, following the public PX4 Bootloader
PROG_MULTI parser. This model requires no actual delay across 1025 frames.

The old four-write command returns PROTO_INVALID in this model. The Harmony
complete-frame command passes full bytes, lengths, alignment, EOC, final short
block, progress and an independently calculated image-plus-FF-padding CRC.
Four short-write cases reject missing EOC, payload tail, header-only and zero
writes. An injected PROTO_INVALID at 0xf980 verifies exact error address and
stopped progress. The real asynchronous Bridge/IODevice path is exercised
separately by HarmonyRtkFirmwareTest's delayed-provider case.

A 67-byte write can span USB packets (for example, 64+3 at MPS64). It removes
application/API completion boundaries between command and length; it does not
make USB transfers physically atomic. The real board's exact bootloader build
and wire timing remain unknown. This RED/GREEN demonstrates the transport risk
and candidate mitigation, without proving the observed Pad failure is fixed.

Protocol reference: [PX4 Bootloader PROG_MULTI](https://github.com/PX4/PX4-Bootloader/blob/master/bl.c#L734).
