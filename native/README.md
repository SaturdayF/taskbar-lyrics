# Taskbar Lyrics Native · 0.2.0 原型

这是与原 C# 版本并存的 C++ 原型，尚未完整迁移原版功能。

## 使用

目标系统：Windows 10 2004（19041）及以上、Windows 11，x64。
直接运行 `TaskbarLyrics.Native.exe`，无需 .NET、WebView2 或单独安装 VC++ 运行库。
安装版为当前用户安装，无需管理员权限，可通过 Windows“已安装的应用”卸载。

支持识别 QQ 音乐、网易云音乐、Apple Music/iTunes 的系统媒体会话。
播放器必须提供 SMTC 信息；真实客户端的端到端兼容性仍待逐一验收。
歌词默认在当前屏幕任务栏的空闲区域显示，背景透明。按住歌词左侧的拖动手柄可左右移动，歌词会避开检测到的按钮及通知区域。
空间不足、任务栏隐藏或检测结果过期时隐藏；右键托盘可关闭“在任务栏空闲区域显示”，切换为悬浮窗口。
采用任务栏区域内置顶窗口，不向 Explorer 注入代码或修改系统任务栏布局。全屏应用前暂时隐藏任务栏歌词。

- 默认向 LRCLIB 发送歌名、歌手、专辑和时长。精确匹配失败后搜索候选，优先使用同专辑结果，仍校验歌名、歌手及可用时长。
- 右键歌词或托盘图标，可对当前播放器执行上一首、播放或暂停、下一首。菜单根据播放器报告的可用操作启用。
- 右键菜单可复制当前同步歌词，前奏或空白间奏不复制占位文本。
- 单曲与全局时间校准各支持 ±10 秒，叠加生效；正值使歌词提前出现，不改变播放进度。`Ctrl + 滚轮` 调整单曲，`Ctrl + Shift + 滚轮` 调整全局，每格 0.5 秒；菜单可重置。
- 可选暂停或无播放时隐藏，播放恢复后自动显示；可选鼠标穿透，开启后从托盘菜单取消以恢复拖动、滚轮和右键。
- 时间校准、暂停隐藏及鼠标穿透保存到 `%LOCALAPPDATA%\TaskbarLyrics\Native\settings.json`；单曲以播放器、歌名、歌手和专辑区分。演示模式不读取或保存正式设置。
- LRCLIB 失败后，回退网易公开歌曲/歌词接口（发送歌名、歌手及匹配歌曲 ID）。约 30 秒后重试，菜单也可手动重试。来源收录及网络状态仍会影响可用性。
- 菜单可关闭在线匹配；`--offline` 在启动时禁止在线匹配。已发出的请求无法撤回。
- 菜单可导入当前歌曲的 UTF-8 LRC（最大 2 MB）；仅当前会话有效，切歌或退出后不保存。
- `--demo --offline` 显示明确标注为模拟数据的演示。
- 菜单可查看当前歌曲和匹配状态、重新获取、恢复居中、退出。没有自动启动项。
- `--floating` 启动时使用悬浮模式；`--demo --offline --floating` 可在没有任务栏空间时查看演示。

尚未迁移：原版设置页、QQ 歌词源、持久化歌词缓存、在线及停靠开关的保存、每屏位置、同时在全部屏幕显示。
原型采用 GDI 系统字体绘制，以验证媒体、歌词和部署路径；最终排版可迁移至 DirectWrite。
媒体轮询、在线匹配和任务栏读取分别运行。切歌会丢弃旧歌的请求结果，但同步网络请求尚不能立即取消，下一首的歌词获取可能等待旧请求超时。
多屏混合 DPI、自动隐藏和不同系统版本的任务栏仍需进一步实机验证。

## 构建与检查

开发机需要 Visual Studio 2022 C++ 桌面工具和 Windows SDK；用户电脑不需要。

```powershell
.\scripts\build-native.ps1
# 安装版另需 Inno Setup 6.3+ 或 7，工具不随应用分发：
.\scripts\build-native.ps1 -Iscc 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'

# 仅输出会话数量，不记录当前歌名：
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --probe .\codex_tmp\native-media-probe.txt
# 显式请求公开测试歌曲的 LRCLIB 歌词，仅记录行数，不保存歌词正文：
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --network-check .\codex_tmp\native-network-check.txt
# 用户反馈曲目：Super Star / S.H.E，196 秒录音，仅记录行数和来源：
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --superstar-check .\codex_tmp\superstar-check.txt
# 当前歌曲诊断，会记录当前歌曲元数据，不保存歌词正文：
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --current-check .\codex_tmp\current-check.txt
# 任务栏内位置、图标避让、窗口响应和退出检查：
.\scripts\check-native.ps1 -Docked
# 使用离线演示验证鼠标穿透、透明标志、暂停隐藏及恢复，不控制真实播放器：
.\scripts\check-native.ps1 -Docked -Interactions
# Unicode 单曲校准与设置文件读写检查，不改动正式设置：
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --preferences-check .\codex_tmp\preferences-check.txt
```

输出：`artifacts/native-win-x64/TaskbarLyrics.Native.exe`、便携 ZIP，以及启用 Inno Setup 时的 `artifacts/*setup-x64.exe`。
诊断文件父目录必须已存在。构建执行 LRC 边界检查，并在 `artifacts/native-build/dependencies.txt` 记录 EXE 的直接 DLL 依赖。

参考：[SMTC API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager)、[LRCLIB API](https://lrclib.net/docs)、[C++ 静态部署及更新取舍](https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp)。
