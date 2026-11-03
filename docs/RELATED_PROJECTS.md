# 相关项目与功能对照

资料核对日期：2026-11-03。以下信息来自各项目公开 README，表示文档描述的能力，不代表已在本机逐一安装验收。

| 项目 | 文档列出的相关功能 | Taskbar Lyrics 的对应功能 |
| --- | --- | --- |
| [LyricBar](https://github.com/silora/LyricBar) | 停靠任务栏、无播放时隐藏、单曲及全局偏移、滚轮校准、复制当前歌词、曲目或歌手主题 | 暂停或无播放时隐藏、独立单曲校准、全局校准、修饰键配合滚轮、右键复制歌词 |
| [HotLyric](https://github.com/cnbluefire/HotLyric) | 为其他播放器提供歌词、锁定后不接收点击或拖动、托盘解锁、自定义样式、低功耗模式 | 独立鼠标穿透开关、托盘恢复交互 |
| [ANYNC/TaskbarLyrics](https://github.com/ANYNC/TaskbarLyrics) | 播放控制、多显示器、单曲校准、手动选择匹配结果、本地歌词、频谱 | 播放控制、单曲校准、本地 LRC 导入 |
| [mo-jinran/Taskbar-Lyrics](https://github.com/mo-jinran/Taskbar-Lyrics) | BetterNCM 插件，在任务栏中嵌入歌词，主要面向 Windows 11 原版任务栏 | 独立程序，通过 SMTC 获取媒体信息，在任务栏空闲区域显示透明歌词 |

本项目的校准、隐藏、鼠标穿透和复制逻辑为独立实现，未引入上述仓库的代码、资源或运行依赖。原版的透明背景、固定垂直位置、横向拖动和图标避让继续适用。

本项目使用 C++，通过托盘菜单设置，在窗口所在屏幕显示。完整功能范围见 [主说明](../README.md) 和 [构建与诊断](../native/README.md)。

设置界面的导航、设置行、浅深色配色及底部保存操作参考 [Codex Quota Bar](https://github.com/SaturdayF/codex-quota-bar) 的布局，在本项目中以独立的 Win32 / GDI+ 设置窗口实现。
