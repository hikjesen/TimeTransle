# TimeTransle 发布、签名与干净环境验证

> 状态：阶段 11 普通程序实现、UAC 成功路径与实体全局快捷键已通过；阶段 12 已生成本地未签名 `0.1.4` 候选包，正式发布仍等待兼容性矩阵、签名、UAC 取消路径与干净 VM 验收。

## 1. 便携包

在仓库根目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package.ps1 `
  -Configuration Release `
  -Version 0.1.4
```

脚本执行以下门禁：

- `npm ci`、ESLint、106 项 Vitest 与 Vite 生产构建。
- x64/x86 Release configure 与 build。
- 直接依赖锁和第三方 NOTICE 校验。
- 仅收集 Controller、双架构 Injector/Hook 与固定 UI 资源；不把 TestApp、测试工具或 PDB 放入用户包。
- 从锁定的原生源码与已安装 npm 生产依赖中收集精确许可证/NOTICE 文本。
- 为包内每个文件生成 `SHA256SUMS.txt`，并为 ZIP 生成独立 `.sha256`。
- 记录所有 EXE/DLL 的 Authenticode 状态。

Release 使用静态 MSVC 运行库。便携包不依赖单独安装 Visual C++ Redistributable；运行时仍需要系统 WebView2 Evergreen Runtime。

当前 `0.1.4` ZIP 只能标记为技术预览。它已通过 x64 16 项、x86 14 项 Release CTest、两条 x64 Controller→x86 目标 E2E、前端 106 项测试、包内 39 个文件的许可/清单检查、包内 Controller `--self-test` 返回 `0` 和两级 SHA-256 验证。TestApp 名称门禁已经移除，但不得仅凭包已生成、哈希已验证就宣称所有普通程序兼容。

## 2. Windows 图标

Controller 使用 `src/controller/resources/timetransle-icon.svg` 作为矢量主稿，并由 `scripts/generate-icon.ps1` 生成包含 `16/20/24/32/40/48/64/128/256` px 的 `timetransle.ico`。图标复用参考图的黑底暖光、双外环、四向连接桥、内环和直角双指针，并通过原生资源文件进入 EXE 的标题栏、任务栏和资源管理器显示。

重新生成图标：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\generate-icon.ps1
```

## 3. WebView2

Controller 检测不到 WebView2 Runtime 时会保持原生失败界面，并询问用户是否打开 Microsoft 官方 WebView2 下载页。它不会静默下载、安装或提升权限。

官方分发页：

- <https://developer.microsoft.com/microsoft-edge/webview2/>

## 4. Authenticode 签名策略

正式发布应在打包前使用受保护的代码签名证书依次签署：

1. `TimeTransleController.exe`
2. `TimeTransleInjector32.exe`
3. `TimeTransleInjector64.exe`
4. `TimeTransleHook32.dll`
5. `TimeTransleHook64.dll`

要求：

- SHA-256 文件摘要。
- RFC 3161 SHA-256 时间戳。
- 私钥、证书密码与硬件令牌配置不得进入仓库或 CI 日志。
- 签名后重新执行包内哈希与 ZIP 哈希生成。
- 发布流水线使用 `package.ps1 -RequireSigned`；任一签名不是 `Valid` 时拒绝产包。

当前本地构建没有发布证书，因此包中的 `SIGNATURE-STATUS.txt` 明确记录 `NotSigned`，不伪装为正式签名版本。

## 5. 干净 VM 验证矩阵

正式发布前，在独立、未安装开发工具的 VM 中执行：

| 环境 | WebView2 | 目标 | 必须验证 |
| --- | --- | --- | --- |
| Windows 10 x64 | 已安装 Evergreen | TestApp32/64 | 选择、同步、0.5×、2×、暂停、1×、关闭安全复位 |
| Windows 11 x64 | 已安装 Evergreen | TestApp32/64 | 同上，并检查 100%/150%/200% DPI |
| Windows 10 x64 | 已安装 Evergreen | 普通命名 x86 测试程序 | 最小硬门禁、选择后自动连接、0.5×、2×、暂停、1×、正常关闭与异常断连复位 |
| Windows 11 x64 | 已安装 Evergreen | 普通命名 x64 测试程序 | 同上，并检查 `320/360/440` 窗口与 100%/150%/200% DPI |
| Windows 10/11 x64 | 已安装 Evergreen | 管理员权限测试程序 | 普通 Controller 不写入；取消 UAC 保持未连接；新实例重新枚举、预检并在通过后自动连接 |
| Windows 10/11 x64 | 未安装 | 无 | 出现可执行的官方安装入口，不崩溃、不静默联网 |

每个环境还需确认：

- 从解压后的 ZIP 直接启动，不依赖源码目录、Node.js、Visual Studio 或 VC++ Redistributable。
- 默认以普通用户权限运行。只有原始结果为 `INTEGRITY_MISMATCH` 且用户明确确认后，才通过 Windows 标准 UAC 重启 Controller；不得启动即请求、循环请求或静默提升。
- 不根据名称、模块、联网或游戏类别推测兼容性；硬门禁与真实系统/Hook 结果保持 fail-closed。
- 结束 Controller、强制终止 Controller 与目标退出都恢复 `1.0×`、解除暂停。
- 选择普通目标后无需打开详情菜单或再次点击即可连接；`Insert+F10/F11/F12` 分别完成临时 `1×`、恢复先前倍率和显示聚焦窗口。
- 包内 `SHA256SUMS.txt` 与 ZIP `.sha256` 均匹配。

当前机器没有 Windows Sandbox 或可支配的干净 Win10/11 VM，且系统策略为 `ConsentPromptBehaviorAdmin=0`，不会显示可取消的 UAC 同意提示。高完整性成功路径以及 `Insert+F10/F11/F12` 的实体键盘路径已经人工通过；取消路径和外部兼容性矩阵仍需要独立环境人工验收。干净 VM 与签名是阶段 12 的最终门禁。
