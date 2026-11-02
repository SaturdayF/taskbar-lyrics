<p align="center"><img src="assets/app.png" width="100" alt="声波横栏图标" /></p>

# Taskbar Lyrics · 任务栏歌词

Windows 10/11 的歌词伴随工具，把当前播放歌曲的同步歌词显示在任务栏区域。当前 v0.2.1，C# / .NET 8 / WPF。

## v0.2.1 更新

采用选定的 B「声波横栏」图标，应用于 EXE、Windows 窗口、系统托盘和设置页品牌区。PNG 原图和包含 16–256 像素九种尺寸的 Windows ICO 位于 `assets`，构建后的程序内嵌图标资源，无需外置文件。

## v0.2.0 更新

- 设置界面参考 Codex 的深色侧栏、克制的灰阶和圆角控件，分为“正在播放”“外观与位置”“歌词与来源”；支持实时预览和自动保存。
- 歌词栏默认水平居中，垂直位置始终固定在主屏任务栏中心。拖动仅改变水平偏移，上下移动鼠标不会改变高度。
- 水平偏移按任务栏中心保存，重启后恢复；“恢复任务栏居中”一键回到中央，锁定后禁止拖动。
- 暂不支持竖直任务栏，遇到竖直或高度不足的任务栏时隐藏歌词栏，不产生自由悬浮窗口。

## 播放器与歌词来源

| 播放器 | 歌曲及进度 | 歌词来源 |
| --- | --- | --- |
| QQ 音乐 Windows 客户端 | Windows SMTC 媒体会话 | QQ 歌曲搜索与 LRC 接口，失败回退 LRCLIB；支持导入 LRC |
| 网易云音乐 Windows 客户端 | Windows SMTC 媒体会话 | 网易歌曲搜索与 LRC 接口，失败回退 LRCLIB；支持导入 LRC |
| Apple Music Windows 客户端 / iTunes | Windows SMTC 媒体会话 | 按歌名、歌手、专辑、时长匹配 LRCLIB；支持导入 LRC |

**这不是直接读取三个客户端内部的歌词缓存。** 系统媒体会话提供歌曲信息和播放时间，不提供歌词文本。Apple Music 首版不会提取官方歌词，显示的是外部匹配的歌词。QQ／网易使用的公开可访问接口不是稳定的开发者 API，可能受版本、地区或服务端策略影响。找不到歌词时显示歌名，可手动导入合法取得的 LRC；不生成虚假歌词。

客户端必须开启“系统媒体控制 / SMTC / 与系统媒体控件集成”（名称随版本变化）。旧版本、未提供媒体会话的客户端、网页播放器和其他设备上的播放不在首版支持范围内。此为初始实现，三家真实客户端的播放、暂停、切歌、拖动进度仍需逐一实机验收，不代表已完成全版本兼容验证。

## 运行

要求 Windows 10 2004（19041）及以上，推荐 Windows 11 x64。`scripts/build.ps1 -Publish` 生成无需另装 .NET 的便携版。

1. 解压便携包并打开 `TaskbarLyrics.App.exe`。
2. 在支持的播放器中播放音乐；自动模式优先选择正在播放的会话。
3. 歌词默认出现在主屏任务栏中央。拖动歌词仅调整横向位置，纵向始终固定在任务栏中心；双击或右键进入设置。
4. 设置支持选择播放器、调整字号、±10 秒时间偏移、锁定位置、关闭在线匹配、导入当前歌曲的 LRC 和重新匹配。
5. 关闭设置后程序继续运行，从系统托盘菜单或歌词右键菜单退出。

任务栏显示使用透明置顶窗口，未向 Explorer 注入代码，不会为任务栏按钮自动预留空间；如果居中位置挡住任务栏按钮，可向左右调整。当前适配主屏水平任务栏；自动隐藏、全屏应用和替代任务栏尚需专项适配，竖直任务栏暂不显示。可以从托盘“恢复任务栏居中”恢复位置。

`TaskbarLyrics.App.exe --demo` 显示明确标注的模拟歌词，用于查看界面。`--probe` 把本机 SMTC 会话写入 `%LOCALAPPDATA%\TaskbarLyrics\sessions.txt`，用于核对客户端是否提供会话；该诊断文件可能含当前歌名。

## 歌词同步及数据

- 行级 LRC 同步，支持多时间戳、1–3 位小数和 `[offset:]`；暂停时停止推进，切歌取消旧请求，拖动进度后重新定位。
- 按歌名、歌手和时长严格匹配，避免把翻唱或不同版本当作同一录音。严格匹配可能漏掉合作歌手、别名或不同标点的专辑版本，可导入 LRC。
- 本地 LRC 优先于网络，按播放器和录音信息保存；在线成功结果缓存 7 天，失败最多每 2 分钟自动重试，也可手动重试。
- 在线匹配会发送歌名、歌手、专辑和时长到相应平台及 LRCLIB。没有登录、Cookie 读取、遥测或音频上传。设置可关闭在线请求。
- 设置和缓存位于 `%LOCALAPPDATA%\TaskbarLyrics`；导入文件建议 UTF-8。不保存音乐文件，不下载音频。歌词权利归其权利人所有。

## 开发与验证

安装 .NET 8 SDK，在 PowerShell 运行：

```powershell
./scripts/build.ps1
./scripts/build.ps1 -Publish
dotnet run --project src/TaskbarLyrics.App -- --demo
```

`tests/TaskbarLyrics.Tests` 是不依赖测试框架的控制台验证，覆盖 LRC 边界、时间偏移、前后跳转、匹配保护和本地导入。CI 在 Windows 上运行验证、构建并输出便携 ZIP。

开发时可用 `TaskbarLyrics.App.exe --render-previews <输出目录>` 渲染三页设置界面和紧凑窗口的 PNG；截图包含明确标注的模拟歌曲，不读取用户播放数据。位置验证覆盖居中、只沿水平方向移动、屏幕边界和显示尺寸变化。

目录：`src/TaskbarLyrics.Core` 为歌词解析、匹配和网络适配；`src/TaskbarLyrics.App` 为 SMTC、任务栏窗口、设置和托盘；`scripts` 为构建脚本；`tests` 为核心验证。

参考：[Windows 媒体会话 API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession)、[LRCLIB API](https://lrclib.net/docs)、[QQ 接口行为参考](https://github.com/jsososo/QQMusicApi)。
