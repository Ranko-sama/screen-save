# Screen Save

为当前 Xiaomi Book Pro 14 编译的离线黑屏工具。运行 `dist/ScreenSave.exe`，仅显示一个托盘图标。

按 **Ctrl+Alt+B** 一键黑屏／最暗，再按一次恢复。没有分档调节、渐变或动画，也不再使用 Ctrl+Alt+PageDown / PageUp。

托盘菜单包含“黑屏／最暗／恢复”“调光方式”和“退出”。在“调光方式”中选择：

- **颜色矩阵（可全黑）**：默认模式，直接将颜色输出设为黑色，保持系统亮度不变。
- **Gamma（不能全黑）**：系统亮度降至最低，同时将原始 Gamma 表缩放为 0.5；恢复时还原 Gamma 和本次黑屏前的系统亮度。0.5 是本机实测接受的最低线性系数，不是实体屏幕亮度百分比。

两种方式共用一个开启／关闭状态、快捷键和保活逻辑，不创建遮罩窗口。暗屏时可以切换方式：进入 Gamma 时系统亮度降至最低，切回颜色矩阵时还原本次黑屏前的系统亮度，保持黑屏；两种颜色效果不叠加。选择仅本次运行有效，下次启动仍为颜色矩阵。

暗屏期间阻止自动息屏和睡眠，恢复后释放保活请求。通过“退出”结束时恢复程序修改的原始颜色矩阵或 Gamma 表；若正在使用 Gamma 暗屏，也还原系统亮度。

程序不联网，没有配置文件、日志、设置界面或自动启动。只需分发 `dist/ScreenSave.exe`；`tests/`、`build/` 和本地测试页面属于开发工具。

## 编译

使用本机 Visual Studio 2022 Build Tools 和 Windows SDK，在项目目录运行 `build.cmd`。生成的 EXE 静态链接 C++ 运行库。

## 验证

当前一键黑屏版本的功能与恢复流程测试见 [tests/TOGGLE-RESULTS.md](tests/TOGGLE-RESULTS.md)。

旧版颜色矩阵模式的实测见 [tests/RESULTS.md](tests/RESULTS.md)。`3e0fca3` 的 Gamma 与双模式验证记录见 [tests/GAMMA-RESULTS.md](tests/GAMMA-RESULTS.md)。原生亮度键研究已放弃，历史实验与结果见 [tests/NATIVE-KEY-RESULTS.md](tests/NATIVE-KEY-RESULTS.md)，实验代码不参与正式程序编译。
