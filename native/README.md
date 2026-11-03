# C++ 构建与诊断

应用使用 C++20、Win32、C++/WinRT、GDI 和 Windows UI Automation。运行说明及功能范围见 [主文档](../README.md)。

## 构建

在项目根目录运行 `./scripts/build.ps1`，或直接运行 `./scripts/build-native.ps1`。需要 Visual Studio 2022 C++ 桌面工具和 Windows SDK。脚本使用 x64 MSVC、静态运行库、`/W4 /WX`，构建后执行核心检查及设置读写检查。

输出包含可执行文件、README 和许可证。依赖检查写入 `artifacts/native-build/dependencies.txt`。安装包需传入 Inno Setup 的 `ISCC.exe` 路径。

## 诊断命令

下列命令在仓库根目录运行，输出目录需事先存在。

```powershell
New-Item -ItemType Directory -Force artifacts/diagnostics
# 仅记录媒体会话数量
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --probe .\artifacts\diagnostics\sessions.txt
# 查询公开样例 Hello / Adele，记录同步歌词行数和来源
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --network-check .\artifacts\diagnostics\network.txt
# 查询公开样例 Super Star / S.H.E
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --superstar-check .\artifacts\diagnostics\superstar.txt
# 记录真实当前歌曲元数据和匹配结果，不保存歌词正文
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --current-check .\artifacts\diagnostics\current.txt
# 单曲校准、Unicode 和设置文件往返保存，不改正式设置
.\artifacts\native-win-x64\TaskbarLyrics.Native.exe --preferences-check .\artifacts\diagnostics\preferences.txt
# 离线验证避让、透明标志、鼠标穿透、暂停隐藏和播放恢复
.\scripts\check-native.ps1 -Docked -Interactions
```

`--demo --offline` 不读取真实播放器，不写正式设置，可用右键菜单暂停或继续模拟进度。省略 `-Docked` 的窗口检查使用悬浮模式。实际双屏、不同 DPI 和音乐客户端仍需专项验收。
