# TimeTransle 开发阶段与验收计划

## 1. 文档目的

本文定义工程目录、构建矩阵、依赖原则、阶段顺序和量化验收条件。它面向开始编写代码的新对话，不能替代产品边界、架构或前端规范。

相关文档：

- [`../PROJECT_PLAN.md`](../PROJECT_PLAN.md)
- [`requirements-and-boundaries.md`](requirements-and-boundaries.md)
- [`architecture-and-protocol.md`](architecture-and-protocol.md)
- [`frontend-design.md`](frontend-design.md)

## 2. 当前起点

截至 2026-07-27，阶段 0–10 和阶段 11 的主体实现已完成；项目已有核心时钟、双架构 Hook/Injector、IPC、Controller、参考图表盘、普通程序最小硬门禁、只读预检、UAC 重启和本地诊断：

- 已创建双架构 CMake preset、基础 `common`/`protocol` target 和无第三方依赖的 C++ smoke test。
- 已创建 IPC、错误码、WebView Schema 和测试向量契约。
- 已锁定原生与前端直接依赖并记录来源、版本和许可证。
- 已按用户确认的“桌面旋钮”方向完成默认约 `360×360` 的 React/TypeScript/Vite 功能 UI；原生窗口限制为 `320–440` 的近方形尺寸、移除最大化入口并记忆位置与尺寸。
- SVG 旋钮、全程统一 `45°`/八度的对数映射、Pointer Capture、拖动中实时吸附、键盘与精确输入、desired/applied 状态机、严格 Bridge 解析、可访问语义与 reduced-motion 已实现。
- UI 已实际通过 ESLint、106 个 Vitest 契约/Schema/状态机/映射/交互测试和 Vite 生产构建。
- 已安装 Visual Studio Build Tools 2022、MSVC v143 x86/x64 与 Windows 11 SDK `10.0.26100.0`；CMake `4.4.0` 已完成 x64/x86 Debug/Release 构建与完整回归。
- 已实现独立 `VirtualClock` 静态库、可注入 fake real-QPC source、连续重锚、独立暂停、统一时间换算、DWORD 回绕、溢出保护和原子 payload seqlock。
- x64/x86 Debug 均通过 `virtual_clock.unit`：覆盖 `0.05 / 0.5 / 1 / 2 / 8`、全部倍率两两切换、暂停/恢复/RESET、非法输入、宽位换算、32 位回绕和 32-reader 快照一致性；并发目标在两个架构各连续运行 20 次无失败。
- TestApp32/64、四个时间 API Hook、IPC V1、断连本地复位、安全筛查和 Injector32/64 已实现并通过双架构测试。
- x64 Win32 Controller、WebView2 STA 宿主、严格受信 origin/文档、Web 消息 schema/range/state 校验、ProcessCatalog、状态快照、目标退出监视和 Controller→Injector→Hook 受控 TestApp 编排均已实现。
- Debug/Release 均通过 x64 16 项 CTest、x86 14 项 CTest，以及 x64 Controller 控制 x86 TestApp 和普通命名夹具的跨架构 E2E；其中包含现实 5 秒 `2×`、现实 10 秒 `0.5×` 和两个架构各 100 次会话生命周期循环。
- `$review-animations`、`$improve-animations`、reduced-motion QA、静态 MSVC 运行库便携包、精确许可证归档、逐文件/ZIP 哈希和签名状态记录已完成。
- SafetyPolicy 已移除 TestApp 名称、模块风险、AppContainer 与缓解策略预拒绝；`AntiCheatFixture.dll` 和缓解策略夹具已证明这些信号不影响预检。
- 前端已使用 SVG 复刻参考图的双外环、内环、四向连接桥与独立双针，并接入只读预检、选择后自动连接、UAC 状态和全局快捷键。
- 高完整性 UAC 成功路径已使用 `ElevatedProgramFixture32/64` 中的 x64 夹具人工通过：普通 Controller 返回 `INTEGRITY_MISMATCH`，用户触发标准 UAC 重启后，新实例重新枚举、重新预检并完成标准加载与 Hook 初始化。本机 `ConsentPromptBehaviorAdmin=0`，取消路径需在会显示 UAC 同意提示的独立环境复验。
- Authenticode、UAC 取消路径、外部兼容性矩阵和独立干净 Win10/11 VM 验证后移到阶段 12。

环境检查：

| 项目 | 状态 |
| --- | --- |
| Node.js | 已发现，`v24.14.0` |
| WebView2 Runtime | 已发现，`150.0.4078.83` |
| CMake | 项目本地 `4.4.0`，脚本可自动发现 |
| Ninja | 项目本地 `1.13.0`，脚本可自动发现 |
| Visual Studio Build Tools | `17.14.37`，安装完成且无需重启 |
| MSVC `cl` | `19.44.35228`，x64/x86 均已实际编译 |
| MSBuild | `17.14.51` |
| Windows SDK | `10.0.26100.0` |
| `vswhere.exe` | 标准位置已安装 |

`scripts/bootstrap.ps1` 当前返回 `Ready: True`。阶段 7 的设计决策已经完成。

## 3. 目标目录结构

以下为计划结构，后续应按阶段创建，不要求第一步一次性填满所有文件：

```text
TimeTransle/
├─ .agents/skills/
├─ .github/workflows/
│  └─ windows.yml
├─ CMakeLists.txt
├─ CMakePresets.json
├─ README.md
├─ LICENSE
├─ NOTICE-THIRD-PARTY.md
├─ PROJECT_PLAN.md
│
├─ cmake/
│  ├─ CompilerWarnings.cmake
│  ├─ Dependencies.cmake
│  ├─ BuildInfo.cmake
│  └─ Packaging.cmake
│
├─ contracts/
│  ├─ ipc-v1.md
│  ├─ webview-v1.schema.json
│  ├─ error-codes.md
│  └─ test-vectors/
│
├─ docs/
│  ├─ requirements-and-boundaries.md
│  ├─ architecture-and-protocol.md
│  ├─ frontend-design.md
│  ├─ development-plan.md
│  └─ adr/
│     ├─ 0001-supported-targets.md
│     ├─ 0002-cross-arch-injectors.md
│     ├─ 0003-disconnect-reset.md
│     ├─ 0004-dll-lifecycle.md
│     ├─ 0005-virtual-clock-sync.md
│     └─ 0006-webview2-distribution.md
│
├─ src/
│  ├─ common/
│  │  ├─ include/timetransle/common/
│  │  └─ src/
│  ├─ protocol/
│  │  ├─ include/timetransle/protocol/
│  │  └─ src/
│  ├─ virtual_clock/
│  │  ├─ include/timetransle/clock/
│  │  └─ src/
│  ├─ controller/
│  │  ├─ app/
│  │  ├─ process_catalog/
│  │  ├─ safety_policy/
│  │  ├─ injector_broker/
│  │  ├─ target_session/
│  │  ├─ command_coordinator/
│  │  ├─ pipe/
│  │  ├─ webview/
│  │  ├─ config/
│  │  └─ diagnostics/
│  ├─ injector/
│  │  ├─ main.cpp
│  │  ├─ target_identity.cpp
│  │  ├─ remote_loader.cpp
│  │  └─ result_channel.cpp
│  ├─ hook/
│  │  ├─ dll_main.cpp
│  │  ├─ bootstrap.cpp
│  │  ├─ hook_manager.cpp
│  │  ├─ time_api_hooks.cpp
│  │  ├─ pipe_client.cpp
│  │  └─ lifecycle.cpp
│  └─ test_app/
│     ├─ app.cpp
│     ├─ timer_panel.cpp
│     ├─ moving_object.cpp
│     └─ real_time_reference.cpp
│
├─ ui/
│  ├─ package.json
│  ├─ package-lock.json
│  ├─ vite.config.ts
│  ├─ tsconfig.json
│  └─ src/
│     ├─ app/
│     ├─ bridge/
│     ├─ state/
│     ├─ components/
│     │  ├─ ClockDial/
│     │  ├─ ProcessPicker/
│     │  ├─ SpeedInput/
│     │  └─ StatusBar/
│     ├─ motion/
│     ├─ styles/
│     └─ test/
│
├─ tests/
│  ├─ unit/
│  │  ├─ virtual_clock/
│  │  ├─ protocol/
│  │  ├─ safety_policy/
│  │  └─ command_coordinator/
│  ├─ integration/
│  │  ├─ hook_in_process/
│  │  ├─ named_pipe/
│  │  └─ injection/
│  ├─ e2e/
│  ├─ fixtures/
│  └─ ui/
│
├─ scripts/
│  ├─ bootstrap.ps1
│  ├─ build-all.ps1
│  ├─ test-all.ps1
│  ├─ package.ps1
│  └─ verify-licenses.ps1
│
├─ packaging/
├─ third_party/licenses/
└─ out/
```

## 4. 构建矩阵

一个普通 CMake configure tree 不同时混合 Win32 和 x64。使用独立 preset 和 `binaryDir`。

### 4.1 x64 构建

```text
TimeTransleController.exe
TimeTransleInjector64.exe
TimeTransleHook64.dll
TimeTransleTestApp64.exe
x64 单元/集成测试
```

### 4.2 x86 构建

```text
TimeTransleInjector32.exe
TimeTransleHook32.dll
TimeTransleTestApp32.exe
x86 单元/集成测试
```

Hook、Injector 和 TestApp 的 x86/x64 目标共用源文件，禁止复制两套逐渐分叉的源码。

### 4.3 UI 构建

- UI 在 `ui/` 中独立执行 `npm ci`、测试和 Vite build。
- 不要让每次 C++ 增量编译都重新运行 npm。
- 打包阶段把固定版本的 UI build 产物复制到 `resources/ui/`。

## 5. 编译和依赖原则

- C++20。
- 自有 MSVC 代码启用 `/W4 /WX /permissive-`。
- 第三方代码不继承 `/WX`。
- TypeScript 开启 `strict`。
- 提交精确 lockfile。
- 所有生产依赖固定版本；构建脚本禁止使用 `latest`。
- MinHook 固定已审查 tag，保留 BSD-2-Clause 声明。
- WebView2 SDK 固定 release 版本；Runtime 使用 Evergreen 检测和官方安装入口。
- 前端预期依赖：
  - `react`
  - `react-dom`
  - `@base-ui/react`
  - `motion`
  - `@number-flow/react`
  - `sonner`
- 不默认加入 Electron、Tailwind、Redux、Zustand、拖放库或大型组件套件。
- 若为 Win32/COM RAII 使用 WIL，应固定版本并记录许可证。
- C++ JSON 解析器与测试框架在工程骨架阶段通过 ADR 选定；不得手写脆弱的 JSON split/parser。

## 6. 开发阶段

### 阶段 0：决策与规范冻结

本轮 Markdown 完成后视为完成。输出包括：

- 支持矩阵。
- 安全边界。
- 断连立即恢复 1.0×。
- 跨位数 Injector。
- V1 逻辑停用 DLL 生命周期。
- VirtualClock、IPC、前端和测试方案。

### 阶段 1：工具链与工程骨架

当前状态：已完成。x86/x64 configure、Debug build 与 CTest、UI lint/test/build、依赖许可证检查均已通过。

输出：

- 双架构 CMake preset。
- 基础目录和 target。
- 依赖锁定。
- 协议契约与错误码。
- 最小 C++/TS smoke test。
- CI 初始矩阵。

本阶段不实现远程注入。

### 阶段 2：纯 VirtualClock

状态：已完成（2026-07-26）。

输出：

- 可注入 fake real-QPC source 的时钟库。
- 连续重锚。
- 独立暂停状态。
- QPC 到 Tick64/Tick32/timeGetTime 的统一 elapsed 换算。
- 原子 payload seqlock。
- 确定性和并发单元测试。

### 阶段 3：TestApp 与 Hook 核心

状态：已完成（2026-07-26）。

输出：

- TestApp32/64。
- 四个时间 API detour。
- 原始 trampoline。
- MinHook 安装、一次性启用和完整回滚。
- 不 Hook QueryPerformanceFrequency。
- 逻辑停用与在途 Hook rundown。

优先先在 TestApp 自身通过普通 `LoadLibrary` 验证，不立即引入远程加载问题。

### 阶段 4：IPC 与生命周期

状态：已完成（2026-07-26）。

输出：

- 明确长度的命名管道帧。
- HELLO、STATUS、SET_SPEED、PAUSE、RESUME、RESET、SHUTDOWN。
- request/session/sequence/revision。
- peer PID 与 DACL。
- 一条 in-flight + 一个最新 pending 槽。
- 管道断连在 DLL 本地立即重锚到 1.0×并解除暂停。

### 阶段 5：安全筛查与 Injector

状态：已完成（2026-07-26）。

输出：

- ProcessCatalog。
- PID + 创建时间身份。
- 位数、完整性、PPL、关键进程、缓解策略和风险模块检查。
- Injector32/64。
- 已注入检测和幂等错误。
- 目标退出监视。

本阶段的远程加载测试只针对 TestApp，用于隔离 Injector 和安全门禁实现风险；这是一项历史记录。阶段 11 已移除 TestApp 名称门禁、缓解策略预拒绝和风险模块扫描，以最小硬门禁取代。

### 阶段 6：Controller 与 WebView2

状态：已完成（2026-07-26）。

输出：

- x64 Win32 Controller。
- WebView2 STA 宿主。
- 本地受信 origin。
- JSON schema 校验。
- StateCoordinator。
- ProcessPicker、状态快照和错误展示的原生接口。

说明：阶段 6 先冻结原生安全边界；完整 attach/控制编排已在阶段 8 接通并通过双架构 E2E。

### 阶段 7：功能 UI 与 SVG 旋钮

状态：已完成。

输出：

- React reducer。
- desired/applied 区分。
- 发光 SVG 时钟。
- 分段对数映射（`0.1.2` 起已被全程统一 `45°`/八度的单斜率映射取代，见 `docs/frontend-design.md` §6）。
- Pointer Events、Pointer Capture、死区与吸附。
- 键盘、精确输入和 reduced-motion。

验收结果：

- 默认约 `360×360`，`320–440` 近方形缩放，主界面无纵向滚动。
- 旋钮在 `360×360` 中占主导视觉面积；目标/连接状态为一行，底部仅保留三项核心操作。
- 详细状态、精确输入、错误与进程选择进入弹层；关闭弹层后恢复紧凑主界面。
- 本阶段验收时为 57 项 Vitest（`0.1.2` 起为 106 项）、ESLint、TypeScript 和 Vite 生产构建通过。
- Chromium 在最小、默认和最大设计尺寸完成视觉与核心交互检查。

### 阶段 8：端到端与韧性

状态：已完成（2026-07-27）。

输出：

- 比例、连续性和跨 API 累计误差测试。
- 高频拖动和乱序 ACK 测试。
- Controller 崩溃/断连复位。
- 目标退出。
- 重复注入。
- Hook 安装失败、部分失败和关闭时并发。

验收结果：

- 现实 `5s` 在 `2×` 下的虚拟时间为 `10.0±0.1s`；现实 `10s` 在 `0.5×` 下的虚拟时间为 `5.0±0.1s`。
- 四个 API 的长时累计差不超过 `20ms`。
- x86/x64 各完成 100 次 Controller→Injector→Hook→安全关闭生命周期循环。
- 正常关闭、Controller 强制断连、目标退出、旧 session、重复注入和失败回滚均通过自动化回归。
- 真实 360×360 WebView2 窗口完成选择 TestApp、同步、`2×`、暂停/继续、`1×` 与关闭恢复烟雾验证。

### 阶段 9：动画审查与分发

状态：本地门禁已完成；正式签名与独立干净 VM 验证待发布环境。

输出：

- 完整读取并执行 `$review-animations`。
- 完整读取并执行 `$improve-animations`。
- 按 `$emil-design-eng` 要求形成 Before/After/Why 审查表。
- 性能、DPI、可访问性和 reduced-motion QA。
- 便携包、许可证、哈希、签名策略和干净 VM 报告。

本地验收结果：

- 动效审查无高优先级问题；弹层保持 300ms 内、GPU 属性和正确 origin，hover 有精细指针门禁。
- reduced-motion 保留 80–120ms 颜色/透明度反馈并移除位移/缩放反馈。
- Release 使用静态 MSVC 运行库，`dumpbin /DEPENDENTS` 未发现 VC Runtime DLL 依赖。
- `scripts/package.ps1` 生成仅含用户运行时产物的便携 ZIP、精确许可证文本、`SHA256SUMS.txt`、ZIP `.sha256` 与 `SIGNATURE-STATUS.txt`。
- 包内 Controller `--self-test` 返回 `0`；当前 ZIP 哈希已复验匹配。
- 当前机器没有 Windows Sandbox/独立干净 VM，也没有发布证书；详情见 [`release-and-validation.md`](release-and-validation.md)。

### 阶段 10：前端表盘修订

状态：当前主任务。

目标：

- 保持默认约 `360×360`、`320–440` 近方形缩放、无最大化布局和单屏无滚动的桌面旋钮形态。
- 移除 `vector-effect: non-scaling-stroke` 造成的固定粗描边，让表盘线宽随窗口等比缩放。
- 校准完整双外环、四向连接桥、完整内环、中心轴和暖金色光晕；保留环间清晰的黑色间隔，移除仪表盘式可见刻度。
- 删除倍率读数的负间距布局，使用独立表盘视觉层与正常文档流，保证表盘和倍率在 `320×320`、`360×360`、`440×440` 下不重叠。
- 把共同旋转的刚性 L 形双臂拆成两根指针：倍率分针继续使用现有角度/倍率映射，时针以分针可见角度增量的 `1/12` 机械联动。
- 保留 Pointer Capture、跟手拖动、死区、吸附、键盘、精确输入、desired/applied 分离、透明宽命中区和 reduced-motion。
- 让静态预览与交互组件共用同一套表盘几何，删除要求“刚性 L 形共同旋转”的旧测试。

验收：

- 参考图对照下，核心线条不再形成实心白色光带；双外环之间的暗缝与四处连接桥清楚可辨。
- `1.0×` 时倍率分针位于 12 点，时针初始位于 3 点；调整、点击、键盘输入和吸附只直接控制倍率分针，时针按 `1/12` 比例同帧跟随。
- 100%、150%、200% Windows 缩放及 `320/360/440` 三档窗口均无重叠、裁切、纵向滚动或不可点击核心操作。
- ESLint、TypeScript、Vitest、Vite 构建和 WebView2 窗口视觉回归全部通过。

### 阶段 11：最小安全门禁的普通程序支持

状态：主体实现与成功提升路径已完成；取消路径因本机不显示 UAC 同意提示，保留到阶段 12 的独立环境验收。

目标：

- 只保留 PID/创建时间、x86/x64 架构、完整性、PPL/受保护状态、关键进程和产品 Hook DLL 身份硬门禁。
- 不建立游戏、反作弊、保护软件、联网程序、发行商、商店、路径、窗口标题或模块名名单。
- 不预先拒绝 AppContainer、Dynamic Code、签名策略、CFG 等缓解策略；兼容性由真实加载和 Hook 结果决定。
- 普通权限目标由默认非提升 Controller 直接连接。目标完整性高于 Controller 时停止当前流程并返回可恢复的 `INTEGRITY_MISMATCH`；允许用户明确确认 Windows 标准 UAC 重启，提升后重新核验目标并执行全部门禁。
- 匹配架构 Injector 先运行只读 `--inspect-only`；检查通过并显示名称、PID、架构和结论后，Controller 自动正式连接，不需要在详情菜单二次确认。
- 正式连接重新检查硬门禁，并仅使用 `LoadLibraryW + CreateRemoteThread`；重复连接由 Hook 的 `ALREADY_ATTACHED` 返回处理。
- 不静默提升，不启用 `SeDebugPrivilege`，不使用隐藏加载、manual map、驱动或保护绕过。
- JSONL 日志只记录时间、组件、阶段、真实结果和稳定错误码，位于 `%LOCALAPPDATA%\TimeTransle\logs`，单文件 `2 MiB`、保留 5 份。

兼容性测试范围：

- TestApp32/64 继续执行全倍率、暂停、API 一致性、断连复位和 100 次生命周期回归。
- 使用 `OrdinaryProgramFixture32/64` 验证非 TestApp 名称不影响完整控制链路。
- 使用带 `requireAdministrator` manifest 的 `ElevatedProgramFixture32/64` 验证真实高完整性门禁和标准 UAC 重启；夹具只属于测试产物，不进入用户包。
- 使用加载 `AntiCheatFixture.dll` 且启用 Dynamic Code、签名和 CFG 策略的夹具证明预检不扫描模块名或预拒绝缓解策略。

验收：

- 至少一个非 TestApp x86 目标和一个非 TestApp x64 目标通过完整门禁、连接、`0.5×`、`2×`、暂停、恢复 `1×`、正常关闭和 Controller 异常断连复位。
- 高完整性夹具验证普通 Controller 不写入并提示权限匹配；取消 UAC 保持未连接；新实例重新枚举、重新预检，并仅在新预检通过后自动连接。
- 所有允许路径在首次写入前完成身份、架构、完整性、保护、关键进程和本地 DLL 身份检查。
- 所有拒绝路径返回与真实原因一致的稳定错误和本地诊断记录；UI 不再把非权限策略拒绝显示为“权限不足”。
- 普通目标支持不得破坏 TestApp 的 x86/x64 确定性回归。

### 阶段 12：产品兼容性、发布与干净环境

状态：进行中；阶段 10–11 主体、UAC 成功路径、实体全局快捷键、本地 Release 回归和未签名候选包已完成（当前候选 `0.1.3`），UAC 取消路径并入独立环境验收。

目标：

- 扩充普通离线测试程序兼容性矩阵，记录“支持、部分生效、不受支持、被安全门禁拒绝”及原因。
- 完成前端定稿、普通目标支持、日志、错误分类、双架构回归和便携包自检。
- 在独立干净 Win10/11 VM 上同时验证 TestApp 与普通离线测试程序。
- 通过 Authenticode 签署 Controller、Injector 和 Hook DLL，签名后重新生成文件与 ZIP 哈希。
- 建立可追踪的 Git 发布基线和远端 CI 结果后，才允许声明 V1 产品完成。

## 7. 阶段验收门槛

| 阶段 | 必须通过的验收 |
| --- | --- |
| 0 | 所有规范文件内部一致；断连策略明确为立即 1.0×、解除暂停、无 keep-last |
| 1 | 干净目录可分别 configure/build x86 与 x64；UI 可 lint/test/build；依赖均有版本、来源、许可证 |
| 2 | Fake QPC 下 0.5×/2× 比例误差不超过 1 QPC tick；切速和暂停边界不超过 1 tick；NaN/Inf/越界拒绝；DWORD 环绕正确；32 个 reader 压力下逐线程不倒退 |
| 3 | TestApp32/64 四个 API 均受控；QPF 不变；不同 API elapsed 换算误差不超过 2ms；任一 Hook 创建/启用失败可完整回滚；热路径无分配、文件 I/O、IPC 和普通 mutex |
| 4 | 截断、超长、未知版本、乱序、旧 session 和非法倍率全部拒绝；队列始终有界；杀死 Controller 后 Hook DLL 不等待额外 grace，立即连续恢复 1.0×、paused=false |
| 5 | 受控阶段只对 TestApp32/64 成功；高完整性目标在完成权限匹配前不写入，PPL、关键进程、已知风险模块、检查不完整和不兼容缓解策略均在写入目标前拒绝；重复请求返回 `AlreadyAttached` |
| 6 | 非受信 origin 无法调用原生命令；所有 WebView 消息经过 schema/range/state 校验；WebView 初始化失败、目标退出和 Hook 无响应均有明确状态 |
| 7 | 100/150/200% DPI 无裁切；鼠标/触摸/笔使用 Pointer Capture 且拖动 1:1；死区不跨端点跳变；映射往返通过单测；重新抓取立即中断弹簧；reduced-motion 下不运行弹簧 |
| 8 | 现实 10s 在 0.5× 下为 `5.0±0.1s`；现实 5s 在 2× 下为 `10.0±0.1s`；四 API 10s 累计差不超过 20ms；逻辑 Hook 生命周期循环 100 次不崩；健康 IPC 下松手 250ms 内 desired/applied 最终一致 |
| 9 | 动画审查无高优先级问题；无持续高成本 SVG filter/noise；干净 Win10/11 VM 能控制自有 x86/x64 TestApp；WebView2 缺失时给出可执行安装入口；NOTICE 和哈希齐全 |
| 10 | 细线双外环/内环和四向连接桥与参考图比例接近；倍率不重叠；倍率分针与 `1/12` 联动时针独立；`320/360/440` 与 100/150/200% DPI 视觉、交互、可访问性和构建回归全部通过 |
| 11 | 非 TestApp 名称的 x86/x64 夹具通过预检、确认和完整控制；风险命名模块及缓解策略不影响预检；管理员目标经标准 UAC 后重新枚举和预检；硬门禁发生在首次写入前；错误来自真实系统或 Hook 结果 |
| 12 | 干净 Win10/11 VM 同时通过 TestApp 与普通离线测试程序矩阵；诊断、许可证、签名、哈希、CI 和 Git 发布基线齐全；不存在把技术预览误标为产品完成的文案 |

## 8. 测试设计补充

### 8.1 VirtualClock

- 使用外部 fake QPC，不使用已经被 Hook 的 API 测量“现实时间”。
- 覆盖 `0.05 / 0.5 / 1 / 2 / 8`。
- 覆盖任意旧倍率到新倍率。
- 覆盖暂停、暂停时改倍率、恢复、RESET。
- 覆盖接近 `UINT32_MAX` 的 Tick32/timeGetTime 回绕。
- 覆盖宽乘法、加法和极大 delta 溢出路径。
- 并发测试定义为“每个 reader 线程的连续读取不倒退”，不强求跨线程返回完成顺序全局单调。

### 8.2 Hook

- TestApp 同时使用静态导入与 `GetProcAddress` 调用 API。
- 覆盖 `kernel32`/`kernelbase` 转发和实际目标地址去重。
- `timeGetTime` 覆盖 `winmm`。
- 倍率变化只更新 VirtualClock，不重复启停 Hook。
- 关闭时在高并发调用四个 API 的情况下验证 rundown。

### 8.3 IPC/UI

- 帧拆分、粘连、截断和 payload 上限。
- NaN、Inf、负数、0 和越界倍率。
- 旧 session、重复和乱序 sequence。
- 60/120Hz 拖动突发下队列有界。
- 迟到 ACK 不覆盖新状态。
- 超时后通过 GET_STATUS 对账，不擅自假定成功或失败。
- WebView reload 不等于 Controller 与 Hook DLL 断连。

### 8.4 安全

- 所有拒绝分支优先使用 policy unit test 和自建 fixture。
- PPL、Windows 受保护进程和关键系统进程只验证写入前拒绝；不尝试绕过。
- 不启用 `SeDebugPrivilege`。
- 默认不请求管理员权限；只有原始结果为 `INTEGRITY_MISMATCH` 且用户确认时，才允许触发一次 Windows 标准 UAC。

## 9. 分发前仍可后置的决定

以下不阻塞 VirtualClock 和 TestApp：

- 项目最终开源许可证。
- 便携 ZIP 之后是否增加安装器。
- Authenticode 证书和发布签名流程。
- 自动更新。
- 参考图片公开使用权确认。

建议初期：

- 便携 ZIP。
- WebView2 Evergreen。
- Windows 系统字体，例如 Segoe UI Variable。
- 不分发 SF Pro。
- 自动生成第三方 SPDX/NOTICE。

## 10. 官方技术参考

- MinHook：https://github.com/TsudaKageyu/minhook
- WebView2 Win32：https://learn.microsoft.com/en-us/microsoft-edge/webview2/
- WebView2 Runtime 分发：https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/evergreen-vs-fixed-version
- WebView2 线程模型：https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model
- WebView2 安全：https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security
- DllMain：https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain
- IsWow64Process2：https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2
- QueryPerformanceCounter：https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
- GetTickCount64：https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64
- timeGetTime：https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timegettime
