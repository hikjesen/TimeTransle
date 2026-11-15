# TimeTransle

Windows 本地程序时间控制器，以及通用即时存读档的原生 MVP。

> **当前状态：待用户验收、待用户审阅的技术原型。** 此前记录的“通过”均为代理侧自测结果，不代表用户已经接受，也不代表真实游戏兼容性已经确认。本仓库按用户要求公开，供审阅当前实现。

## 功能范围

- 对用户选择的 x86/WOW64 或 x64 程序提供 Win32 时间倍率控制：`0.05×–8.0×` 和暂停。
- Controller 使用 Win32、WebView2 和 React，原生组件包括 Injector32/64、Hook32/64、命名管道与虚拟时钟。
- 检查点原型限 **Windows x86、D3D9、DirectSound、单逻辑与渲染线程**，从 Controller 启动目标并在应用入口运行前接管。
- 一个同进程内存槽，支持跨关卡读取；恢复内存、执行上下文、图形资源内容与声音状态，保留读取前的倍率和暂停设置。

后附加检查点、x64 存档、其他图形接口、多应用线程、文件回滚及退出后的持久读取不属于本轮实现范围。东方永夜抄、Deltarune、以撒的结合尚未验证。

## 使用原型

完成构建和文件收集后，运行 `out/checkpoint-mvp/TimeTransleController.exe`：

1. 点“选择 → 启动程序…”，选择同目录 `TimeTransleCheckpointFixture32.exe`。
2. 在 A 关暂停并保存，继续运行；测试窗口使用英文输入，按 N 切到 B 关。
3. 设置当前倍率和暂停状态，通过按钮或快捷键读取，观察 A 关恢复。
4. 继续后可用方向键移动、空格加分。关闭目标窗口后槽位清除。

工具栏提供暂停、1×、保存、读取、选择。检查点仅在同一进程会话内有效。

| 快捷键 | 操作 |
| --- | --- |
| 反引号 + F5 | 保存检查点 |
| 反引号 + F9 | 读取检查点 |
| 反引号 + F1 / F2 / F3 | 开关切换 1× / 0.4× / 2.5×，记忆并恢复之前的倍率 |
| 反引号 + Esc | 显示或隐藏 Controller |

## 构建

需要 Windows、Visual Studio 2022 C++ Build Tools、MSVC x86/x64、Windows SDK、CMake 3.28+，以及 Node.js 24 / npm 11。运行 Controller 需要系统 WebView2 Runtime。

先构建前端，再配置原生工程：

```powershell
npm ci --prefix ui
npm run build --prefix ui

cmake --preset windows-x86
cmake --preset windows-x64

cmake --build --preset windows-x86-release --target timetransle_hook timetransle_injector timetransle_checkpoint_fixture
cmake --build --preset windows-x64-release --target timetransle_controller timetransle_hook timetransle_injector

powershell.exe -NoProfile -File scripts/package-checkpoint.ps1
```

交付目录为 `out/checkpoint-mvp/`。文件收集脚本不会自动构建或运行测试。`out/`、依赖安装目录、本机工作记录和构建产物不提交到仓库；当前未发布 GitHub Release 二进制包。

## 记录与协作

- [项目协作规则](agent.md)：MVP、优先复用、禁止哈希校验、禁止代理重复验收。
- [检查点范围与实现说明](docs/checkpoint-mvp.md)。
- [简短使用说明](docs/checkpoint-quickstart.md)。
- [代理自测记录；用户验收待进行](docs/checkpoint-acceptance.md)。
- [IPC 1.2](contracts/ipc-v1.md) 和 [WebView 命令](contracts/webview-v1.schema.json)。

GitHub Actions 仅支持手动触发；本次公开推送不重新运行已通过的代理测试。用户验收与审阅状态单独记录。旧阶段文档与最新要求冲突时，以根目录 `agent.md` 和当前检查点范围为准。

## 第三方来源

保留原有版权、许可和引用记录，见 [依赖声明](NOTICE-THIRD-PARTY.md) 与 [检查点模块来源](third_party/checkpoint/NOTICE.md)。其中两个源码参考项目未见明确许可，Hourglass 的 GPL v2 原文随源码保留。本仓库未对全部代码声明统一开源许可证。
