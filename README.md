# QGroundControl for HarmonyOS

<p align="center">
  <img src="https://raw.githubusercontent.com/Dronecode/UX-Design/35d8148a8a0559cd4bcf50bfa2c94614983cce91/QGC/Branding/Deliverables/QGC_RGB_Logo_Horizontal_Positive_PREFERRED/QGC_RGB_Logo_Horizontal_Positive_PREFERRED.svg" alt="QGroundControl Logo" width="500">
</p>

<p align="center">
  <a href="https://github.com/mavlink/QGroundControl/releases"><img src="https://img.shields.io/github/v/release/mavlink/QGroundControl" alt="Latest Release"></a>
  <a href="https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md"><img src="https://img.shields.io/github/license/mavlink/QGroundControl" alt="License"></a>
  <a href="https://github.com/mavlink/QGroundControl/actions/workflows/linux.yml"><img src="https://github.com/mavlink/QGroundControl/actions/workflows/linux.yml/badge.svg" alt="Linux Build"></a>
  <a href="https://securityscorecards.dev/viewer/?uri=github.com/mavlink/qgroundcontrol"><img src="https://img.shields.io/ossf-scorecard/github.com/mavlink/qgroundcontrol?label=openssf%20scorecard" alt="OpenSSF Scorecard"></a>
  <a href="https://crowdin.com/project/qgroundcontrol"><img src="https://badges.crowdin.net/qgroundcontrol/localized.svg" alt="Crowdin"></a>
  <a href="https://discord.com/channels/1022170275984457759/1022185820683255908"><img src="https://img.shields.io/discord/1022170275984457759?logo=discord&logoColor=white&label=Discord" alt="Dronecode Discord"></a>
  <a href="https://doi.org/10.5281/zenodo.595404"><img src="https://zenodo.org/badge/DOI/10.5281/zenodo.595404.svg" alt="DOI"></a>
</p>

**QGroundControl** (QGC) is a Ground Control Station (GCS) for UAVs, providing full flight control
and mission planning for any *MAVLink-enabled* drone, including *PX4* and *ArduPilot* platforms.

## Features

- **Mission planning** — plan, edit, and fly autonomous waypoint, survey, and structure-scan missions.
- **Live Fly View** — real-time flight display with map, instruments, and full vehicle telemetry.
- **Vehicle setup** — guided wizards for sensor calibration, radio, flight modes, and power.
- **Parameter tuning** — inspect and edit every vehicle parameter through the Fact System.
- **Video streaming** — GStreamer-based UDP RTP / RTSP video with recording in the Flight Display.
- **Multi-vehicle** — connect to and monitor multiple vehicles simultaneously.
- **MAVLink tooling** — built-in MAVLink Inspector, console, and log download/analysis.
- **Cross-platform** — Windows, macOS, Linux, Android, and iOS from a single codebase.

## Download

Grab the latest stable build for your platform, or see all assets on the
[releases page](https://github.com/mavlink/QGroundControl/releases/latest):

<p align="center">
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl-installer.exe"><img src="https://img.shields.io/badge/Windows-0078D6?logo=windows&logoColor=white" alt="Windows"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl.dmg"><img src="https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white" alt="macOS"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl-x86_64.AppImage"><img src="https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black" alt="Linux (AppImage)"></a>
  <a href="https://github.com/mavlink/QGroundControl/releases/latest/download/QGroundControl.apk"><img src="https://img.shields.io/badge/Android-3DDC84?logo=android&logoColor=white" alt="Android"></a>
</p>

## Links

- [Official Website](http://qgroundcontrol.com)
- [User Manual](https://docs.qgroundcontrol.com/en/)
- [Developer Guide](https://dev.qgroundcontrol.com/en/) / [Build Instructions](https://dev.qgroundcontrol.com/en/getting_started/)
- [Discussion & Support](https://docs.qgroundcontrol.com/en/Support/Support.html)
- [Dronecode Discord](https://discord.com/channels/1022170275984457759/1022185820683255908)
- [Security Policy](.github/SECURITY.md)
- [Code of Conduct](.github/CODE_OF_CONDUCT.md)
- [License](https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md)

## Contributing

QGC is open source and welcomes contributions. See [AGENTS.md](AGENTS.md) for build/test/lint
commands and coding conventions, and [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) for
architecture patterns and the contribution workflow.

QGC's interface is translated by the community — help translate it into your language on
[Crowdin](https://crowdin.com/project/qgroundcontrol).

## Star history

[![Star History Chart](https://api.star-history.com/svg?repos=mavlink/qgroundcontrol&type=Date)](https://star-history.com/#mavlink/qgroundcontrol&Date)

## HarmonyOS 移植

本项目是 [mavlink/qgroundcontrol](https://github.com/mavlink/qgroundcontrol) 的社区 fork，面向 **2in1 / Pad / Phone**。`Stable_V5.1` 分支基于官方 [QGroundControl v5.1.5](https://github.com/mavlink/qgroundcontrol/releases/tag/v5.1.5)，上游基线提交为 [`3a67d31f0c36bf3fe38ec52970d250a89d0aaf67`](https://github.com/mavlink/qgroundcontrol/commit/3a67d31f0c36bf3fe38ec52970d250a89d0aaf67)。`Stable_V5.0` 保留 v5.0.8 适配。HarmonyOS 相关问题请提交到[本 fork 的 Issues](https://github.com/avengerdsf/qgroundcontrol-for-HarmonyOS/issues)。

统一 ARM64 HAP 声明手机、平板和 2in1。手机采用 QGC 移动端布局，Pad / 2in1 采用桌面布局及原生窗口控件，共用业务与通信后端。平台适配包括 USB 串口、UDP / TCP、蓝牙 SPP / BLE、视频接收、文件导出和语言切换。视频接收使用静态链接的 GStreamer 1.28.4（GL 纹理导入、RTP / RTSP、内置 TLS 根证书），不依赖系统 GStreamer。最低声明版本为 **HarmonyOS 6.1 / API 23**。

当前处于开发验证阶段，完整迁移验收尚未完成。默认视频路径稳定性、Qt QoS、手机局部布局及日志文件回读的已知限制见 [GStreamer 视频后端](docs/harmonyos/GSTREAMER.md#已知限制)。

### 构建、安装与测试

- [HarmonyOS 构建与打包](deploy/harmonyos/README.md)
- [Qt 窗口适配补丁](deploy/harmonyos/qt/README.md)
- [自行签名与侧载安装](docs/harmonyos/自行签名与侧载安装.md)
- [通信软件测试](test/HarmonyCommunications/README.md)
- [鸿蒙通信运行测试](test/HarmonyCommunicationsRuntime/README.md)
- [GStreamer 视频后端](docs/harmonyos/GSTREAMER.md)
- [传感器校准回放](test/SensorCalibration/README.md)

### 许可证

QGC 源码沿用上游的 [Apache 2.0](LICENSE-APACHE) / [GPLv3](LICENSE-GPL) 双许可，详见[上游授权说明](.github/COPYING.md)。当前采用开源 Qt 的 HarmonyOS 构建使用 GPLv3 授权路径；二进制分发须同时满足 Qt、GStreamer（含其 LGPL/GPL 插件与 FFmpeg 编解码库）等依赖的许可证及对应源码要求。

上游文档、美术及图像资源采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。USB 串口适配中的第三方代码保留 [MIT 许可证及版权声明](deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt)。QGroundControl 的名称、标识及原有版权归各自权利人所有。
