# Screen Save

轻量的 Windows 托盘调光工具，按快捷键即可让屏幕变暗或全黑，同时保持电脑继续运行。离线使用，无需安装。

**适用范围：Windows 10／11 x64，目前仅针对作者的 Xiaomi Book Pro 14 适配和验证，其他机型尚未验证。**

## 使用方法

1. 从 [Releases](https://github.com/Ranko-sama/screen-save/releases/latest) 下载 Windows x64 压缩包，解压后运行 `ScreenSave.exe`。源码仓库中的程序位于 `dist/ScreenSave.exe`。
2. 按 **Ctrl+Alt+B** 黑屏／最暗，再按一次恢复。
3. 点击右下角系统托盘图标，可切换调光方式、恢复显示或选择“退出”。退出时会恢复显示。

| 调光方式 | 效果 |
| --- | --- |
| 颜色矩阵（默认） | 画面全黑并隐藏鼠标光标，保留系统亮度。 |
| Gamma | 将系统亮度降至最低并进一步压暗画面，不能全黑。 |

托盘图标左侧白色圆钮表示正常亮屏，右侧黑色圆钮表示正在调光。暗屏期间阻止自动息屏和睡眠，恢复后释放该请求。调光方式仅本次运行有效。

重复启动或快捷键被其他程序占用时，新进程会直接退出。

## 编译方式

安装 **Visual Studio 2022 Build Tools**，勾选“使用 C++ 的桌面开发”和 **Windows SDK**。在项目根目录运行：

```bat
build.cmd
```

生成文件为 `dist/ScreenSave.exe`（x64），图标已内嵌，无需额外安装 VC++ 运行库。

## 许可证

Copyright 2026 Ranko-sama。项目采用 [Apache-2.0](LICENSE) 许可证。

项目已进入维护阶段，维护约定及历史验证记录见[维护文档](https://github.com/Ranko-sama/screen-save/blob/main/docs/MAINTENANCE.md)。
