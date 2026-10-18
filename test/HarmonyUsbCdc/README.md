# HarmonyOS USB serial host tests

Run from the repository root:

```powershell
./test/HarmonyUsbCdc/test-all.ps1
./test/HarmonyUsbCdc/test-all.ps1 -IncludeUpstream
```

The suite executes the actual bridge, chip protocol modules and descriptor parser with the local DevEco SDK TypeScript package. The bridge uses a mock `@kit.BasicServicesKit`; protocol tests check independent control-transfer transcripts and packet vectors. No emulator or connected device is used. The SDK declaration check emits no files. Full ArkTS validation remains part of the HarmonyOS app build.

`-IncludeUpstream` downloads SHA256-pinned sources and Java dependencies into the project `tmp` directory, runs the unmodified v3.8.1 CDC and FTDI JUnit classes, and removes its temporary checkout and dependencies. These are the upstream host tests. Upstream `DeviceTest` and `CrossoverTest` require physical USB devices and are excluded from host verification.

`QGC_TYPESCRIPT` can override the TypeScript package location. `QGC_ETS_SDK` can override the SDK `ets` directory for `--sdk-types`. The default package is `D:/Progrem/Huawei/DevEco Studio/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript`. `QGC_TEST_FILTER` selects tests by name; `QGC_BRIDGE_SOURCE` selects a saved bridge source for comparison.

Coverage includes native capability 801, native port preservation, descriptor qualification, stable Int32 IDs and bounded caches, re-enumeration, shared and denied authorization, line coding, DTR/RTS, cancellation, short transfers, partial timeouts and replacement sessions. Multiport cases verify independent interface claims, configuration isolation and one shared device pipe retained through the final callback and shutdown.

Supported raw USB protocols:

| Protocol | Identification and scope |
| --- | --- |
| CDC ACM | One unambiguous control/data pair, or multiple pairs resolved using Union/IAD descriptors in the selected configuration. Missing or conflicting associations fail before interface claims. |
| CP210x | `10c4:ea60`, `10c4:ea70`, `10c4:ea71`; CP2105 second-port framing restrictions are enforced. |
| CH340/CH341A | `1a86:7523`, `1a86:5523`; vendor initialization, framing, inverted DTR/RTS and the 921600 baud divisor are covered. |
| FTDI UART | `0403:6001`, `0403:6010`, `0403:6011`, `0403:6014`, `0403:6015`; per-channel requests and status-byte removal at each USB packet boundary. Uses the upstream 3MHz divisor range; rates above 3M and excessive divisor error are rejected. MPSSE/FIFO and unidentified OEM VID/PIDs are excluded. |

Enumeration requests no permission and opens no device. Composite descriptor resolution occurs after authorization. Native serial ports retain priority over raw USB for the same device. Driver protocol references are the USB serial library pinned by QGC v5.0.8: `usb-serial-for-android v3.8.1`. Its MIT notice is in `deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt` and included in the HAP.

Physical-device verification covers OEM authorization and USB callbacks, actual baud rates, MAVLink reception, multiport coexistence, hotplug and each board's bootloader path. Host test results do not constitute physical-device acceptance.
