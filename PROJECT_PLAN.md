# TimeTransle 项目交接说明

> 状态：阶段 10 表盘与阶段 11 普通程序实现已完成；阶段 12 已生成本地未签名 `0.1.4` 候选包，`0.1.5` 在其上新增 `` ` `` 快捷键组、进程图标与状态行三态，正式发布仍等待外部兼容性、UAC 取消路径、签名与干净 VM 验收  
> 最后更新：2026-08-29（Asia/Shanghai）  
> 适用对象：在新 Codex 对话中继续搭建和实现 TimeTransle 的开发者/代理

## 1. 先读什么

开始实现前，按顺序完整阅读：

1. [`docs/requirements-and-boundaries.md`](docs/requirements-and-boundaries.md)
2. [`docs/architecture-and-protocol.md`](docs/architecture-and-protocol.md)
3. [`docs/frontend-design.md`](docs/frontend-design.md)
4. [`docs/development-plan.md`](docs/development-plan.md)
5. [`docs/release-and-validation.md`](docs/release-and-validation.md)

这些文件共同构成本项目当前的规范。若它们与用户后续明确指示冲突，以用户最新指示为准。

## 2. 当前状态

- 项目暂定名和当前确认名：**TimeTransle**。
- 已建立双架构 CMake 工程，并实现 Controller、ControllerCore、Hook DLL、Injector、TestApp、IPC、安全策略和 VirtualClock。
- x64 产物包括 `TimeTransleController.exe`、Injector64、Hook64 和 TestApp64；x86 产物包括 Injector32、Hook32 和 TestApp32。
- 已实现独立 `VirtualClock` 静态库：支持 fake real-QPC source、连续重锚、独立暂停、`0.05×–8.0×` 倍率、统一 QPC/Tick64/Tick32/timeGetTime 换算、DWORD 自然回绕、溢出保护和原子 payload seqlock。
- `VirtualClock` 的确定性、全倍率切换、边界、溢出和 32-reader 并发测试已在 x64/x86 Debug 下通过；并发测试在两个架构各连续运行 20 次无失败。
- Hook DLL 已通过自有 TestApp 的普通加载验证四个时间 API、原始 trampoline、QPF 不变、一次性启用、失败回滚和逻辑停用。
- IPC V1 已实现 64 字节固定头、部分读写、会话/令牌/序号校验、有界倍率队列，以及 Controller 断连后 Hook 本地立即连续恢复 `1.0×`、解除暂停。
- SafetyPolicy 已精简为 PID/创建时间、架构、完整性、PPL/保护、关键进程和固定 Hook DLL 身份硬门禁；TestApp 名称、AppContainer、缓解策略、风险模块和现有 Hook 模块预扫描已移除。重复连接由 Hook 初始化返回 `ALREADY_ATTACHED`。
- x64 Win32 Controller 与 WebView2 STA 宿主已实现：只映射 `https://app.timetransle.local/index.html`，禁止外部导航、新窗口、权限、宿主对象与开发者工具，严格校验 Web 消息并提供进程列表、稳定目标身份、状态快照和目标退出监视。
- Controller→Injector→Hook 的受控端到端编排已接通：会话密钥仅经受限继承管道传递，命名管道执行双向身份/令牌校验；真实 Controller 窗口已针对 TestApp 完成进程选择、同步、`2×`、暂停/继续、`1×` 与安全关闭烟雾验证。
- 阶段 8 已通过现实 5 秒的 `2×`、现实 10 秒的 `0.5×`、四 API 累计误差、正常关闭/Controller 断连复位及 x86/x64 各 100 次完整会话生命周期循环。
- 已固化 IPC V1 帧、消息编号、错误码、WebView JSON Schema、测试向量、依赖锁和 7 份 ADR。
- 已完成“桌面旋钮”功能框架：默认约 `360×360`、限制为 `320–440` 的近方形窗口、无最大化布局；目标/连接状态压缩为一行，底部仅保留暂停、恢复 `1×` 与选择进程，精确输入、日志与错误进入弹层。
- 前端已按参考图实现细线完整双外环、四向连接桥、较大的细线内环、独立倍率分针和按分针角度增量 `1/12` 联动的时针，并接入目标预检、选择后自动连接、UAC 状态与全局快捷键。
- SVG 旋钮的对数映射、Pointer Capture、直接跟手、死区、吸附、键盘与精确输入、desired/applied 分离、乱序确认过滤、reduced-motion 及可访问语义已经实现，修订时必须保留。
- 倍率映射为全程统一 `45°`/八度的单斜率对数映射（`speed = 2^(θ/45)`），有效弧 `[-180°, +135°]`：指针可转到表盘正下方，死区收窄为右下方 `45°` 楔形，且不能越过底部转到另一侧。旋钮可达 `0.0625×–8×`，契约下界 `0.05×` 只能由精确输入弹层到达。旧的分段对数映射（减速侧 `ln 20`、加速侧 `ln 8`）在 `1×` 处有斜率断点，已废弃。
- 吸附点集为 `0.1 / 0.2 / 0.25 / 0.4 / 0.5 / 1 / 2 / 4`，采用拖动中实时 detent：进入阈值 `3.5°`、退出阈值 `6°`，最小相邻间隔 `14.487°` 保证捕获区互不重叠；判定逻辑是纯函数并有单元测试锁定。保持吸附期间不重复发送相同倍率的 IPC。
- UI 已实际通过 ESLint、106 个 Vitest 契约/Schema/状态机/映射/交互测试和 Vite 生产构建。
- x64 原生窗口默认 `360×360`，按 DPI 约束在 `320–440`，保持方形缩放、移除最大化入口，并记忆上次位置与尺寸。
- 已安装 Visual Studio Build Tools 2022 `17.14.37`、MSVC v143 x86/x64 与 Windows 11 SDK `10.0.26100.0`；CMake `4.4.0` 已完成双架构 Debug/Release 构建与回归：x64 16 项 CTest、x86 14 项 CTest，以及 x64 Controller 控制 x86 TestApp/普通命名夹具的跨架构 E2E 均通过。
- 已执行 `$review-animations` 与 `$improve-animations`；未发现高优先级动效问题，并把 reduced-motion 从“全部瞬时关闭”收敛为保留 80–120ms 颜色/透明度反馈、移除位移动效。
- 已生成只含用户运行时产物的便携 ZIP；Release 使用静态 MSVC 运行库，包内文件级与 ZIP 级 SHA-256 均已核对。当前候选版本为 `0.1.5`：在 `0.1.4` 悬停上浮/提亮交互（`ClockDialLift`）之上，新增 `` `+F1/F2/F3 `` 倍率开关快捷键（检查点重记语义）、`` `+Esc `` 显隐窗口、进程选择列表原生图标与状态行三态文案，仍是未签名技术预览；正式版本需要在交互式 UAC 取消路径、外部兼容性和干净 VM 验收后重新签名打包。
- Controller 已嵌入由 `参考图.jpg` 简化复刻的 9 尺寸 Windows 图标，覆盖标题栏、任务栏与资源管理器；SVG 主稿和可重复生成脚本位于 `src/controller/resources` 与 `scripts/generate-icon.ps1`。
- 全局快捷键自 `0.1.5` 起为 `` ` `` 修饰键组合：`` `+F1/F2/F3 `` 开关切换 `1×`/`0.4×`/`2.5×`（每次激活重记唯一检查点，再次按同键恢复；切换不同键作废旧检查点），`` `+Esc `` 切换窗口显隐；旧 `Insert+F10/F11/F12` 已移除。
- 已创建 Windows x86/x64 与 UI 的 GitHub Actions 初始矩阵，但尚未在远端执行。
- 已在项目级 `.agents/skills` 安装 `emilkowalski/skills` 的 7 个 Skill。
- 本轮规划阶段已完整读取：
  - `$pick-ui-library`
  - `$emil-design-eng`
  - `$apple-design`
  - `$review-animations`
  - `$improve-animations`
- 用户已确认阶段 7 的紧凑桌面控制器方向；后续继续执行，不再逐步等待确认。
- `skills-lock.json` 已记录 Skill 来源和哈希。

环境预检结果：

- Node.js：`v24.14.0`
- WebView2 Runtime：`150.0.4078.83`
- PowerShell 禁止执行 `npx.ps1`，但 `npx.cmd` 可正常使用。
- CMake：项目本地 `4.4.0`；Ninja：项目本地 `1.13.0`。
- Visual Studio Build Tools 2022：`17.14.37`；MSVC：`19.44.35228`；MSBuild：`17.14.51`。
- Windows SDK：`10.0.26100.0`；`vswhere.exe` 已安装在标准位置。
- `scripts/bootstrap.ps1` 返回 `Ready: True`，且安装不要求重启。

仓库根目录的 `.vsconfig` 固定所需组件；`bootstrap.ps1`、`build-all.ps1` 和 `test-all.ps1` 均可回退发现项目本地 CMake/CTest/Ninja。

## 3. 已冻结的关键决策

### 3.1 产品边界

- 仅连接用户主动选择且已经运行的 Windows 本地程序，不自动扫描注入、跟随子进程或后台驻留。
- V1 主机范围：x64 Windows 10/11。
- V1 目标范围：已经运行的普通 x86/WOW64 或 x64 桌面进程。
- 不支持 ARM64/ARM64EC、PPL、Windows 受保护进程、系统关键进程、自动子进程注入或启动前注入；AppContainer 不预先拒绝，但不保证标准加载成功。
- 所有加载和 Hook 操作必须透明、由用户显式触发。
- 普通权限目标默认由普通权限 Controller 连接；高完整性目标只在原始结果为 `INTEGRITY_MISMATCH` 且用户确认后，通过 Windows 标准 `runas`/UAC 重启 Controller。
- 不建立游戏、反作弊、保护软件、联网程序、发行商、商店、路径、窗口标题或模块名称白名单/黑名单；不预先拒绝 AppContainer 或 Dynamic Code、签名、CFG 缓解策略。

### 3.2 明确禁止

- 权限提升漏洞利用、UAC 绕过、令牌窃取、静默提权或无用户确认的管理员重启。原始结果为 `INTEGRITY_MISMATCH` 时用户显式触发的 Windows 标准 UAC 重启不在禁止范围内。
- manual map、模块隐藏、进程隐藏、线程劫持、APC 注入或任何规避检测的加载方式。
- 内核驱动。
- 受保护进程、反作弊或反注入机制绕过。
- 允许前端或 Injector 指定任意 DLL 路径。
- 安全检查失败后提供“强制继续”选项。

### 3.3 断连必须立即恢复 1.0×

**这是用户最后明确确认的冻结要求：**

当 `TimeTransleController.exe` 与目标进程内的 Hook DLL 连接一经确认断开时，Hook DLL 必须在目标进程内本地、立即执行：

1. 使用原始 QPC 读取当前真实时间。
2. 使用断开前的旧状态计算当前虚拟时间。
3. 连续重锚，避免虚拟时间跳变。
4. 将 `configuredSpeed` 设置为 `1.0`。
5. 将 `paused` 设置为 `false`。
6. 发布新的时钟 revision。

不得：

- 保持最后倍率。
- 保持暂停。
- 在确认断开后再等待宽限期。
- 依赖 Controller 还能发送 `RESET`。
- 因恢复到 1.0× 而让虚拟时间倒退或跳变。

“确认断开”至少包括命名管道返回 broken/disconnected、Controller 进程句柄已触发，或协议规定的连接存活检测已经判定对端消失。WebView2 页面单独 reload、但原生 Controller 与 Hook DLL 连接仍健康时，不属于这里定义的断连。

### 3.4 跨架构

发布/开发产物增加：

- `TimeTransleInjector32.exe`
- `TimeTransleInjector64.exe`

x64 Controller 通过与目标同位数的短生命周期助手加载固定的 Hook DLL。Injector 不是通用注入器。

### 3.5 DLL 生命周期

V1 的关闭/卸载默认定义为：

```text
恢复安全状态
→ 停止接受新命令
→ 禁用 Hook
→ 等待在途 detour 结束
→ 清理 MinHook 与 IPC
→ DLL 作为 inert module 驻留至目标退出
```

V1 不强制在目标仍运行时 `FreeLibrary`。真正热卸载只有在后续完成并验证严格 rundown/refcount 方案后才考虑。

## 4. 目标产物

用户发布包：

```text
TimeTransleController.exe
TimeTransleInjector32.exe
TimeTransleInjector64.exe
TimeTransleHook32.dll
TimeTransleHook64.dll
resources/ui/...
licenses/...
```

开发/测试包额外包含：

```text
TimeTransleTestApp32.exe
TimeTransleTestApp64.exe
单元测试、集成测试与 E2E 测试工具
```

## 5. 核心实现顺序

不要从注入层开始。建议顺序：

1. 安装/确认 C++ 工具链。
2. 创建双架构 CMake preset、目录骨架、依赖锁和协议契约。
3. 实现可注入 fake real-time source 的纯 `VirtualClock`。
4. 完成倍率、切换、暂停、DWORD 回绕和并发测试。
5. 实现 TestApp32/64。
6. 实现四个时间 API Hook 与失败回滚。
7. 实现命名管道、ACK、状态对账和强制断连复位。
8. 实现安全筛查和 Injector32/64。
9. 实现 x64 Controller 与 WebView2 bridge。
10. 实现 React/SVG 时钟旋钮与完整状态机。
11. 执行端到端、生命周期、安全边界和性能测试。
12. 完整读取并执行 `$review-animations`、`$improve-animations`。
13. 完成受控技术预览包、许可证与本地哈希门禁。
14. 修订前端表盘几何、布局和独立双指针，并重新执行视觉、交互和可访问性回归。
15. 将 SafetyPolicy 精简为普通程序最小硬门禁；加入只读预检、选择后自动连接、标准 UAC 重启、真实加载/Hook 错误、全局快捷键和受限 JSONL 诊断。
16. 使用普通命名 x86/x64 夹具和实际获准程序完成兼容性矩阵，记录成功、部分生效或真实失败。
17. 完成产品级回归后，再执行 Authenticode、干净 Win10/11 VM 和正式分发门禁。

每个阶段的量化验收条件见 `docs/development-plan.md`。

## 6. 新对话的第一项任务

建议新对话直接使用以下目标：

> 完整阅读 `PROJECT_PLAN.md` 及其链接规范。保持 `360×360` 桌面旋钮和断连立即恢复 1.0×策略。阶段 11 的最小硬门禁、只读预检、选择后自动连接、标准 UAC 重启、全局快捷键和受限诊断已实现；下一步在管理员夹具与干净 VM 上点测 UAC，并建立只记录实际成功、部分生效或真实失败的兼容性矩阵。不得加入程序/反作弊/联网/模块名称名单，也不得实现隐藏加载、manual map、驱动或保护绕过。

## 7. 重要说明

- “通用变速”仅表示覆盖大量读取这些 Win32 时间 API 的传统程序，不承诺支持所有游戏。
- Unity Mono 已通过运行时元数据接入 `Time.timeScale`；Unity IL2CPP、其他引擎私有时钟、RDTSC/RDTSCP、固定步长、服务器权威逻辑、音频设备时钟和预渲染视频仍可能不受影响。
- 产品不扫描风险模块或联网状态；硬门禁无法完成时拒绝，硬门禁通过后报告标准加载与 Hook 的真实结果。
- TestApp 是每次改动必须运行的确定性回归基准，但不再是产品支持的唯一目标。
- 普通程序兼容性测试只操作用户主动选择且有权操作的程序，不根据名称或类别加入特例。
- PPL、Windows 受保护进程和关键系统进程必须在首次写入前拒绝；不尝试绕过目标自身保护。
