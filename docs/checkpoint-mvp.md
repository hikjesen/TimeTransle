# 通用即时存读档 MVP

> 当前为待用户验收、待用户审阅的原型。已有结果仅为代理自测记录。

从 `out/checkpoint-mvp/TimeTransleController.exe` 启动，在“选择”面板点“启动程序…”，选择同目录 `TimeTransleCheckpointFixture32.exe`。从暂停创建的进程完成注入和握手后才运行程序；启动失败会结束尚未运行的目标并显示错误。

工具栏为暂停、1×、保存、读取、选择。反引号 + F5 保存，反引号 + F9 读取。单槽只在同一进程内有效；选择目标、退出、断连及 D3D Reset 清空槽。操作时倍率和重复存读档不可用。常规后附加仅支持原时间控制。

测试程序：N 切换 A/B 关，方向键移动，空格加分，Esc 退出。蓝色棋盘/A 与橙色棋盘/B 来自同一块被改写的纹理；绿色与粉色小方块使用各关卡独立创建的资源。标题和窗口下方显示堆状态、静态帧数、主线程栈帧数、随机状态、实际声音播放游标及 PCM 第 2 个采样值（A=100，B=226），用于观察真实资源内容的恢复，不使用哈希。合成音频不读取文件。

运行模型限 Windows x86、D3D9、DirectSound，单逻辑与渲染线程。Hook 使用私有堆保存检查点和登记表，应用主模块 HeapAlloc/ReAlloc/Free/Size 经 arena，预留 256 MiB、按需提交；复制已用 arena、主模块可写区段、x86 上下文与有效栈/SEH。Present 停驻后由 IPC 线程处理内存；图形和音频 API 在渲染线程运行。读档后跳过旧的已渲染画面，在第一张重新绘制的场景成功 Present 后确认完成。

纹理首版支持托管/系统内存二维 A8R8G8B8、X8R8G8B8 及可读写顶点/索引缓冲区。保存内容与设备 StateBlock，保留被存档引用的对象；同一对象地址原地恢复。声音保存 PCM、游标、音量、声像、频率与播放/循环状态。允许设备输出缓冲产生短暂延迟。

读档恢复虚拟 QPC，重新锚定当前真实 QPC，保留读取前倍率与暂停；revision 和 IPC 序号递增。内存与资源不传过小消息管道。普通文件、DLL 私有分配、外部服务、持久化、后附加检查点、x64、多应用线程和真实游戏兼容性不属于本轮成果。

故障仅报告具体失败阶段；未建设通用失败事务。前端错误详情沿用原状态入口。源码来源与许可状态见 `third_party/checkpoint/NOTICE.md` 和根目录 `agent.md`。本轮验收结果另见 `checkpoint-acceptance.md`。

构建：先在 `ui/` 执行 `npm.cmd run build`，配置 `windows-x86` 与 `windows-x64` CMake preset，然后分别构建 Release。x86 所需目标为 `timetransle_hook timetransle_injector timetransle_checkpoint_fixture`，x64 为 `timetransle_controller timetransle_hook timetransle_injector`。最后运行 `scripts/package-checkpoint.ps1` 只收集文件，不触发构建、测试或哈希步骤。`out/` 为本机交付目录，不纳入源码 Git 版本管理；项目规则与源码、文档一起进入用户明确要求的新公开仓库。
