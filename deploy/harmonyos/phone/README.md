# HarmonyOS 手机适配

手机、Pad 和 2in1 共用仓库主工程及同一个 ARM64 HAP。设备声明包含 `phone`、`tablet` 和 `2in1`，包名为 `org.mavlink.qgroundcontrol`。

手机使用 QGC 移动端布局、传感器横屏和前台常亮，后台释放常亮。Pad / 2in1 使用桌面布局与窗口控件。USB、MAVLink、蓝牙、视频和飞控业务共用后端。

构建与打包命令见 [HarmonyOS 构建说明](../README.md)。设备分类、能力开关和 QML 行为测试见 [HarmonyDeviceUi](../../../test/HarmonyDeviceUi/README.md)。窗口生命周期测试使用当前主工程的 `QGCWindowPolicy.ets`。

本目录保留 UDP 对端工具及测试。测试不执行飞控刷写、校准或参数写入。
