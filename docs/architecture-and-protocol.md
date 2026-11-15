# TimeTransle 架构、时钟模型与通信协议基线

> 状态：已确认的核心设计基线；阶段 11 普通程序最小硬门禁已接入  
> 项目阶段：阶段 11 验收与阶段 12 兼容性、签名及干净 VM  
> 适用版本：V1  
> 最后更新：2026-07-27

## 1. 目的与不可突破的边界

TimeTransle 是一个面向用户主动选择的 Windows 本地程序的时间倍率调节工具。它通过目标进程内的用户态时间虚拟化，让目标程序看到按倍率推进的时间。

V1 支持：

- Windows 10/11 x64 主机。
- 用户明确选择的、已经运行的 x86 或 x64 桌面进程。
- `0.05×` 到 `8.0×` 的倍率。
- 独立暂停状态。
- x86 与 x64 目标的分离构建和同位数加载。
- 四个时间读取 API：
  - `QueryPerformanceCounter`
  - `GetTickCount`
  - `GetTickCount64`
  - `timeGetTime`

V1 不 Hook：

- `QueryPerformanceFrequency`
- `Sleep` / `SleepEx`
- `WaitForSingleObject` 及其他等待函数
- 网络超时函数
- 系统日期、时区和日历时间函数

明确禁止：

- 静默/无确认权限提升、UAC 绕过、令牌窃取或漏洞利用。用户选择高完整性目标后显式确认的 Windows 标准 UAC 重启属于支持流程。
- 启用 `SeDebugPrivilege` 后强行操作高权限目标。
- 隐藏 DLL、隐藏进程或隐蔽持久化。
- Manual Map、反射式加载或其他规避正常模块加载记录的方式。
- 内核驱动。
- 受保护进程、PPL 或反注入机制绕过。
- 反作弊规避、篡改或欺骗。
- 自动向未由用户明确选择的进程或其子进程注入。

硬门禁无法完成时拒绝操作；硬门禁通过后直接尝试标准加载，不根据经验推测兼容性。

准入只依据稳定身份、架构、完整性、PPL/保护状态、关键进程和本地 Hook DLL 身份。AppContainer、缓解策略、模块名称、联网或游戏类别不属于预先拒绝证据；重复连接由 Hook 初始化的真实结果处理。

准入策略不维护游戏、反作弊、保护软件、联网程序、发行商、商店、路径、启动器或模块名白名单/黑名单。只有硬门禁或真实系统加载/Hook 结果可以阻止连接。

## 2. 已确认技术栈与产物

### 2.1 原生侧

- C++20
- CMake
- Win32 API
- MinHook
- WebView2
- 命名管道
- RAII
- 严格错误处理与可读日志
- x86、x64 独立 CMake build tree / preset

### 2.2 前端

- React
- TypeScript
- Vite
- SVG
- Motion
- NumberFlow
- Base UI
- Sonner
- CSS Modules 或结构清晰的普通 CSS

V1 不使用 Electron。React 只负责界面和用户意图，不能直接获得进程句柄、DLL 路径或命名管道访问能力。

### 2.3 可执行产物

```text
TimeTransleController.exe    x64，原生窗口、会话协调与 WebView2 宿主
TimeTransleInjector32.exe    x86，同位数加载助手
TimeTransleInjector64.exe    x64，同位数加载助手
TimeTransleHook32.dll        x86 时间 Hook
TimeTransleHook64.dll        x64 时间 Hook
TimeTransleTestApp32.exe     x86 测试目标
TimeTransleTestApp64.exe     x64 测试目标
```

增加 Injector32/64 是必要的架构调整。Controller 固定为 x64；它不自行跨位数完成远程加载，而是启动与目标同位数、短生命周期的 Injector。Injector 只接受产品目录内、名称固定、架构和 build ID 匹配的 Hook DLL，不接受任意 DLL 路径。

## 3. 模块职责

### 3.1 Controller

Controller 是 UI 和目标会话的协调者，但 DLL 中的 VirtualClock 才是“实际已应用状态”的权威来源。

职责：

- 枚举可选目标进程。
- 保存并反复核验目标身份。
- 判断目标架构。
- 协调匹配架构 Injector 的只读硬门禁预检。
- 选择 Injector32 或 Injector64。
- 托管 WebView2。
- 校验所有 WebView2 消息。
- 创建带严格 ACL 的命名管道服务器。
- 将前端高频倍率意图合并为有界命令流。
- 将 DLL 的 ACK、状态和错误转换为前端状态。
- 监测目标退出、Injector 退出和管道断开。
- 输出不包含秘密、完整用户路径或其他隐私数据的诊断日志。

Controller 不负责：

- 在前端确认之前伪造“已应用”状态。
- 在断连后替 DLL 保持或恢复倍率。
- 静默提升权限或绕过 UAC。Controller 可以在原始结果为 `INTEGRITY_MISMATCH` 时提供明确的标准 `runas`/UAC 重启操作。
- 允许用户跳过安全拒绝。

### 3.2 Injector32 / Injector64

Injector 是一次性、同位数、透明的普通用户态加载助手。

职责：

- 支持只读 `--inspect-only`，只申请完成硬门禁所需的查询权限。
- 从 Controller 接收固定格式的最小启动参数。
- 重新打开并核验 `PID + 创建时间`，防止 PID 重用。
- 再次核验目标架构。
- 核验待加载 DLL 的固定路径、架构、产品 build ID。
- 对新目标使用常规 `LoadLibraryW` 流程加载 DLL。
- 在加载完成后调用 DLL 的显式初始化导出，传入一次性会话 bootstrap。
- 将精确结果返回 Controller 后退出。

重复请求仍执行透明的标准加载调用，由 Windows 引用计数和 Hook 初始化导出的 `ALREADY_ATTACHED` 真实结果决定，不做现有模块名称预扫描。

### 3.3 Hook32 / Hook64

职责：

- 最小化 `DllMain` 工作。
- 在显式初始化入口中建立运行时状态。
- 安装四个时间 API Hook。
- 保存每个原始函数的 trampoline，防止递归。
- 运行命名管道客户端和唯一 VirtualClock writer。
- 对倍率、暂停、恢复和重置命令执行连续重锚。
- 返回包含命令序号和 clock revision 的 ACK。
- 在管道断开时独立执行强制故障安全恢复。
- 安装失败时回滚已经创建或启用的 Hook。
- V1 逻辑停用后保持模块驻留至目标进程退出。

### 3.4 VirtualClock

职责：

- 以原始 QPC 为唯一现实时间基准。
- 保存连续虚拟时间状态。
- 为四个 Hook API 生成同源结果。
- 在无普通互斥锁的读取热路径中提供一致快照。
- 保留 `GetTickCount` 和 `timeGetTime` 的 32 位自然回绕语义。
- 将暂停表示为独立状态，而不是把配置倍率改成 `0.0`。

### 3.5 React / WebView2 UI

职责：

- 表示用户意图 `desiredSpeed` / `desiredMode`。
- 只用 DLL ACK 更新 `appliedSpeed` / `appliedMode`。
- 拖动时本地逐帧更新视觉位置。
- 每个 `requestAnimationFrame` 最多向 Controller 发一次最新倍率。
- 忽略旧 session、旧 target generation 和旧 sequence 的确认。
- 明确显示未知、等待、已应用、停滞、拒绝和目标退出状态。

## 4. 端到端数据流

### 4.1 选择并连接目标

```text
用户选择进程
  → React 发送 selectTarget
  → WebView2 Bridge 校验 origin、schema、范围和当前 UI 状态
  → Controller 取得 PID + 创建时间
  → SafetyPolicy 执行只读检查
  → 判断目标为 x86 或 x64
  → 启动同位数 Injector
  → Injector 再次核验目标身份与 DLL
  → 常规 LoadLibraryW，或对已有兼容 DLL 执行 rebind
  → DLL 显式初始化
  → DLL 连接 Controller 创建的随机命名管道
  → HELLO / HELLO_ACK 相互验证
  → DLL 返回 Hook 状态与当前权威时钟状态
  → UI 进入 hooked + synced
```

任何一步失败都不得继续下一步。尤其是在完成所有只读筛查之前，不得向目标写入内存或启动远程线程。

### 4.2 应用倍率

```text
Pointer / 键盘 / 精确输入
  → React 更新 desiredSpeed
  → 每个 rAF 最多发送一次最新 uiSequence
  → Controller 校验 speed 是 finite 且位于 [0.05, 8.0]
  → CommandCoordinator 合并高频 SET_SPEED
  → 命名管道发送 commandSequence
  → DLL IPC worker 串行处理
  → VirtualClock 按旧状态计算边界 virtualNow
  → 连续重锚并发布新 revision
  → DLL 返回 SET_SPEED_ACK
  → Controller 转发 uiSequence、commandSequence 和权威状态
  → React 仅在确认仍是当前会话时更新 appliedSpeed
```

### 4.3 权威关系

```text
desiredSpeed       React 中的用户意图
queuedSpeed        Controller 中尚未发送的最新合并值
configuredSpeed    DLL 中恢复后应使用的倍率
effectiveSpeed     paused ? 0 : configuredSpeed
appliedSpeed       DLL ACK 返回的 configuredSpeed
clockRevision      DLL 每次成功状态变更后递增
```

UI 不得因为本地指针已移动就显示“已应用”。当状态未知或 ACK 超时时，最后一个确认值只能标为“最后已知”。

## 5. 目标身份与安全筛查

### 5.1 稳定身份

进程身份必须至少包含：

```text
processId
creationTime
machineType
```

显示名称和可执行文件路径只能作为 UI 信息，不能替代身份。每个会话使用独立 `targetGeneration`。在以下边界必须重新核验 `PID + creationTime`：

- 用户选择目标后。
- 启动 Injector 前。
- Injector 对目标进行任何写操作前。
- 命名管道握手时。
- 处理可能来自重连会话的状态时。

### 5.2 筛查顺序

1. 用户选中目标后，Controller 启动匹配架构 Injector 的 `--inspect-only`。
2. Injector 以 `PROCESS_QUERY_LIMITED_INFORMATION` 打开目标并核对 PID 与创建时间。
3. 使用 `IsWow64Process2` 判断目标是否为受支持且匹配的 x86/WOW64 或 x64。
4. 比较 Controller/Injector 与目标完整性级别。
5. 检查 PPL、Windows 受保护状态和关键系统进程。
6. 检查产品目录中固定 Hook DLL 的架构、版本和 build ID。
7. 将 `checking → ready / elevationRequired / rejected` 结果返回 UI。
8. 预检通过后，Controller 自动开始正式连接，重新执行全部硬门禁并尝试标准加载；目标选择本身就是本次连接的用户显式动作。

预检不申请 VM 写入权限、不加载 DLL。切换目标、目标退出或身份变化时必须丢弃旧结果。

停止或拒绝条件包括但不限于：

- 目标完整性高于 Controller 时，当前连接流程停止并返回 `INTEGRITY_MISMATCH`。这是一种可恢复状态：只有用户明确确认标准 UAC 重启后，提升版 Controller 才能从稳定身份开始重跑全部门禁。
- PPL、受保护进程或关键系统进程。
- 目标架构不受支持。
- 硬门禁查询因访问被拒绝或数据不完整而无法完成。
- 目标身份在操作期间发生变化。
- 产品 Hook DLL 身份、架构、版本或 build ID 不匹配。

AppContainer、Dynamic Code、签名策略、CFG、模块名称和联网状态不在此列表中。它们若令标准加载或 Hook 失败，UI 显示真实的 `ACCESS_DENIED`、`DLL_LOAD_FAILED`、`HOOK_CREATE_FAILED` 等结果。

真实 Win32 查询/访问失败使用 `ACCESS_DENIED`；目标完整性更高使用 `INTEGRITY_MISMATCH`；UAC 启动失败使用 `ELEVATION_FAILED`，用户取消通过中性 `elevationCancelled` 消息表达。`APPCONTAINER_UNSUPPORTED`、`MITIGATION_POLICY_UNSUPPORTED`、`RISK_MODULE_DETECTED` 和 `RISK_CHECK_INCOMPLETE` 仅保留 ABI 数值，普通目标准入不再产生。

## 6. VirtualClock 统一时间模型

### 6.1 核心公式

```text
virtualNow =
    virtualAnchor
    + (realNow - realAnchor) × effectiveSpeed
```

其中：

```text
configuredSpeed ∈ [0.05, 8.0]
paused          ∈ {false, true}
effectiveSpeed  = paused ? 0.0 : configuredSpeed
```

`realNow` 始终来自原始 `QueryPerformanceCounter` trampoline。`QueryPerformanceFrequency` 在初始化时读取并固定，但不被 Hook。

### 6.2 连续重锚

每次改变倍率或暂停状态都必须按相同顺序：

1. 通过原始 QPC trampoline 读取 `realNow`。
2. 使用旧状态计算该边界上的 `virtualNow`。
3. `realAnchorQpc = realNow`。
4. `virtualAnchorQpc = virtualNow`。
5. 最后写入新的 `configuredSpeed` 和 `paused`。
6. 递增 `clockRevision` 并原子发布完整状态。

不能使用 `virtualNow = realNow × speed`。切换倍率只改变此后的斜率，不改变切换点的虚拟时间值。

### 6.3 暂停语义

- `PAUSE` 连续重锚后设置 `paused = true`。
- 暂停期间虚拟时间保持在 `virtualAnchorQpc`。
- 暂停期间 `SET_SPEED` 只更新 `configuredSpeed`，不推进虚拟时间。
- `RESUME` 从当前冻结值连续重锚，随后使用 `configuredSpeed`。
- `RESET` 原子地设置 `configuredSpeed = 1.0`、`paused = false`，并保持边界连续。
- UI 中的旋钮在暂停时仍表示恢复后倍率。

### 6.4 四个 API 共享同一时间线

初始化时读取并保存：

```text
qpcOrigin
tick64Origin
tick32Origin
timeGetTimeOrigin
qpcFrequency
```

每次 Hook 调用先得到统一 `virtualQpc`，再换算：

```text
virtualElapsedQpc = virtualQpc - qpcOrigin
virtualElapsedMs  = floor(virtualElapsedQpc × 1000 / qpcFrequency)

QPC         = virtualQpc
Tick64      = tick64Origin      + virtualElapsedMs
Tick32      = low32(tick32Origin + virtualElapsedMs)
timeGetTime = low32(timeOrigin   + virtualElapsedMs)
```

实现必须使用经过溢出分析的宽位乘除或等价安全算法，不能让中间乘法溢出。`GetTickCount` 和 `timeGetTime` 的低 32 位截断是有意保留的 Windows 回绕行为，不能用饱和运算代替。

### 6.5 精度与单调性

- 支持的倍率均非负。
- 同一线程连续调用 64 位时间源不得倒退。
- 多线程读者可能以不同完成顺序返回；V1 不用全局 atomic-max 强制建立跨线程完成顺序，因为那会在 QPC 热路径制造严重缓存争用。
- 状态切换以 writer 发布新偶数 sequence 的时刻作为线性化点。
- 新旧状态在切换边界计算出同一个 `virtualNow`。
- 若浮点缩放产生亚 tick 小数，统一采用已测试的截断规则；writer 和 reader 必须调用完全相同的缩放函数。

## 7. 线程安全设计：原子 payload seqlock

### 7.1 为什么不用其他候选

| 方案 | 结论 |
| --- | --- |
| 普通 mutex / SRWLOCK | 正确但不适合高频 QPC Hook 热路径 |
| 多个无版本的独立 atomic | 可能拼接出不同代次的锚点和倍率 |
| `atomic<State>` | 大状态在 x86/x64 上通常不保证 lock-free |
| `atomic<shared_ptr<State>>` | 每次读有引用计数和潜在锁开销 |
| 普通 struct + 版本号 | payload 存在 C++ data race，属于未定义行为 |
| 简单双缓冲 | slot 复用时仍可能和读者并发访问 |
| RCU / 多缓冲回收 | 可行但 V1 复杂度过高 |
| atomic payload + seqlock | 推荐，单 writer、读者无普通锁且无 data race |

### 7.2 建议状态

概念字段如下，实际 wire layout 与内存 layout 不应绑定：

```text
atomic<uint64> sequence          奇数=writer 正在更新，偶数=稳定
atomic<int64>  realAnchorQpc
atomic<int64>  virtualAnchorQpc
atomic<uint64> speedBits         已校验 double 的位表示
atomic<uint32> flags             paused 等
atomic<uint64> clockRevision
```

固定不变的 `qpcFrequency` 和各 API origin 在 Hook 发布前完成初始化，之后只读。

### 7.3 Reader

1. acquire 读取 `sequence`。
2. 若为奇数则短暂重试。
3. relaxed 读取所有 atomic payload。
4. 调用原始 QPC trampoline 得到 `realNow`。
5. acquire 再读 `sequence`。
6. 若两次 sequence 不同或为奇数，丢弃快照并重试。
7. 用接受的快照计算输出。

Hook 热路径禁止：

- 普通 mutex。
- 堆分配。
- 文件或管道 I/O。
- 日志格式化。
- 调用任何可能重新进入被 Hook 时间 API 的代码。

### 7.4 Writer

命名管道 IPC worker 是正常运行时唯一 writer。所有命令在该 worker 上串行处理。

1. 将 sequence 从稳定偶数推进为奇数。
2. 读取原始 QPC。
3. 用旧 payload 计算边界 `virtualNow`。
4. 写入新的原子 payload。
5. 增加 `clockRevision`。
6. 用 release 发布新的偶数 sequence。

故障安全断连处理也必须串行进入同一个 writer 路径；不能从多个回调并发修改时钟。

## 8. 断连故障安全策略

这是已确认的强制行为，不是可配置偏好：

> Controller 与 Hook DLL 的连接一经确认断开，DLL 必须立即在本地连续重锚到 `1.0×`，同时解除暂停。

精确定义：

```text
configuredSpeed = 1.0
paused          = false
effectiveSpeed  = 1.0
```

要求：

- 无 grace period。
- 无 keep-last 模式。
- 不等待 Controller 指令。
- 不等待 UI。
- 不等待重新连接。
- 不从磁盘读取策略。
- 先完成本地重锚，再尝试记录或重连。
- 恢复的是正常推进斜率 `1.0×`，不是把虚拟时间数值瞬间改回原始系统 epoch。

“确认断开”包括：

- 命名管道 EOF。
- `ERROR_BROKEN_PIPE` / `ERROR_PIPE_NOT_CONNECTED` 等终止性错误。
- 已绑定 Controller 进程句柄变为 signaled。
- 协议层确认会话不可继续的终止性认证或版本错误。
- 若实现心跳，达到明确的 liveness failure 条件。心跳超时是断连检测机制，不是保持旧倍率的宽限期；一旦条件成立就立即恢复。

恢复必须沿用普通 `RESET` 的连续重锚算法，因此：

- 慢速历史不会瞬间向真实 QPC 跳跃。
- 加速历史不会因回到真实 QPC 而时间倒退。
- 暂停解除后从冻结点以 `1.0×` 继续。

管道断开后 DLL 可以等待显式 rebind，但在任何新会话握手完成前必须持续保持 `1.0×` 且未暂停。重新连接也不得自动恢复旧倍率。

## 9. Hook 安装与 DLL 生命周期

### 9.1 DllMain

`DllMain` 只做 loader-lock 安全的最小工作：

- 保存模块句柄。
- 可调用 `DisableThreadLibraryCalls`。
- 初始化不依赖其他 DLL 的简单常量。

禁止在 `DllMain`：

- 安装或移除 Hook。
- 创建命名管道。
- 等待线程。
- 初始化 COM。
- 写日志文件。
- 执行耗时扫描。
- 调用可能触发 loader lock 重入的复杂 API。

Injector 在 `LoadLibraryW` 完成后调用显式导出，例如 `TimeTransleInitialize`。该入口运行在 loader lock 之外，完成 bootstrap 校验、IPC 和 Hook 安装。

### 9.2 两阶段安装与回滚

1. 校验 bootstrap、build ID、架构和进程身份。
2. 解析并保存所有原始函数地址。
3. 初始化 VirtualClock origins。
4. `MH_Initialize`。
5. 创建全部 Hook，但尚不向 Controller 宣告成功。
6. 使用队列方式统一启用 Hook。
7. 确认四个 Hook 状态。
8. 发布 `HOOKS_ACTIVE`，随后接受倍率命令。

任何一步失败：

- 不发布成功状态。
- 禁用已经启用的 Hook。
- 移除已经创建的 Hook。
- 清理 MinHook。
- 关闭 IPC。
- 返回具体错误。

HookManager 需要对可能解析到相同底层地址的转发导出做去重，且每个 detour 只能调用保存的 trampoline，不能再次通过普通导出名调用自身。

### 9.3 逻辑停用

V1 默认不在目标运行期间强制 `FreeLibrary`。原因是经历慢速或加速后，虚拟时间相对真实时间已有偏移；直接移除 detour 会造成突跳，甚至时间倒退。

V1 的“安全停用”定义为：

1. 连续重锚到 `1.0×` 并解除暂停。
2. 停止接受新的倍率命令。
3. 关闭或解绑当前 IPC 会话。
4. 保持 detour 在 `1.0×` 连续虚拟时间线上工作。
5. DLL 保持驻留直到目标进程自然退出。

因此，V1 的 `SHUTDOWN` 是“逻辑停用当前控制会话”，不是远程热卸载 DLL。

未来若实现真正热卸载，必须另外解决：

- 虚拟 epoch 与真实 epoch 的无跳变重新同步。
- detour 在途调用的 rundown / 引用计数。
- 禁用 Hook 与代码卸载之间的严格顺序。
- 所有线程不再执行 DLL 代码的证明。

在这些条件被实现和测试之前，不得调用远程 `FreeLibrary`。

### 9.4 目标进程退出

- Controller 通过进程句柄监测退出。
- 目标退出时 UI 立即进入 `target.exited`，清除 `appliedSpeed`，保留必要的最终诊断。
- 不尝试在进程 teardown 阶段执行复杂清理。
- `DLL_PROCESS_DETACH` 在进程整体终止时只做最小工作，不等待线程或执行 MinHook 生命周期操作。

## 10. WebView2 JSON 协议

### 10.1 信任边界

- 生产版只加载随应用打包的 Vite 静态资源。
- Bridge 只接受受信 origin / document。
- 当前原生实现只接受 `https://app.timetransle.local/` 与 `https://app.timetransle.local/index.html`，拒绝其他导航、新窗口和权限请求。
- 每条消息都进行严格 schema 校验。
- 宿主输入上限为 65,536 个 UTF-16 code unit，UTF-8 JSON 校验层上限为 65,536 字节；超限、非法编码、非法 JSON 和 schema/range/state 不匹配均拒绝。
- 未知字段是否允许必须在 schema 中明确；默认拒绝关键命令的未知字段。
- 所有数字必须验证类型、`isfinite` 和范围。
- React 无权提交 DLL 路径、管道名、远程地址或原始 HANDLE。
- 日志不得记录 session token。

### 10.2 UI 到 Controller

示例：

```json
{
  "v": 1,
  "type": "setSpeed",
  "uiEpoch": "f7b59f60-0ec6-47e1-a43e-4ba05fd8e57d",
  "uiSequence": 42,
  "targetGeneration": "9f2452b1-0acf-46d4-ad72-384ffb88335e",
  "payload": {
    "speed": 0.5
  }
}
```

必要公共字段：

| 字段 | 含义 |
| --- | --- |
| `v` | WebView 协议主版本 |
| `type` | 消息类型 |
| `uiEpoch` | 每次页面启动生成的新 ID |
| `uiSequence` | 当前 uiEpoch 内严格递增 |
| `targetGeneration` | 当前目标会话代次 |
| `payload` | 按 type 校验的载荷 |

UI 命令至少包括：

```text
listProcesses
selectTarget
attachTarget
setSpeed
pause
resume
reset
getStatus
deactivate
```

`attachTarget` 作为 V1 ABI 兼容项保留；正常 UI 流程不再发送它，Controller 在 `targetInspection=ready` 后直接进入正式连接。

### 10.3 Controller 到 UI

示例：

```json
{
  "v": 1,
  "type": "speedApplied",
  "controllerSessionId": "bdcb4424-0d7c-43f9-8eea-7534df85001f",
  "uiEpoch": "f7b59f60-0ec6-47e1-a43e-4ba05fd8e57d",
  "uiSequence": 42,
  "targetGeneration": "9f2452b1-0acf-46d4-ad72-384ffb88335e",
  "commandSequence": 17,
  "clockRevision": 9,
  "payload": {
    "configuredSpeed": 0.5,
    "paused": false,
    "effectiveSpeed": 0.5
  }
}
```

React 只有在以下条件全部满足时才能更新 `appliedSpeed`：

- `controllerSessionId` 是当前 Controller。
- `uiEpoch` 是当前页面。
- `targetGeneration` 是当前目标。
- `uiSequence` 不早于已接受确认。
- `commandSequence` 和 `clockRevision` 未倒退。

Controller 还可发送无目标序号的 `shortcutInvoked`，其载荷只允许 `{action: "toggleSpeed", targetSpeed: 1 | 0.4 | 2.5}`。前端必须基于当前已 Hook 会话处理该动作：目标倍率与当前倍率一致且没有检查点时不得发送倍率命令，切换与恢复都通过现有 `setSpeed`（必要时先 `resume`）完成。`` `+Esc `` 的窗口显隐完全由原生窗口层处理，不经过 WebView。

## 11. Controller 与 DLL 命名管道协议

### 11.1 传输与安全

- Controller 创建随机、不可预测名称的本地命名管道服务器。
- DLL 是管道客户端。
- 使用 message mode 或带完整 framing 的 byte mode；实现必须能正确处理部分读写。
- 管道禁止远程客户端。
- DACL 只允许当前用户和实现所需的最小系统主体。
- Controller 通过 `GetNamedPipeClientProcessId` 等机制核验连接方 PID，并重新核验创建时间。
- DLL 核验 bootstrap 中的 Controller PID/创建时间。
- Injector 向 DLL 传入一次性随机会话令牌，HELLO 必须证明持有该令牌。
- 令牌不写日志、不写配置、不发送到 React。
- 单帧 payload 上限建议为 64 KiB。

### 11.2 帧格式草案

所有字段使用显式小端序列化；不能直接发送编译器布局的 C++ struct。

```text
magic             4 bytes   ASCII "TTLE"
protocolMajor     u16
protocolMinor     u16
headerBytes       u16
messageType       u16
flags             u32
payloadBytes      u32
sessionId         16 bytes
requestId         u64
commandSequence   u64
reserved          固定为 0，长度由 headerBytes 定义
payload           payloadBytes
```

要求：

- `magic`、版本、header 长度和 payload 长度先验证再分配。
- 大于上限、截断、重复字段、未知必要 flags 或保留位非零均拒绝。
- 主版本不兼容时终止会话并触发 DLL 故障安全恢复。
- 次版本只能以显式声明的向后兼容规则协商。
- `requestId` 用于请求/响应关联。
- `commandSequence` 在每个 session 内严格递增。
- ACK 必须回显对应 request ID 和 command sequence。

### 11.3 消息集合

```text
HELLO
HELLO_ACK
GET_STATUS
STATUS
SET_SPEED
SET_SPEED_ACK
PAUSE
PAUSE_ACK
RESUME
RESUME_ACK
RESET
RESET_ACK
PING
PONG
SHUTDOWN
SHUTDOWN_ACK
ERROR
HOOK_STATE_CHANGED
```

`SHUTDOWN` 在 V1 表示逻辑停用，不表示卸载 DLL。

HELLO 载荷至少包含：

```text
controller build ID
DLL build ID
processId + creationTime
machineType
targetGeneration
一次性会话令牌证明
支持的 protocol range
Hook 状态
```

STATUS / ACK 至少包含：

```text
configuredSpeed
paused
effectiveSpeed
clockRevision
Hook 状态
lastAppliedCommandSequence
故障安全恢复是否曾发生
targetRuntime
timeBackend
effectState
```

协议 1.1 的 STATUS/ACK 载荷保持 40 字节：偏移 `35`、`36`、`37` 分别编码目标运行时、实际控制后端和效果状态，偏移 `38..39` 仍必须为零。Unity Mono 命令可先以 `pending` 返回；只有 Unity 主线程实际写入对应 revision 后，`lastAppliedCommandSequence` 才前进并返回 `applied`。通用 Win32 后端返回 `unverified`，表示 IPC 与 Hook 已同步，但不宣称目标行为已经改变。

### 11.4 命令规则

- `SET_SPEED` 载荷是 IEEE-754 float64；必须 finite 且位于 `[0.05, 8.0]`。
- `SET_SPEED` 在暂停时只改变恢复倍率。
- `PAUSE` 和 `RESUME` 是幂等命令，但仍返回新旧状态和 revision。
- `RESET` 设置 `1.0×` 且解除暂停。
- 旧 command sequence 返回 `STALE_SEQUENCE`，不得应用。
- DLL 只有一个命令 writer，按接受的 command sequence 串行执行。
- ACK 在期望状态原子发布后发送；Unity Mono 后端以 `effectState` 区分等待主线程和真实生效。

## 12. 高频命令背压与顺序

拖动时不能让每个 pointer event 变成 IPC 队列项。

### 12.1 前端节流

- 指针视觉位置本地逐帧更新。
- 每个 `requestAnimationFrame` 最多发送一个 `setSpeed`。
- 同一帧内只保留最后值。
- pointerup 后必须保证最终 desired value 至少被发送一次。

### 12.2 Controller 合并

每个目标会话最多保留：

```text
1 个正在等待 ACK 的 SET_SPEED
1 个尚未发送的最新 SET_SPEED
```

当新倍率到达：

- 若没有在途命令，立即发送。
- 若已有在途命令，覆盖唯一 pending slot。
- ACK 到达后，只发送 pending slot 的最新值。
- 不建立无界 FIFO。

`PAUSE`、`RESUME`、`RESET` 和 `SHUTDOWN` 是不可丢弃的状态屏障：

- 它们不被 SET_SPEED 合并掉。
- `RESET` 和 `SHUTDOWN` 清除更早的 pending SET_SPEED。
- 所有屏障获得独立 command sequence。
- DLL 严格按序处理，因此快速拖动和暂停不会形成状态反转。

超时不等于成功，也不立即等于命令失败。Controller 将会话标为 `stalled`，停止继续堆积，并用 `GET_STATUS` 对账；如果传输已确认断开，DLL 已在本地恢复 `1.0×` 且解除暂停，UI 的 applied 状态应变为 unknown，直到新会话返回权威 STATUS。

## 13. 错误码草案

### 13.1 参数与协议

```text
INVALID_MESSAGE
INVALID_SPEED
PAYLOAD_TOO_LARGE
UNSUPPORTED_PROTOCOL
AUTHENTICATION_FAILED
SESSION_MISMATCH
TARGET_GENERATION_MISMATCH
STALE_SEQUENCE
TIMEOUT
PIPE_DISCONNECTED
```

### 13.2 目标与安全

```text
TARGET_NOT_FOUND
TARGET_IDENTITY_CHANGED
TARGET_EXITED
ARCH_UNSUPPORTED
ACCESS_DENIED
INTEGRITY_MISMATCH
PROTECTED_PROCESS
CRITICAL_PROCESS
APPCONTAINER_UNSUPPORTED
MITIGATION_POLICY_UNSUPPORTED
RISK_MODULE_DETECTED
RISK_CHECK_INCOMPLETE
```

### 13.3 加载与 Hook

```text
DLL_NOT_TRUSTED
DLL_ARCH_MISMATCH
DLL_BUILD_MISMATCH
DLL_LOAD_FAILED
DLL_INITIALIZE_FAILED
ALREADY_ATTACHED
EXISTING_MODULE_INCOMPATIBLE
HOOK_CREATE_FAILED
HOOK_ENABLE_FAILED
HOOK_ROLLBACK_FAILED
HOOK_STATE_INVALID
```

### 13.4 生命周期

```text
NOT_INITIALIZED
ALREADY_DEACTIVATED
CONTROLLER_RESTARTED
SHUTDOWN_INCOMPLETE
INTERNAL_ERROR
```

错误响应必须同时包含稳定错误码和面向 UI 的简短上下文，但不能泄露令牌、远程地址或不必要的完整文件路径。

## 14. 特殊场景

### 14.1 目标退出

- Controller 进程监视器先于普通轮询处理目标退出。
- 取消 pending 命令。
- UI 进入 `target.exited`。
- `appliedSpeed` / `appliedMode` 变为 unknown，不显示恢复成功的伪确认。
- Controller 不尝试重新注入同一 PID，因为 PID 可能被重用。

### 14.2 重复注入

- Injector 不按模块名称预扫描或推断重复连接。
- 标准 `LoadLibraryW` 对已加载 DLL 使用 Windows 正常引用计数。
- Hook 初始化导出是重复状态的权威来源；同一活跃会话返回 `ALREADY_ATTACHED`。
- 不覆盖、不卸载未知模块，也不使用 manual map 或隐藏加载。

### 14.3 Controller 重启

- 旧 Controller 一旦退出，DLL 通过管道或进程句柄确认断开后立即恢复 `1.0×` 并解除暂停。
- 新 Controller 不自动重放旧倍率、暂停状态或 pending 命令。
- 用户重新选择该目标后仍走只读预检与自动标准加载流程；若 Hook 已初始化，由其返回 `ALREADY_ATTACHED`。
- 新会话使用新的 session ID、target generation、管道名和令牌。
- 成功建立新会话后先读取权威状态，不自动重放旧 UI 命令。

### 14.4 Controller 正常关闭

- Controller 先发送 `RESET` 或 `SHUTDOWN`。
- DLL 连续重锚到 `1.0×` 并解除暂停。
- DLL ACK 后逻辑停用当前会话。
- 如果 ACK 前连接中断，断连故障安全路径产生相同本地结果。
- Controller 关闭不能依赖 UI 页面仍然存活。

### 14.5 DLL 暂时无响应

- UI 显示 desired 和最后已知 applied，不宣称成功。
- Controller 不建立无界命令队列。
- 状态超时后尝试有限次 `GET_STATUS` 对账。
- 一旦连接被确认失效，按断连策略处理；恢复后的状态在没有新会话 ACK 前仍显示 unknown。

## 15. 实现时必须保持的不变量

1. 只有用户明确选择目标后才能触发加载。
2. 对目标的每次写操作前都核验 `PID + 创建时间`。
3. 安全检查失败或不完整时不加载。
4. UI 的 `appliedSpeed` 只来自 DLL 权威 ACK/STATUS。
5. 所有 VirtualClock 写操作串行。
6. 四个时间 API 来源于同一个 virtual QPC timeline。
7. 倍率和暂停切换连续、不倒退。
8. QPC Hook 热路径无普通锁、堆分配、日志和 IPC。
9. 管道断连一经确认，DLL 立即恢复 `1.0×` 并解除暂停。
10. 断连后不保留旧倍率，不自动恢复旧状态。
11. V1 不热卸载 DLL；逻辑停用后驻留至目标退出。
12. Hook 安装只有“全部成功”或“完整回滚”两种公开结果。
13. Controller 重启产生全新 session，不复用旧序号或令牌。
14. 任何重复注入尝试都不得加载第二份 Hook DLL。

## 16. 后续实现顺序

本文不授权跳过阶段直接完成注入。建议实现顺序：

1. 固化协议枚举、错误码、build ID 和序列化测试向量。
2. 独立实现不依赖 Hook 的 VirtualClock 与 fake-QPC 单元测试。
3. 实现 TestApp32/64。
4. 在测试进程内验证四 API Hook、完整回滚和统一时间线。
5. 实现命名管道、会话认证、ACK、背压和断连本地恢复。
6. 实现 SafetyPolicy 与 Injector32/64，仅先允许自有 TestApp。
7. 实现 x64 Controller 与 WebView2 严格消息桥。
8. 接入 React 状态机和 SVG 旋钮。
9. 完成故障注入、并发、目标退出、Controller 重启和重复注入测试。
10. 修订 SVG 表盘几何和布局：细线完整双外环/内环、四向连接桥、倍率不重叠、倍率分针与 `1/12` 机械联动时针独立。
11. 移除 TestApp 名称门禁，使用完整安全证据支持用户明确选择的普通离线 x86/x64 桌面目标；加入用户确认的标准 UAC 权限匹配流程与本地结构化诊断。
12. 使用 TestApp 与自有/授权普通离线测试程序完成产品兼容性矩阵、干净 Win10/11 VM、签名和发布。

所有未实际构建或测试的行为，在后续文档和提交说明中必须继续标为“设计”或“待验证”，不能写成已经完成。
