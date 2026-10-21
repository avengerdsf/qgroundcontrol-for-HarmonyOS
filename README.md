# QGroundControl for HarmonyOS

<p align="center">
  <img src="https://raw.githubusercontent.com/Dronecode/UX-Design/35d8148a8a0559cd4bcf50bfa2c94614983cce91/QGC/Branding/Deliverables/QGC_RGB_Logo_Horizontal_Positive_PREFERRED/QGC_RGB_Logo_Horizontal_Positive_PREFERRED.svg" alt="QGroundControl Logo" width="500">
</p>

<p align="center">
  <a href="https://github.com/mavlink/QGroundControl/releases">
    <img src="https://img.shields.io/github/release/mavlink/QGroundControl.svg" alt="Latest Release">
  </a>
</p>

*QGroundControl* (QGC) is a highly intuitive and powerful Ground Control Station (GCS) designed for UAVs. Whether you're a first-time pilot or an experienced professional, QGC provides a seamless user experience for flight control and mission planning, making it the go-to solution for any *MAVLink-enabled drone*.

---

### 🌟 *Why Choose QGroundControl?*

- *🚀 Ease of Use*: A beginner-friendly interface designed for smooth operation without sacrificing advanced features for pros.
- *✈️ Comprehensive Flight Control*: Full flight control and mission management for *PX4* and *ArduPilot* powered UAVs.
- *🛠️ Mission Planning*: Easily plan complex missions with a simple drag-and-drop interface.

🔍 For a deeper dive into using QGC, check out the [User Manual](https://docs.qgroundcontrol.com/en/) – although, thanks to QGC's intuitive UI, you may not even need it!


---

### 🚁 *Key Features*

- 🕹️ *Full Flight Control*: Supports all *MAVLink drones*.
- ⚙️ *Vehicle Setup*: Tailored configuration for *PX4* and *ArduPilot* platforms.
- 🔧 *Fully Open Source*: Customize and extend the software to suit your needs.

🎯 Check out the latest updates in our [New Features and Release Notes](https://github.com/mavlink/qgroundcontrol/blob/master/ChangeLog.md).

---

### 💻 *Get Involved!*

QGroundControl is *open-source*, meaning you have the power to shape it! Whether you're fixing bugs, adding features, or customizing for your specific needs, QGC welcomes contributions from the community.

🛠️ Start building today with our [Developer Guide](https://dev.qgroundcontrol.com/en/) and [build instructions](https://dev.qgroundcontrol.com/en/getting_started/).

---

### 🔗 *Useful Links*

- 🌐 [Official Website](http://qgroundcontrol.com)
- 📘 [User Manual](https://docs.qgroundcontrol.com/en/)
- 🛠️ [Developer Guide](https://dev.qgroundcontrol.com/en/)
- 💬 [Discussion & Support](https://docs.qgroundcontrol.com/en/Support/Support.html)
- 🤝 [Contributing](https://dev.qgroundcontrol.com/en/contribute/)
- 📜 [License Information](https://github.com/mavlink/qgroundcontrol/blob/master/.github/COPYING.md)

---

With QGroundControl, you're in full command of your UAV, ready to take your missions to the next level.

---

## HarmonyOS 移植

本项目是 [mavlink/qgroundcontrol](https://github.com/mavlink/qgroundcontrol) 的社区 fork，将官方 [QGroundControl v5.0.8](https://github.com/mavlink/qgroundcontrol/releases/tag/v5.0.8) 迁移至 HarmonyOS，面向 **2in1 / Pad / Phone**。移植基线为提交 [`e0816c957602789200ae5ba0af45217f0f2f1db4`](https://github.com/mavlink/qgroundcontrol/commit/e0816c957602789200ae5ba0af45217f0f2f1db4)。HarmonyOS 相关问题请提交到[本 fork 的 Issues](https://github.com/avengerdsf/qgroundcontrol-for-HarmonyOS/issues)。

统一 ARM64 HAP 支持手机、平板和 2in1：手机采用 QGC 移动端布局，Pad / 2in1 采用桌面布局及原生窗口控件，共用业务与通信后端。平台适配包括 USB 串口、UDP / TCP、蓝牙 SPP / BLE、视频接收、文件导出和语言切换。

最低声明版本为 **HarmonyOS 6.1 / API 23**。

### 构建、安装与测试

- [HarmonyOS 构建与打包](deploy/harmonyos/README.md)
- [Qt 窗口适配补丁](deploy/harmonyos/qt/README.md)
- [自行签名与侧载安装](docs/harmonyos/自行签名与侧载安装.md)
- [通信软件测试](test/HarmonyCommunications/README.md)
- [鸿蒙通信运行测试](test/HarmonyCommunicationsRuntime/README.md)
- [GStreamer 视频后端](docs/harmonyos/GSTREAMER.md)
- [传感器校准回放](test/SensorCalibration/README.md)

### 许可证

QGC 源码沿用上游的 [Apache 2.0](LICENSE-APACHE) / [GPLv3](LICENSE-GPL) 双许可，详见[上游授权说明](docs/en/qgc-dev-guide/contribute/licences.md)。当前采用开源 Qt 的 HarmonyOS 构建使用 GPLv3 授权路径；二进制分发须同时满足 Qt、FFmpeg 等依赖的许可证及对应源码要求。

上游文档、美术及图像资源采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。USB 串口适配中的第三方代码保留 [MIT 许可证及版权声明](deploy/harmonyos/bridge/USB_SERIAL_LICENSE.txt)。QGroundControl 的名称、标识及原有版权归各自权利人所有。
