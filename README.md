# QGroundControl for harmonyOS

<p align="center">
  <img src="https://raw.githubusercontent.com/Dronecode/UX-Design/35d8148a8a0559cd4bcf50bfa2c94614983cce91/QGC/Branding/Deliverables/QGC_RGB_Logo_Horizontal_Positive_PREFERRED/QGC_RGB_Logo_Horizontal_Positive_PREFERRED.svg" alt="QGroundControl Logo" width="500">
</p>

QGroundControl **v5.0.8** 的 HarmonyOS 移植，面向 **2in1 / Pad / Phone**，沿用 QGC 的 PX4 / ArduPilot 配置和 MAVLink 任务管理流程。

HarmonyOS port of **QGroundControl v5.0.8** for **2in1 devices, tablets and phones**.

同一个 ARM64 HAP 声明 `phone`、`tablet` 和 `2in1`，运行时按设备选择界面：手机使用 QGC 移动端布局，Pad / 2in1 使用桌面布局和窗口控件，共用业务和通信后端。

## 上游关系

本项目是 [mavlink/qgroundcontrol](https://github.com/mavlink/qgroundcontrol) 的社区 fork。移植基线为官方 [v5.0.8](https://github.com/mavlink/qgroundcontrol/releases/tag/v5.0.8)，对应提交 [`e0816c957602789200ae5ba0af45217f0f2f1db4`](https://github.com/mavlink/qgroundcontrol/commit/e0816c957602789200ae5ba0af45217f0f2f1db4)。

QGroundControl 的名称、标识、原有版权及许可证归属沿用上游声明。该 HarmonyOS 移植由社区维护，问题请提交到本 fork 的 Issues。

## 支持范围

| 项目 | 说明 |
| --- | --- |
| 设备 | HarmonyOS 2in1 / Pad / Phone；实体设备构建使用 ARM64 |
| 界面 | 手机移动端布局；Pad / 2in1 桌面界面、原生标题栏、窗口置顶、全屏与窗口状态记忆 |
| USB | 原生串口及 USB Host 后端，包含 CDC、多 CDC、CP210x、CH34x、FTDI 驱动适配 |
| 网络 | UDP / TCP MAVLink 连接 |
| 蓝牙 | SPP 与 BLE / GATT 后端；具体 BLE 串口服务约定见桥接代码和测试文档 |
| 日志与语言 | 鸿蒙文件导出、默认日志文件名、上游已覆盖译文的语言切换 |
| 视频 | HarmonyOS FFmpeg 接收后端，构建入口为 [build-ffmpeg.ps1](deploy/harmonyos/build-ffmpeg.ps1) |

最低声明版本为 **HarmonyOS 6.1 / API 23**。当前运行验证主要来自 API 26 的 Pad 与 2in1 仿真环境；API 23 未完成运行验收。`x86_64` 构建用于仿真器，实体 2in1 / Pad 使用与设备匹配的 ARM64 包。

USB 芯片、蓝牙和其他外设的适配包含软件测试，实物覆盖随设备型号变化。当前仍有完整测试输出、压力测试及个别实机现象待补充，项目尚未宣称所有飞控与外设完成验收。加速度计校准控制器、六面校准页面和校准命令保持官方 v5.0.8 逻辑。

## 构建

使用 **Qt 6.12.0 HarmonyOS**、对应的桌面 Qt 工具、华为 DevEco Studio SDK 和官方 **DevEco CLI**。Qt 平台插件需要应用仓库提供的[窗口适配补丁](deploy/harmonyos/qt/README.md)。依赖、缓存、生成工程及 HAP 存放在项目 `tmp/` 下。

原生构建、打包参数、签名配置保留和设备运行命令见 [HarmonyOS 构建说明](deploy/harmonyos/README.md)。打包示例：

```powershell
./deploy/harmonyos/package.ps1 -Architecture arm64 -Action Build -BuildMode release `
    -BuildDirectory tmp/build-v5.0.8-harmonyos-release-arm64 `
    -VersionCode 5010538 `
    -DevEcoPath '<DevEco Studio 安装目录>' `
    -DevEcoCli '<devecocli.ps1 路径>'
```

该命令需要先完成同架构、同模式的原生构建。生成工程位于所选构建目录的 `harmonyos-build`，模块为 `entry`，Ability 为 `QAbility`，包名为 `org.mavlink.qgroundcontrol`。

## 未签名 HAP 与侧载

ARM64 未签名包采用以下命名，突出适用设备：

```text
QGroundControl-5.0.8-harmonyOS-2in1-Pad-Phone-arm64-<versionCode>-unsigned.hap
```

未签名 HAP 需要使用安装者自己的配套证书、Profile 和密钥完成签名。步骤见[自行签名与侧载安装](docs/harmonyos/自行签名与侧载安装.md)。同包名更新需要兼容的签名身份，并应保留已有应用数据。

HAP、签名材料、个人配置及构建缓存不提交到源码仓库。

## 测试

- [通信软件测试](test/HarmonyCommunications/README.md)：UDP / TCP、SPP、BLE 和可选 USB 回归。
- [鸿蒙通信运行测试](test/HarmonyCommunicationsRuntime/README.md)：独立测试包与 NAPI 运行边界。
- [传感器校准回放](test/SensorCalibration/README.md)：六面状态、进度、分段状态消息和既有校准日志。
- [Qt 窗口补丁测试](deploy/harmonyos/qt/README.md)：窗口模式、启动恢复及几何持久化。

测试文档区分宿主软件测试、仿真器运行和实体设备验证；构建成功只证明对应构建与产物检查通过。

## 上游文档与许可证

- [QGroundControl 官网](https://qgroundcontrol.com/)
- [用户手册](https://docs.qgroundcontrol.com/en/)
- [开发文档](https://dev.qgroundcontrol.com/en/)
- [上游许可证](.github/COPYING.md)
- [USB 串口适配第三方声明](deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt)
