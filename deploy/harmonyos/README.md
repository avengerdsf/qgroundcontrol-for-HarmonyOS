# QGroundControl for harmonyOS — 2in1 / Pad / Phone

Community fork of [mavlink/qgroundcontrol](https://github.com/mavlink/qgroundcontrol), based on official [v5.1.5](https://github.com/mavlink/qgroundcontrol/tree/v5.1.5) (`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`). The project overview and validation scope are in the [repository README](../../README.md).

The application selects the official QGroundControl v5.1.5 mobile QML layout on phones and the desktop layout on tablets and 2in1 devices. One ARM64 HAP declares `phone`, `tablet` and `2in1`, with minimum HarmonyOS 6.1 / API 23. Backend code is shared. Phone orientation is applied at runtime without constraining desktop windows.

## Toolchain

The source checkout must include the official `v5.1.5` Git tag. QGC derives its application version from Git. Fetch the tag after checking out this fork:

```powershell
git fetch https://github.com/mavlink/qgroundcontrol.git tag v5.1.5
```

- Qt 6.12.0: `tmp/toolchains/Qt/6.12.0/harmonyos_x86_64`, `harmonyos_arm64_v8a`, and host `mingw_64`.
- Huawei DevEco Studio with its HarmonyOS SDK, Java and Node.js runtimes; select its installation with `-DevEcoPath`.
- Official DevEco CLI; select its installed launcher with `-DevEcoCli`.
- Native build directories: `tmp/build-v5.1.5-harmonyos-x86_64` and `tmp/build-v5.1.5-harmonyos-arm64`.

The native CMake build must produce `libQGroundControl.so` and `QGroundControl-harmony-deployment-settings.json` in the selected build directory. Its `CMAKE_BUILD_TYPE` must match the selected package mode: `Debug` for `-BuildMode debug`, or `Release` for `-BuildMode release`. Deployment settings must declare version `5.1.5`, device types `phone`, `tablet` and `2in1`, and compatible SDK `6.1.0(23)`.

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

QGC loads its compiled `qrc:/qml` interface and embedded resources. Staging scans the complete QML source tree and deploys its dependencies, then removes the duplicate application source tree at `entry/src/main/resources/rawfile/qml`. Native libraries, Qt QML import assets under `resfile/qml`, embedded parameter metadata and 3D assets, and third-party notices remain included. Source removal is checked before accepting a HAP; package equivalence checks are documented under [HarmonyResources](../../test/HarmonyResources/README.md).

Map tiles and user-created offline map sets are stored separately from the HAP. The upstream [3D View](../../docs/en/qgc-user-guide/viewer_3d/viewer_3d.md) requires an imported OpenStreetMap `.osm` file; it does not automatically load the source repository's sample map. Without an imported map or vehicle data, its scene can be empty. See also [Offline Maps](../../docs/en/qgc-user-guide/settings_view/offline_maps.md).

Cold creation restores the saved window mode from the application's existing Qt INI before loading the Qt page. System geometry memory retains the floating size and position. The system `REQUIRED_SHOW` starting-window profile displays the official QGC icon on a dark loading background while Qt initializes. The system dismisses this page when application content is ready. Stage restoration keeps the mode already restored by the system.

Pad / 2in1 use system maximization with the application pin beside the native maximize, minimize and close buttons. Saved desktop fullscreen states restore as maximized. Phones retain the QGC mobile fullscreen layout.

The Qt surface extends through the bottom system safe area. System maximization retains the native caption and status bar, and the navigation indicator overlays application content.

`-VersionCode` sets the generated wrapper's internal installation version code. It defaults to `5011501` and accepts integers from `1` to `2147483647`. Version name and native source remain `5.1.5`. For a simulator that already has a higher version code, choose an explicit higher code to update the existing bundle while preserving its data:

```powershell
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Build -VersionCode 5011501
```

The separate QtTest build uses its own directory and bundle:

```powershell
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Build -BuildDirectory tmp/build-v5.1.5-harmonyos-tests-x86_64 -BundleName org.mavlink.qgroundcontrol.tests
```

```powershell
. ./deploy/harmonyos/deveco-env.ps1
& 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' device list
./deploy/harmonyos/package.ps1 -Architecture x86_64 -Action Run -Device '<serial>'
& 'C:/Users/chen/AppData/Roaming/npm/devecocli.ps1' log --device '<serial>' --bundle-name org.mavlink.qgroundcontrol --level E --from 5m --tail 200
```

`Run` stages and builds the selected v5.1.5 output, verifies its native-library architecture, then installs and launches it. Installation follows the target device's signing requirements. Qt's `QT_HARMONYOS_SIGNING_*` environment variables are inherited by staging; signing credentials remain outside source control.

`-BuildMode release` requires a native `Release` build. `-DevEcoCli` selects the installed CLI executable. `-DevEcoPath` sets the environment's Java, Node.js, and SDK paths; DevEco CLI 1.2.0 discovers its build tools separately through the installed DevEco Studio registry entry. Temporary files, user caches, npm, and Hvigor data are redirected to `tmp/cache` by `deveco-env.ps1`.

## Validation

Cross-device application continuation is disabled for QGC. Packaging sets the main Ability's `continuable` declaration to `false` and verifies it in the built HAP.

The packaging driver checks the build directory, bundle identity, version, architecture, native-library location, native build mode, device types, and minimum SDK before staging. It checks the generated AppScope version fields and bundle, then checks the module for unresolved template markers, the required device types, and a single matching ABI. After building, every HAP must retain the selected version code, version name `5.1.5`, and selected bundle, and contain the application library; all `.so` entries must use the selected ABI directory and contain matching 64-bit ELF machine headers. These checks run before installation.

SDK 26.0.0.105 is installed locally. API 23 runtime compatibility and the complete v5.1.5 desktop feature set require device validation. A successful package build alone does not establish those results.

## Local unsigned packages

Unsigned ARM64 delivery files use `QGroundControl-5.1.5-harmonyOS-2in1-Pad-Phone-arm64-<versionCode>-unsigned.hap`. Device declarations are `phone`, `tablet` and `2in1`. Keep HAP files, generated projects and signing credentials outside Git under the project `tmp/` directory. Signing and sideloading instructions are in [自行签名与侧载安装](../../docs/harmonyos/自行签名与侧载安装.md).
