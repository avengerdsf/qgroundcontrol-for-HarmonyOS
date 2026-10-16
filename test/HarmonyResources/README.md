# Qt 大资源一致性检查

使用 Python 标准库比较七个 QGC 资源的旧 AUTORCC C++ 数组与 `qt_add_big_resources` 最终 ELF 对象。每个资源检查 `qt_resource_data`、`qt_resource_name`、`qt_resource_struct` 的全部字节、长度和 SHA-256，并确认对应的 `qInitResources_*` 函数已定义。拒绝 pass1 占位对象和缺失的数组。

```powershell
python -B test/HarmonyResources/verify-big-resources.py `
  --build-root E:/tmp/QGC/tmp/build-v5.0.8-harmonyos-release-arm64 `
  --output E:/tmp/QGC/tmp/verification/window-unified-532-arm64-resource-equivalence.json
```

输入为构建目录根部的七个 `qrc_*.o` 及 `QGroundControl_autogen/*/qrc_*.cpp`。支持 ARM64 和 x86_64 的小端 ELF64 ET_REL；按符号的节索引与节内偏移定位数组。按行、分块读取，不载入整份 C++ 或对象。退出码 `0` 表示全部一致，`1` 表示检查失败。报告写入项目 `tmp/`。

检查依赖普通 AUTORCC 生成的 C++ 对照文件。全新空构建目录使用 `qt_add_big_resources` 时不会生成这些对照文件，需先准备基准。生成基准应使用该构建的同一 Qt `rcc`、同一 `qrc` 和资源文件、相同压缩及格式选项，以普通 C++ 模式输出到上述对照路径。保持资源文件内容、`mtime` 和该构建的 `SOURCE_DATE_EPOCH` 设置不变，确保结构数组中的时间字段可比较。
