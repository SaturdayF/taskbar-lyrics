# Taskbar Lyrics Native · 0.1.0 原型

这是与原 C# 版本并存的 C++ 原型，尚未完整迁移原版功能。

## 使用

目标系统：Windows 10 2004（19041）及以上、Windows 11，x64。
直接运行 `TaskbarLyrics.Native.exe`，无需 .NET、WebView2 或单独安装 VC++ 运行库。
安装版为当前用户安装，无需管理员权限，可通过 Windows“已安装的应用”卸载。

支持识别 QQ 音乐、网易云音乐、Apple Music/iTunes 的系统媒体会话。
播放器必须提供 SMTC 信息；真实客户端的端到端兼容性仍待逐一验收。
歌词显示在任务栏上方的半透明悬浮窗口，可拖动。右键窗口或托盘打开菜单。

- 默认在线匹配会向 `https://lrclib.net` 发送歌名、歌手、专辑和时长；失败后约两分钟重试。
- 菜单可关闭在线匹配；`--offline` 在启动时禁止在线匹配。已发出的请求无法撤回。
- 菜单可导入当前歌曲的 UTF-8 LRC（最大 2 MB）；仅当前会话有效，切歌或退出后不保存。
- `--demo --offline` 显示明确标注为模拟数据的演示。
- 菜单可恢复居中、退出。没有自动启动项。

尚未迁移：原版设置页、QQ/网易歌词源、持久化缓存/设置、时间偏移、每屏位置、任务栏内部自动避让、全屏/自动隐藏策略。
原型采用 GDI 系统字体绘制，以验证媒体、歌词和部署路径；最终排版可迁移至 DirectWrite。
在线请求在后台线程进行，但原型的媒体轮询与请求共用线程，网络较慢时切歌识别可能延迟；发布前需拆分并增加取消机制。

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
```

输出：`artifacts/native-win-x64/TaskbarLyrics.Native.exe`、便携 ZIP，以及启用 Inno Setup 时的 `artifacts/*setup-x64.exe`。
诊断文件父目录必须已存在。构建执行 LRC 边界检查，并在 `artifacts/native-build/dependencies.txt` 记录 EXE 的直接 DLL 依赖。

参考：[SMTC API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager)、[LRCLIB API](https://lrclib.net/docs)、[C++ 静态部署及更新取舍](https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp)。
