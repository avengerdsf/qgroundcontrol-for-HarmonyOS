# Qt 6.12.0 鸿蒙窗口模式补丁

普通 Pad 主窗口首次同步会绕过 Qt 的 `WindowModeOnly` 限制，调用桌面标题栏接口。无系统装饰的主窗口返回 `1300002`，同步的标题按钮调用可触发 Qt JS 任务终止及 `SIGABRT`。

补丁在属性入队和刷新时共用现有运行模式检查。普通 Pad 主窗口跳过标题、标题按钮、frameless、置顶及桌面窗口几何属性；全模式属性继续同步。2in1 和 Pad PC 模式保留现有桌面窗口操作。已排队的桌面请求在切回普通模式后清除。子窗口规则及代理 API 的错误处理保持原有行为。

`setWindowDecorVisible(false)` 控制装饰可见性。PC 主窗口隐藏标题栏时仍能更新标题按钮。官方 `WindowProperties` 无 `decorEnabled` 字段；`decorEnabled` 属于窗口创建参数。接口条件见[华为 Window API](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/arkts-apis-window-window)。

## 应用与验证

在项目根目录运行：

```powershell
./deploy/harmonyos/qt/apply-window-mode-patch.ps1 -CheckOnly
./deploy/harmonyos/qt/apply-window-mode-patch.ps1
& ./tmp/venv/Scripts/python.exe -B ./deploy/harmonyos/qt/test-window-mode-sync.py
```

测试用项目内 MinGW 编译器执行 Qt 源码中的属性提交 lambda、setter 和 flush，覆盖普通 Pad 初始与后续同步、PC 桌面属性、排队后的模式切换、旧请求清除和原有子窗规则。生成文件在项目 `tmp/` 内，用后自动删除。该测试替代外部鸿蒙窗口 API 为计数边界，设备冷启动验证仍需执行。

## Pad 冷启动窗口恢复

应用在 `QGCApplication` 构造后、QML 主窗口创建前，对鸿蒙模块声明支持的 tablet 与 2in1 调用 Qt 的 `setMainWindowGeometryPersistenceHint(Enabled)`。`0002-restore-tablet-startup-window-state.patch` 在 2in1 或 Pad PC 模式首次创建主窗口代理时，先将此提示应用到 WindowStage 并查询实际启用结果，再查询当前原生窗口模式。

系统尺寸记忆已启用时，窗口几何由系统恢复，Qt 跳过应用保存矩形的尺寸、位置请求。当前模式与请求模式一致时直接显示；模式不同时只执行所需的全屏、最大化或浮窗切换。查询失败或记忆功能未启用时，保留 QGC 的矩形恢复：原生全屏、最大化和分屏状态先恢复为浮窗，再同步保存矩形并应用最终模式。请求模式在创建代理前保存，避免原生恢复事件覆盖它。

系统尺寸记忆自 HarmonyOS 6.1 起支持自由窗口 Pad。浮窗保存大小和位置，沉浸式窗口保存进入沉浸前的浮窗矩形，QGC 继续保存最终窗口模式。首次启用且无系统历史记录时，系统可采用默认几何；正常关闭后形成后续启动记录。`isWindowRectAutoSave()` 返回当前启用状态，无法单独证明已有历史记录。系统行为及启动配置优先级见[华为 WindowStage API](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/arkts-apis-window-windowstage#setwindowrectautosave17)和[窗口元数据配置](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/window-config-m)。

状态查询使用 API 11 起提供的 `Window.getWindowStatus()`。关闭中的窗口、未知枚举和原生查询异常返回空状态，保留安全显示路径。手机、普通 Pad、子窗口及已有代理的重新显示沿用原行为。

两个补丁通过同一检查与应用入口依次处理。新增回归命令：

```powershell
& ./tmp/venv/Scripts/python.exe -B ./deploy/harmonyos/qt/test-window-startup-restore.py
& ./deploy/harmonyos/qt/test-startup-patch.ps1
```

启动回归编译并执行实际 Qt `showImmediate()`、状态查询、持久化策略及几何属性提交源码，覆盖先启用后查询、系统几何优先、已匹配模式无往返、不匹配模式单次切换、异常回退，以及设备、窗口角色和重复显示边界。补丁回归在项目临时目录中验证原始源码应用、检查无写入、重复应用及不匹配源码拒绝。系统启动页到 QGC 内容的连续位置、尺寸和模式仍需整包冷启动观测。

## 单插件构建

关闭模拟器后运行：

```powershell
./deploy/harmonyos/qt/build-platform-plugin.ps1 -PlanOnly
./deploy/harmonyos/qt/build-platform-plugin.ps1 -Install
```

构建使用既有 `tmp/qt-build/x86_64` 配置，目标为 `QOhosPlatformIntegrationPlugin`，并行数为 1。产物为 `tmp/qt-build/x86_64/qtbase/plugins/platforms/libqohos.so`。安装只替换 CMake cache 中对应 kit 的 `plugins/platforms/libqohos.so`，原文件保存在项目 `tmp/qt-plugin-backups/`。Qt 其他库保持现有产物。

默认配置与 `tmp/toolchains/Qt/6.12.0/harmonyos_x86_64` 来自同一份 Qt 6.12.0 源码、Release 构建及 OHOS toolchain。补丁仅影响平台插件内部类。

### ARM64 独立插件

```powershell
./deploy/harmonyos/qt/build-arm64-platform-plugin.ps1 -ConfigureOnly
./deploy/harmonyos/qt/build-arm64-platform-plugin.ps1 -PlanOnly
./deploy/harmonyos/qt/build-arm64-platform-plugin.ps1 -Install
```

独立入口通过已安装的 ARM64 Qt 6.12.0 公共与私有 CMake 目标链接 `Qt6Core`、`Qt6Gui`、`Qt6OpenGL`，直接读取 Qt 源码内 OHOS 平台的源文件、依赖、定义和 Vulkan 条件。Ninja 计划包含 94 步：3 项 moc/metadata、90 个插件对象及一次链接，唯一模块输出为 `tmp/qt-platform-plugin-arm64/plugins/platforms/libqohos.so`。

NodeAddonApi 和 Fontconfig 使用项目内现有的架构无关头文件。Fontconfig 的四个调用入口由 ARM64 kit 的 `libQt6Gui.so` 提供；配置阶段检查这些 `Qt_6` 版本符号。Qt 源码及 Core/Gui/OpenGL 私有头文件严格匹配 6.12.0。插件类名 `QOhosPlatformIntegrationPlugin`、`qt_plugin_query_metadata_v2`、`qt_plugin_instance` 和 `Qt_6` 导出版本节点保持现有插件约定。

ARM64 构建同样使用并行数 1，安装目标为 `tmp/toolchains/Qt/6.12.0/harmonyos_arm64_v8a/plugins/platforms/libqohos.so`。SDK、Qt 其他库和 x86_64 kit 使用现有产物。原 ARM64 插件备份存储在 `tmp/qt-plugin-backups/`。

## 设备回归标准

1. 用新插件重新打包 HAP，在普通 Pad 非 PC 模式下连续冷启动两次，每次停留至少 30 秒；进程持续存活、主界面正常显示，启动日志中不出现 `setWindowTitleButtonVisible` fatal、`promise destroyed without notifying the caller` 或新增 crash。
2. 在 2in1 或 Pad PC 模式下冷启动，核对主界面、标题按钮和最大化/恢复功能；进入全屏后返回，主窗口继续正常工作。
3. 若验证环境支持运行时 PC 模式切换，切回普通模式并触发标题或 flags 更新，核对进程存活及后续更新正常。

真实普通 Pad 崩溃基线位于 `tmp/verification/qgc-pad-pc-crash.log`。
