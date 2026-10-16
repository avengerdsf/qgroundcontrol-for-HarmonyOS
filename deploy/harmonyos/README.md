# QGroundControl for harmonyOS — 2in1 / Pad / Phone

Community fork of [mavlink/qgroundcontrol](https://github.com/mavlink/qgroundcontrol), based on official [v5.0.8](https://github.com/mavlink/qgroundcontrol/tree/v5.0.8) (`e0816c957602789200ae5ba0af45217f0f2f1db4`). The project overview and validation scope are in the [repository README](../../README.md).

The application selects the official QGroundControl v5.0.8 mobile QML layout on phones and the desktop layout on tablets and 2in1 devices. One ARM64 HAP declares `phone`, `tablet` and `2in1`, with minimum HarmonyOS 6.1 / API 23. Backend code is shared. Phone orientation is applied at runtime without constraining desktop windows.

## Toolchain

- Qt 6.12.0: `tmp/toolchains/Qt/6.12.0/harmonyos_x86_64`, `harmonyos_arm64_v8a`, and host `mingw_64`.
- Huawei DevEco Studio with its HarmonyOS SDK, Java and Node.js runtimes; select its installation with `-DevEcoPath`.
- Official DevEco CLI; select its installed launcher with `-DevEcoCli`.
- Native build directories: `tmp/build-v5.0.8-harmonyos-x86_64` and `tmp/build-v5.0.8-harmonyos-arm64`.

The native CMake build must produce `libQGroundControl.so` and `QGroundControl-harmony-deployment-settings.json` in the selected build directory. Its `CMAKE_BUILD_TYPE` must match the selected package mode: `Debug` for `-BuildMode debug`, or `Release` for `-BuildMode release`. Deployment settings must declare version `5.0.8`, device types `phone`, `tablet` and `2in1`, and compatible SDK `6.1.0(23)`.

`-BuildDirectory` selects another native build directory inside this repository's `tmp`; relative paths are resolved against the repository root. `-BundleName` must match the identity already declared by that build's deployment settings. Its default is `org.mavlink.qgroundcontrol`. The driver retains the native build's identity during packaging.

Existing native input and packaging output paths must have regular ancestors and files. The driver checks the `tmp` root, writable cache directories, and wrapper output trees before tools write to them, and checks staged output again before changing metadata. Official ohpm dependency links and Hvigor workspace `node_modules` are retained as read dependencies.

## Package and run

Run from the repository root in PowerShell:

```powershell
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Build
./deploy/harmonyos/package.ps1 -Architecture arm64 -Action Build
```

Script defaults reflect the development machine. On another machine, pass `-DevEcoPath '<DevEco Studio installation>'` and `-DevEcoCli '<devecocli.ps1 path>'`. Place the matching Qt kits under the project paths above.

`Stage` prepares the Qt wrapper and native dependencies without building a HAP. The wrapper's `abiFilters` contains only the selected architecture. `Build` stages the selected native output and calls `devecocli build --modules entry`. The generated project is `<native-build>/harmonyos-build`; its runnable module is `entry`, with ability `QAbility`.

Cold creation restores the saved window mode from the application's existing Qt INI before loading the Qt page. System geometry memory retains the floating size and position. The official `REQUIRED_HIDE` starting-window profile suppresses the earlier system splash on 2in1 and tablets in free-window mode; its QGC icon and background remain configured for other system configurations. Stage restoration keeps the mode already restored by the system.

`-VersionCode` sets the generated wrapper's internal installation version code. It defaults to `5000800` and accepts integers from `1` to `2147483647`. Version name and native source remain `5.0.8`. For a simulator that already has a higher version code, choose an explicit higher code to update the existing bundle while preserving its data:

```powershell
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Build -VersionCode 5010501
```

The separate QtTest build uses its own directory and bundle:

```powershell
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Build -BuildDirectory tmp/build-v5.0.8-harmonyos-tests-x86_64 -BundleName org.mavlink.qgroundcontrol.tests
```

```powershell
. ./deploy/harmonyos/deveco-env.ps1
& 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' device list
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Run -Device '<serial>'
& 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' log --device '<serial>' --bundle-name org.mavlink.qgroundcontrol --level E --from 5m --tail 200
```

`Run` stages and builds the selected v5.0.8 output, verifies its native-library architecture, then installs and launches it. Installation follows the target device's signing requirements. Qt's `QT_HARMONYOS_SIGNING_*` environment variables are inherited by staging; signing credentials remain outside source control.

`-BuildMode release` requires a native `Release` build. `-DevEcoCli` selects the installed CLI executable. `-DevEcoPath` sets the environment's Java, Node.js, and SDK paths; DevEco CLI 1.2.0 discovers its build tools separately through the installed DevEco Studio registry entry. Temporary files, user caches, npm, and Hvigor data are redirected to `tmp/cache` by `deveco-env.ps1`.

## Validation

The packaging driver checks the build directory, bundle identity, version, architecture, native-library location, native build mode, device types, and minimum SDK before staging. It checks the generated AppScope version fields and bundle, then checks the module for unresolved template markers, the required device types, and a single matching ABI. After building, every HAP must retain the selected version code, version name `5.0.8`, and selected bundle, and contain the application library; all `.so` entries must use the selected ABI directory and contain matching 64-bit ELF machine headers. These checks run before installation.

SDK 26.0.0.105 is installed locally. API 23 runtime compatibility and the complete v5.0.8 desktop feature set require device validation. A successful package build alone does not establish those results.

## Local unsigned packages

Unsigned ARM64 delivery files use `QGroundControl-5.0.8-harmonyOS-2in1-Pad-Phone-arm64-<versionCode>-unsigned.hap`. Device declarations are `phone`, `tablet` and `2in1`. Keep HAP files, generated projects and signing credentials outside Git under the project `tmp/` directory. Signing and sideloading instructions are in [自行签名与侧载安装](../../docs/harmonyos/自行签名与侧载安装.md).
