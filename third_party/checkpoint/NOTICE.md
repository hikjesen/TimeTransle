# 检查点模块来源

取得及修改日期：2026-11-15。源码参考最初以个人使用为前提；用户随后明确要求将当前项目推送至全新公开 GitHub 仓库供审阅。此发布范围变更不改写上游许可记录。两个未明确许可项目的源码参考得到用户允许，该允许不表述为上游授予的许可。

| 来源 | 上游路径 | 本项目采用与修改 |
| --- | --- | --- |
| [Crab1k / GTA Savestates](https://github.com/Crab1k/gta_savestates) | `src/arena.cpp`, `src/arena.h`, `src/d3d.cpp`, `src/d3d.h`, IAT hooks | 移植 TLSF 风格 arena、D3D9 资源登记及引用保留、设备状态获取；改为启动注入、256 MiB 按需提交和同进程单槽。另写受限格式的纹理/VB/IB 内容副本，失败直接返回错误。未采用游戏地址、专用线程识别、截图或持久化资源重建。未见明确许可。 |
| [TASEmulators / Hourglass](https://github.com/TASEmulators/hourglass-win32) | `src/wintaser/hooks/soundhooks.cpp`, 线程上下文存档实现 | 参考暂停后复制 x86 CONTEXT/栈以及 DirectSound PCM、播放位置和播放属性恢复；采用 TimeTransle 自有 Present 停驻点及真实缓冲区登记，未移植完整混音器。Copyright (C) 2011 nitsuja and contributors，GPL v2；原文见 `Hourglass-COPYING.txt`。 |
| [khang06 / pointlessdevice](https://github.com/khang06/pointlessdevice) | 主模块可写 PE 区段遍历思路 | 自动发现主 EXE 可写区段；不使用固定地址或游戏数据结构。未见明确许可。 |

TimeTransle 现有 MinHook、WebView2、nlohmann/json 及相关版权说明继续适用，见项目原第三方声明。这里不将未明确许可源码重新标注为宽松许可。
