# QGroundControl v5.0.8 鸿蒙仿真迁移发布评估报告

报告日期：2026 年 10 月 15 日  
评估时点：2026-10-15 18:47:12 +0800  
当前基线：QGC 5.0.8 / versionCode 5010523 / Qt 6.12.0  
发布判定：**5010523 Release 已安装 Pad，置顶与窗口专项实测通过；5010522 固件刷写获用户成功确认。FTP 完成证据、20 轮压力与当前完整 UI 尚未全部闭环。**

## 1 结论与适用范围

5010523 已具备可交付的 ARM64 Release 签名与未签名产物，包内 142 个 ARM64 原生库及应用 Build ID 已核对。签名包已安装到 API26 Pad，应用 `debug=false`，置顶开关、冷启动保存、自由窗口重叠与返回沉浸全屏均有实测证据。
用户在 5010522 上确认同板固件刷写成功，并反馈同线同连接的 Windows 明显更快。成功截图和成功全程原始日志未取得，用户陈述与此前 67B 帧采样分别保留；5010523 未重新执行物理固件刷写。

5010522 测试包全 49 类首次 CTest 结果为 47 PASS、1 上游 SKIP、1 FAIL；唯一失败为视频 runner 的“bundle not installed”启动检查，随后独立复验通过。FTP 输出未闭合，CTest 状态不能证明整类完成。压力仅两轮完整通过，第三轮目标断连；当前通用 Qt/QML 全流程仍未重验。

| 验收层级 | 当前结论 | 结论边界 |
| --- | --- | --- |
| 主机软件 | 21 步通信批次、置顶 8 次调用、窗口启动 12 场景、校准回放 40 次调用通过 | 分批证据；步骤、调用与场景不可混合累计 |
| API24 / x86_64 仿真目标 | 5010522 相关 NMEA、网络、RTK/Bootloader 专题通过 | 按来源版本/哈希沿用，配置与设备 provider 夹具边界保留 |
| 5010523 / API26 Pad | 安装、启动、置顶与窗口专项通过 | 5010522 的单板刷写成功单列，不冒充 523 刷写复验 |
| 5010522 全 49 类 | 48 类有完整 native 结果；FTP 仅部分执行证据 | 保留首次视频启动失败及复验，不将 CTest 状态替代原生完成标志 |
| 20 轮压力、当前完整 UI | 未闭环 | 以本轮退出码、XML、逐项交互证据判定 |
| API23 | 用户明确豁免本轮实际运行验收 | HAP 的 minimumApi=23 仅为兼容声明 |

软件协议、状态机与界面逻辑可使用 MockLink、受控对端、宿主测试和仿真器验收。实物型号矩阵独立记录，不以未具备全部外设阻止软件阶段验收继续推进。

## 2 当前源码、产物与环境

| 项目 | 记录 |
| --- | --- |
| 官方基线 | `v5.0.8`，提交 `e0816c957602789200ae5ba0af45217f0f2f1db4` |
| 生产应用 | `org.mavlink.qgroundcontrol` / 5.0.8 / 5010523 / ARM64 Release |
| 原生构建输入 | `tmp/build-v5.0.8-harmonyos-release-arm64/Release/libQGroundControl.so` |
| Release 应用 Build ID | `ffcd7890f3c3916dddb96b58878f8f6526db16b4`；与 HAP 内库一致 |
| 签名 HAP | 436,611,860 字节；142 个 ARM64 库；已安装 Pad |
| 未签名 HAP | 434,119,713 字节；142 个 ARM64 库；独立归档，不带签名构建通过声明 |
| 已执行完整测试包 | `org.mavlink.qgroundcontrol.tests` / 5010522 / x86_64 Debug / 145 个原生库；按其来源范围沿用 |
| 测试应用 Build ID | `2560bd6b56ca8830b6dc4d35a095f5b120c17891` |
| 目标环境 | API24 / x86_64 平板仿真器；API26 / ARM64 Pad |
| 源码封存 | 5010523 的 schema 5 审计已冻结；109 项（62 修改 + 47 新增）哈希全部匹配、遗漏 0；Release 与旧 Debug 测试产物分别关联 |

签名 HAP SHA256：
```text
b23d18b105b71994dede1c31479455b01c4a156729900098fe3f2655d0fee2a0
```
未签名 HAP SHA256：
```text
8d7ab794573c26dc44516b0c45b6ba53da42f17db10198c55f114f8d1ddbb717
```
产物 checker 核对部署设置中的实际 Release 库、ABI、Build ID、桥接源码/工厂、权限、启动图标与许可证，并记录构建日志及软件报告哈希。
`SignHap` 任务成功、签名文件选择和安装成功均有记录；checker 的 `signatureVerification=not_performed`，不能将这些字段写成独立密码学验签结论。
[R1][R2][R3]

## 3 已通过的软件和目标运行证据

通信批次与全量目标结果来自 5010522 或各自独立包；置顶、窗口启动与校准回放另列。下列结果未合并成 5010523 全量复跑结论。

| 专题 | 结果 | 已验证范围 |
| --- | --- | --- |
| 宿主统一回归批次 | 21 步全部 PASS，退出码均为 0 | 通信、波特率、Back、窗口策略、启动、语言、导出、虚拟手柄和 CMake 平台配置；含 configure/build 步骤 |
| 统一通信 | 321 PASS = 309 功能 + 12 生命周期，0 失败/跳过/取消/todo | 10 个组件；生产 Worker/配置/QML/ETS，真实 loopback 或受控 Kit/provider |
| 通信 SDK 检查 | 3 组，0 错误、15 警告 | 声明兼容性检查；警告保留，不能写成零警告构建 |
| Bootloader 协议宿主夹具 | 8 PASS = 6 功能 + 2 生命周期 | 真实提取的组帧、写检查、响应解析、CRC；60ms 下游完成延迟模型、短写、尾帧、0xf980 拒绝偏移 |
| NMEA 独立 Harmony 目标 | 11 PASS = 9 功能 + 2 生命周期，0 失败/跳过 | 真实 Qt NMEA parser、生产适配器及提取的 LinkManager/PositionManager 方法；配置/应用服务为夹具 |
| UDP/TCP 独立 Harmony 目标 | 18 PASS = 16 功能 + 2 生命周期，0 失败/跳过 | 真实 Harmony Qt socket loopback、排空竞态、TemporaryError、后续收发与错误保留 |
| RTK / Bootloader 完整目标类 | 30 PASS = 28 功能 + 2 生命周期，0 失败/跳过，runner exit=0 | API24 / Qt6.12；真实 C++ transport/Bootloader/worker，设备 provider 受控 |
| RTSP / FFmpeg 5010522 复验 | 12 PASS = 10 功能 + 2 生命周期，0 失败/跳过；CTest 1/1 PASS | 5010522 测试包；含 UDP-only/TCP-only RTSP，独立启动复验耗时 17.56s |
| 全 49 类原始证据审计 | 48 类完整结果：346 PASS = 250 功能 + 96 生命周期，6 SKIP、0 FAIL | 含视频复验；FTP 仅观察到 6 功能 + init PASS，缺尾部结果，不并入完整计数 |
| 置顶 / 窗口启动宿主专项 | 置顶 8 PASS = 6 功能 + 2 生命周期；启动 12 场景 PASS | Qt6.12 offscreen，真实生产 QML/设置路径；宿主结果与 Pad 窗口层级实测分别记录 |
| 传感器校准回放宿主专项 | 40 PASS = 38 功能 + 2 生命周期，0 失败/跳过 | 完整生产 controller/StatusTextHandler、真实 MAVLink CRC/分块解析；Vehicle 等外围服务为壳 |
| NAPI / 系统授权专题 | 既有真实 Harmony 16 PASS = 10 功能 + 6 生命周期 | core/grant/deny 独立 helper；对应输入哈希未变，结论按原 helper 来源范围延用 |

统一通信已经包含默认与 Harmony-defined 宿主网络变体，不能再把两变体结果加一次；宿主协议夹具、各目标专题和旧证据也不累加成一个“全量功能通过数”。
RTSP 源文件 SHA256 为 `fae2fdf80576730372263dccfd9a3c01dc6772c42326a31d2c29866d5a70ae57`。视频首次启动检查失败与本次实际 12 次调用通过均保留，启动复验不抹除首次失败。
NAPI 专题实际执行 native provider、Qt JS 网关、ArkTS 和系统授权 API；无线字节流使用受控 ETS 工厂，仿真器生产 Bluetooth Core 能力的不支持分支也被保留。
[R4][R5][R6][R7][R8][R9][R10]

### 3.1 固件与 UDP 的当前状态

Harmony 二进制 PROG_MULTI 使用一个完整帧提交：命令、长度、payload、EOC。64B payload 形成 67B write；官方 64B payload 限制、ACK、错误偏移、CRC 与非 Harmony 原路径保持。
真实 Harmony 目标用例确认 60ms 完成延迟下的全字节/CRC，以及 GET_DEVICE 后掉线时不发送 ERASE/PROGRAM/BOOT、不报告成功。
用户在 5010522 上确认同板刷写成功；该物理使用证据及软件目标结果按来源版本保留，5010523 沿用整帧源码，未增加物理刷写复验声明。

UDP 的暂时空读仅在 Harmony 路径按 TemporaryError 处理，并实际复位 Qt 读通知；负返回退出当前 drain，零长度包被消费，真实网络错误继续上报。
Windows 原读取路径保留。Harmony 目标 16 项回环验证已覆盖实际 POSIX 错误映射与后续通信恢复，未将无数据计为收到有效载荷。

## 4 完整应用、用户实测与性能

| 项目 | 当前状态 | 证据归属 |
| --- | --- | --- |
| 5010523 Release 与置顶 | 安装、启动、OFF/ON、冷启动保存 true、自由窗口重叠 pin、返回沉浸全屏通过 | 当前签名 HAP、bundle、原始 windowlist 与截图；最后设置为 ON |
| Pad 物理固件刷写 | 用户确认 5010522 成功；成功截图/完整日志缺失 | Bootloader 协议 5、板 ID 50 单板；不是 5010523 新刷写验收 |
| 完整 UI 七项流程 | 既有候选记录可供参考；未作为 5010523 新批全量通过 | MockLink 保存/恢复、参数、任务、语言、日志等按原报告范围 |
| 系统 Back / 窗口往返 | 已有专项证据；来源候选独立保留 | 不并入当前新包完整 UI 计数 |
| 全 49 类 | 首次 47 PASS / 1 上游 SKIP / 1 视频启动 FAIL；视频独立复验 1/1 PASS | FTP 缺最后两个功能、cleanup、Totals 和 Finished；完整原生结果为 48 类 |
| 20 轮压力 | 仅 2 轮完整，每轮 10 PASS；第三轮断连，未完成 20 轮 | 同期 Windows 虚存事件 2004 与 HDC crash 有记录，不能据此判产品压力通过 |
| 加速度计校准 | 第一轮第三个 back 面已 51%；第二轮 34% 后飞控等待 motion/rest，后取消/失败；UI 可取消 | 原 tlog 60,751 帧 / 1,613 heartbeat / 195 statustext；88 条文本回放未复现软件停住，实物姿态复现未闭环 |
| Pad 息屏 | 最终官方 suspend 命令 exit=0，返回 SuspendDevice is called；随后停止设备探测 | 既有记录有后续 SHELL 唤醒，原因未定位；持续息屏未验证 |

用户原话为“成功了，但是有点慢，同线同连接 Windows 快很多”。吞吐差异是当前真实限制；同镜像、同板、同连接的总耗时尚未统一量测。
两段应用日志短采样的 observed payload 为 2.58 / 2.02 kB/s，帧间隔中位数 25 / 32ms，API 提交调用中位数 10 / 14ms。它们不代表全镜像吞吐或 USB 总线计时；遗漏的日志会影响统计。
Qt/JS/NAPI、UsbService IPC、调度及板端响应各自占比尚未分解，不能把单 API 耗时直接写成唯一瓶颈或保证优化幅度。
性能评估继续保留官方 64B 块长，分别观察提交、完成、ACK 与擦除/程序/校验阶段；不以增大协议不允许的 payload 代替测量。

原扩展坞 NO_DEVICE 的拓扑重建触发来源仍未知；原直连 PROTO_INVALID 的具体拒绝分支、41ms 观测与 50ms 期限之间的因果也未锁定。
用户提供的同连接 Windows 成功对照保留，不据此归咎线缆或扩展坞损坏。当前物理成功支持该板功能可用，不外推长期稳定性或全部外设矩阵。
[R11][R12][R16][R17][R18][R19]

## 5 官方一致性、翻译与保留边界

官方业务与协议为评估基准；Harmony 平台绑定、受控错误修正和以下功能范围存在明确差异，不能宣称与 Windows 所有能力等价。
5010523 最终差异审计已冻结，109 项哈希全部匹配；6 项历史 P1 为 resolvedSource，7 项 P2 保留。软件修复状态与物理型号验收分别记录，窗口专项与旧测试包证据不能互相替代。[R13][R17]

| P2 范围 | 需要保留的边界 |
| --- | --- |
| USB 设备与参数 | 已知 CDC / CH34x / CP210x / FTDI 范围；无硬件流控和 1.5 stop bits；serialNumber 为空；部分 Qt 标准速率未在 SDK SerialManager 枚举声明，UI 速率不构成芯片物理支持证明 |
| USB 队列 | C++ 待发队列及阻塞适配接收队列没有总容量上限；单块 4KiB/8KiB 限制不等于总队列容量上限 |
| 手柄 | GamePad 的 6 轴/15 键范围；不能覆盖任意桌面 raw HID/HOTAS；仿真器型号存在 Unsupported 分支 |
| 视频 | FFmpeg 软件解码及编译时 codec/协议集合；无 TLS/硬件 decoder，不等于 GStreamer 插件全集 |
| 蓝牙授权上下文 | 默认 QAbility 路径有已验收范围；多 Ability 前台切换的授权上下文仍有边界 |
| Back / 窗口 | 主页面 Back 消费与 Qt QPA 定制需要按实际输入法、菜单和窗口状态继续界定运行范围 |
| 固件性能 | 用户确认同板刷写成功，但同线同连接明显慢于 Windows；短采样不代表全程吞吐，IPC/调度/板端贡献仍待量测 |

修改的 11 个 TS 共 2161 个 translation 节点变化：2160 条精确复用官方固定提交中的已有译文，按 context/source/comment/numerus 与完整 translation 节点核对；没有新增自译填补官方空白。
唯一例外是 `Enable,Disable` 的既有中文枚举 `启用/禁用` 改为 `启用,禁用`，属于分隔符修复。译文来源包括其他官方版本，不能声明 catalogs 与原始 v5.0.8 字节相同。[R14]

测试注册没有移除原有有效类。上游未实现边界仍显式保留：两类 FactSystem 各两个 QML 槽、Corridor camera trigger、PX4 log parser；本轮最终跳过数按完整 XML 判定。
MissionControllerManagerTest、TerrainTileTest 仅生命周期，缓存下载槽为构造 smoke；MissionManager 的有效 slot 与内部场景应区分。类数与 PASS 调用数均不能换算为全功能完成率。

## 6 尚需闭环的发布记录

1. 补齐 FTPManagerTest 的末两项、cleanup 与 Totals/Finished 证据；现有汇总为 evidence_incomplete，保留首次视频启动失败和独立复验，不外推全 49 类全部完成。
2. 20 轮压力保持未完成结论；两轮完整输出和第三轮断连、宿主虚存/HDC 故障证据保留。
3. 对当前应用通用 UI 流程保留未复验项；置顶/自由窗口/全屏专项单独通过，息屏未保持及加速度计未完成面继续独立记录。
4. 用户成功陈述和既有 67B 采样已保留；成功截图/全程日志缺失。记录完整吞吐对照仍属未闭环项，性能优化结果独立判定。
5. 保留已冻结的 5010523 源码审计与 Release 关联，旧测试包只按来源/源码范围沿用；未实现原生 USB 提速，getFD/libusb 路径仅属设计可能性，硬件矩阵独立维护。

这些未闭环项限制当前完整发布声明；已通过的软件、目标专题与用户单板成功继续作为其明确范围内的有效证据。

## 7 证据索引

| 引用 | 当前证据 |
| --- | --- |
| R1 | [5010523 签名 Release 产物](E:/tmp/QGC/tmp/verification/upload-release-5010523-signed-artifact.json) |
| R2 | [5010523 未签名 Release 产物](E:/tmp/QGC/tmp/verification/upload-release-5010523-unsigned-artifact.json) |
| R3 | [Pad 安装](E:/tmp/QGC/tmp/verification/upload-pad-release-5010523-install.log)、[已安装 debug=false](E:/tmp/QGC/tmp/verification/upload-pad-release-5010523-installed-bundle.json:75)、[启动](E:/tmp/QGC/tmp/verification/upload-pad-release-5010523-start.log) |
| R4 | [21 步宿主回归](E:/tmp/QGC/tmp/verification/upload-host-20261015-084439-146.json) |
| R5 | [统一通信 321 调用](E:/tmp/QGC/tmp/verification/harmony-communications/run-20261015-084439-891/report.json)、[Bootloader 宿主协议 8 PASS](E:/tmp/QGC/tmp/build-bootloader-protocol-test/result.xml) |
| R6 | [NMEA Harmony 目标 11 PASS](E:/tmp/QGC/tmp/verification/nmea-serial-source-ohos/run-20261015-084709-365/report.json) |
| R7 | [网络 Harmony 目标 18 PASS](E:/tmp/QGC/tmp/verification/network-links-ohos/run-20261015-085115-653/report.json) |
| R8 | [RTK/固件 Harmony 目标 30 PASS](E:/tmp/QGC/tmp/verification/upload-firmware-final-native-acceptance.json)、[当前测试包](E:/tmp/QGC/tmp/verification/upload-full-tests-5010522-artifact.json) |
| R9 | [当前视频复验 XML](E:/tmp/QGC/tmp/verification/upload-final-video-startup-recheck.xml)、[native 日志](E:/tmp/QGC/tmp/verification/upload-final-video-startup-recheck-native.log)；12 PASS，CTest 1/1 PASS |
| R10 | [NAPI/授权 16 PASS 来源](E:/tmp/QGC/tmp/verification/harmony-runtime-acceptance.json)、[输入哈希链](E:/tmp/QGC/tmp/verification/harmony-candidate-runtime-source-chain.json) |
| R11 | [既有完整 UI 来源](E:/tmp/QGC/tmp/verification/harmony-application/run-20261015-041359-251/report.json)；不计为当前包新批完整 UI |
| R12 | [USB/固件故障与用户对照](E:/tmp/QGC/tmp/verification/diagnosis/diagnosis_20261015_usb_firmware.md)、[当前 Pad 实验日志](E:/tmp/QGC/tmp/verification/upload-pad-release-5010522-experiment-app-latest.log) |
| R13 | [生产差异审计](E:/tmp/QGC/tmp/verification/upload-official-production-audit.md)、[哈希与产物关联](E:/tmp/QGC/tmp/verification/upload-official-production-audit.json)；5010523 / schema 5 / 109 项哈希匹配 |
| R14 | [官方翻译与测试真实性审计](E:/tmp/QGC/tmp/verification/upload-official-translations-tests-audit.md)、[逐项翻译来源](E:/tmp/QGC/tmp/verification/upload-official-translations-tests-audit.json) |
| R15 | [全 49 类验收审计](E:/tmp/QGC/tmp/verification/upload-final-full-49-acceptance.json)、[首次 XML](E:/tmp/QGC/tmp/verification/upload-final-full-49-first.xml)、[首次 native 日志](E:/tmp/QGC/tmp/verification/upload-final-full-49-first-native.log)；FTP 证据不完整，视频复验见 R9 |
| R16 | [固件短采样计时](E:/tmp/QGC/tmp/verification/upload-firmware-throughput-samples.json)、[压力日志](E:/tmp/QGC/tmp/verification/upload-final-vehicle-link-stress20.log)、[虚存/HDC 故障](E:/tmp/QGC/tmp/verification/upload-host-memory-hdc-faults.json)；压力仅两轮完整 |
| R17 | [523 Pad 置顶/窗口验收](E:/tmp/QGC/tmp/verification/pad-window-topmost-acceptance.json)、[冻结/宿主回归](E:/tmp/QGC/tmp/verification/window-topmost-freeze.json)；含原始 windowlist 与视觉开关反转；官方依据：[setWindowTopmost](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/arkts-apis-window-window#setwindowtopmost14)、[自由窗口概述](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/freeform-window-overview) |
| R18 | [校准验收边界](E:/tmp/QGC/tmp/verification/accel-calibration-acceptance.json)、[回放 40 PASS](E:/tmp/QGC/tmp/build-sensor-calibration-test/result.json)、[88 条校准文本](E:/tmp/QGC/test/SensorCalibration/accel-replay.json)；实体姿态问题未闭环 |
| R19 | [最终 suspend 记录](E:/tmp/QGC/tmp/verification/pad-final-suspend.json)；命令退出 0、停止后续探测，持续息屏未验证 |
