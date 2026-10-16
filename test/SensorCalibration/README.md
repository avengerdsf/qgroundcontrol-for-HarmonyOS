# PX4 传感器校准主机测试

运行：

```powershell
& E:/tmp/QGC/test/SensorCalibration/run-tests.ps1
```

需要项目内 Qt 6.12.0 MinGW、MinGW 13.1、Python 虚拟环境和已有 QGC MAVLink 生成头文件。默认读取 ARM64 构建缓存中的 MAVLink 源目录；可用 `-MavlinkIncludeRoot` 显式指定。构建串行执行，结果位于 `tmp/build-sensor-calibration-test/`：`result.txt`、原生 JUnit `result.xml`、CTest `test-results.xml` 和含源码哈希的 `result.json`。

测试直接编译完整生产 `SensorsComponentController.cc/.h` 与 `StatusTextHandler.cc/.h`。真实 MAVLink 打包、CRC、字节流解析和分块重组将文本发送给控制器，真实 `QQuickItem` 接收属性更新。Vehicle、FactPanelController、ParameterManager、应用提示和日志注册使用必要的外围服务壳。测试记录校准启动、取消和其他命令调用次数。

覆盖六面三种顺序与零／非零消息编号、第二面到第三面无需追加命令、46～49 字节非零编号原文与 50 字节非分块原文、进度值、重复面、无效前后缀／面名／Vehicle ID、完成／取消／失败、分块完成／缺块／超时。

`accel-replay.json` 保存真实 Pad 两轮加速度计校准的 88 条 `[cal]` 文本，保留原始消息编号、分块序号、严重等级、系统与组件编号及相对时间；不包含位置和其他遥测数据。第一轮回放验证第三个 back 面完成、进度为 51%；第二轮验证第二个 front 面完成后进度为 34%，飞控报告静止／运动期间界面保持校准状态，收到取消消息才退出。回放按原始顺序执行，时间字段不用于实时等待。

本测试不执行 USB／蓝牙硬件读写、完整 Vehicle 服务、Pad 界面渲染或实体飞控校准，不能据此声明实物问题已修复。生产解析器的畸形 `calibration started: 2` 输入存在单独的数组越界风险；当前日志未出现该输入，测试未向应用发送此畸形消息。
