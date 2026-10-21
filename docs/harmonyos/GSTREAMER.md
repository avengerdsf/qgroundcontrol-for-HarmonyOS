# HarmonyOS GStreamer 视频后端

GStreamer 1.28.4 以静态库接入 QGC，支持 RTP/UDP、MPEG-TS、RTSP UDP/TCP、RTSPS、录像与回放。H.264/H.265、VP8/VP9 的软件解码与 EGL/GLES 渲染纳入仿真验证。QGC 5.0.8 保留 `qml6glsink` 渲染架构，5.1.5 保留 `QVideoSink` 与 GL 纹理导入架构。

## 构建

先准备 [HarmonyOS 工具链](../../deploy/harmonyos/README.md)。SDK 构建使用单编译任务，来源版本和 SHA256 固定在 [sources-lock.json](../../deploy/harmonyos/gstreamer/sources-lock.json) 与 [codecs-lock.json](../../deploy/harmonyos/gstreamer/codecs-lock.json)。锁定依赖包含 GLib、OpenSSL、FFmpeg、OpenH264、libvpx、x264 与 x265。

从当前分支仓库根目录运行：

```powershell
./deploy/harmonyos/build-gstreamer.ps1 -Architecture arm64 -DependencyDirectory '<工具链依赖目录>'
./deploy/harmonyos/build-gstreamer.ps1 -Architecture x86_64 -DependencyDirectory '<工具链依赖目录>'
```

依赖目录需包含 `venv/Scripts/python.exe`、Qt 主机工具、OHOS SDK、vcpkg 及对应构建工具。SDK 输出为 `tmp/gstreamer/1.28.4/<architecture>/install`。`-Component` 可选择单个组件；完整构建按依赖顺序执行全部组件。

原生 QGC 的 CMake 配置使用以下选项，并选择匹配的 Qt HarmonyOS 工具链和 SDK 架构：

```text
-DQGC_ENABLE_GST_VIDEOSTREAMING=ON
-DQGC_FFMPEG_STREAMING=OFF
-DQGC_GST_OHOS_GL=ON
-DGStreamer_ROOT_DIR=<SDK install 绝对目录>
```

原生 Release 构建完成后，使用打包脚本的 `-BuildMode release`、`-Architecture arm64` 和对应 `-BuildDirectory`。安装版本号通过 `-VersionCode` 指定。侧载文件的签名方式见 [自行签名与侧载安装](自行签名与侧载安装.md)。

## 平台处理

- EGL 函数通过系统 EGL 分发；OpenGL 桌面后端不参与 HarmonyOS 构建。
- GLES 的 RGB16/RGBA16 纹理依赖 `GL_EXT_texture_norm16`。缺少扩展时转换输入格式后上传，保留正常 PBO 路径。
- 原生总线在观察到小幅时钟倒退时将负间隔归零，避免无符号溢出引发立即超时；测试可注入倒退、相等和前进时钟，Release 使用真实时钟。
- OpenSSL GIO 后端和 CA bundle 静态集成。RTSPS 默认启用证书验证；测试 CA 仅用于测试服务器正例。
- 打包附带组件许可证、来源地址与 SHA256。`libEGL.so`、`libGLESv2.so` 的 SDK 链接桩不进入 HAP，GStreamer 归档静态链接到应用。

## 验证

QtTest 使用独立 `org.mavlink.qgroundcontrol.tests` 测试包。运行时明确指定仿真器，禁止多设备环境下自动选择目标：

```powershell
$env:QT_HARMONYOS_DEVICE = '<仿真器序列号>'
$env:QT_HARMONYOS_HDC = '<SDK>/toolchains/hdc.exe'
ctest --test-dir '<原生测试构建目录>' --parallel 1 --output-on-failure
```

视频专项覆盖协议接收、真实解码帧、像素颜色、录制后解码回放、连接恢复、GL 纹理导入及 TLS 正反例。完整回归从构建根目录运行，以覆盖附加 CMake 配置测试。

测试中只适用于 Windows、macOS、Linux、Android 等后端的项目按名称和原因列为不适用，不计作通过。软件验证与 ARM64 构建验证分别记录；外设、驱动和具体设备的视频性能另行实测。

## 已知限制

当前普通 GUI 在 API 26 电脑形态仿真器接收 Mock H.264 时，默认视频路径出现 `not-negotiated` / `No caps set`，持续重连后曾在 `gstglcontext` 的 EGL 上下文创建路径发生崩溃。启用现有的 `Force CPU video path` 设置并重启应用后，同一模拟流可显示动态画面；默认路径的稳定性仍待修复。

Qt QoS 调用失败仍影响部分手机界面的严格日志测试。手机部分页面存在文字截断，应用日志导出文件的回读尚待确认。专项测试通过不代表这些普通 GUI 项目通过；完整迁移验收仍未完成。
