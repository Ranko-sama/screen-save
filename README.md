# Screen Save

为当前 Xiaomi Book Pro 14 编译的离线黑屏工具。运行 `dist/ScreenSave.exe`，仅显示一个托盘图标。

按 **Ctrl+Alt+B** 一键黑屏／最暗，再按一次恢复。没有分档调节、渐变或动画，也不再使用 Ctrl+Alt+PageDown / PageUp。

托盘图标随调光状态切换：左侧白色圆钮表示关闭调光、正常亮屏；右侧黑色圆钮表示开启调光、黑屏／最暗。图标已嵌入 EXE，源 SVG 与多尺寸 ICO 位于 `assets/tray/`。

托盘菜单包含“黑屏／最暗／恢复”“调光方式”和“退出”。在“调光方式”中选择：

- **颜色矩阵（可全黑）**：默认模式，直接将颜色输出设为黑色，并隐藏系统鼠标光标，保持系统亮度不变。恢复、切换到 Gamma 或正常退出时重新显示光标；鼠标仍可移动和点击。
- **Gamma（不能全黑）**：系统亮度降至最低，同时将本次暗屏前的 Gamma 表缩放为 0.5；恢复时还原 Gamma 和系统亮度。该系数已在本机验证，不是实体屏幕亮度百分比。

两种方式共用一个开启／关闭状态、快捷键和保活逻辑，不创建遮罩窗口。暗屏时可以切换方式：进入 Gamma 时系统亮度降至最低，切回颜色矩阵时还原本次黑屏前的系统亮度，保持黑屏；两种颜色效果不叠加。选择仅本次运行有效，下次启动仍为颜色矩阵。

暗屏期间阻止自动息屏和睡眠，恢复后释放保活请求。每次进入暗屏时保存恢复状态，通过“退出”结束时也会还原；若正在使用 Gamma 暗屏，同时还原系统亮度。未开启暗屏时切换模式不会改变显示。

收到 Windows 确认注销、关机或重启的会话结束通知时，在通知返回前执行同一恢复逻辑；取消结束会话则保持当前状态。

重复启动时，新进程直接退出；Ctrl+Alt+B 已被其他程序占用时，本程序也不启动。

程序不联网，没有配置文件、日志、设置界面或自动启动。只需分发 `dist/ScreenSave.exe`；`tests/`、`build/` 和本地测试页面属于开发工具。

程序自身没有文本输入框，因此关闭本线程的输入法支持，避免加载部分输入组件；其他软件的中文输入不受影响。

## 维护状态

功能开发已完成，自 2026-10-08 起进入维护阶段，保留现有双模式一键黑屏／恢复功能。

维护范围为已确认的缺陷、维持本机运行所必需的调整和文档修正。除非用户提出新需求，不新增功能，不主动开展重构或无明确依据的性能优化。

修复遵循最小改动原则，只执行与改动相关的必要回归，优先使用静态检查和隔离测试。涉及实体屏幕时保持低频，不自动反复切屏或做压力测试。已有的光标并发风险继续作为非阻断记录，仅在出现实际症状或找到有明确依据的简单方案时重新评估。

## 编译

使用本机 Visual Studio 2022 Build Tools 和 Windows SDK，在项目目录运行 `build.cmd`。生成 x64 EXE，复用 Windows 10／11 自带的 UCRT，静态链接编译器支持与启动代码；无需额外安装 VC++ 运行库。当前含图标资源的体积为 34 KiB，硬件适配范围仍为本机。

硬件亮度通过系统 `advapi32.dll` 的原生 WMI 导出调用，减少本进程中 COM/WMI 对象的开销。这条调用路径未列入当前 SDK 的公开接口，仅在本机验证；系统 DLL 自带并不意味着其他电脑或未来系统版本必然兼容。

## 验证

最新的真实 Computer Use / 内置 Browser Use 复现尝试见 [tests/REAL-CURSOR-RESULTS.md](tests/REAL-CURSOR-RESULTS.md)：两轮未复现并发旧坐标覆盖或误点击，但工具实际将拖动与恢复排成先后顺序，未命中竞争窗口；另观察到恢复时各轴增加 1 像素，原因未定位。

会话结束恢复修复、回归和消融结果见 [tests/RECOVERY-FIX.md](tests/RECOVERY-FIX.md)。颜色矩阵恢复光标使用“读取并回写当前位置”：本机静止光标能立即恢复；特定并发交错可能覆盖新位置，但尚未观察到本机 agent 因此回跳或误点击。两个无坐标回写候选均未通过实体光标测试，当前保留有效实现并记录该条件性风险。

最新结构消融的三项精简、编译对照和收益边界见 [tests/STRUCTURE-ABLATION.md](tests/STRUCTURE-ABLATION.md)。本轮没有自动反复切换屏幕。

私有内存组成、输入组件优化与低频验证见 [tests/MEMORY-ATTRIBUTION.md](tests/MEMORY-ATTRIBUTION.md)。

本轮内存优化的保留项、舍弃项、手动测试与测量限制见 [tests/MEMORY-OPTIMIZATION.md](tests/MEMORY-OPTIMIZATION.md)。

此前复用系统 UCRT 的体积、依赖和功能回归见 [tests/SYSTEM-UCRT-RESULTS.md](tests/SYSTEM-UCRT-RESULTS.md)。

当前一键黑屏版本的功能与恢复流程测试见 [tests/TOGGLE-RESULTS.md](tests/TOGGLE-RESULTS.md)。

独立对抗审查、代码简化与消融实验见 [tests/REVIEW-ABLATION.md](tests/REVIEW-ABLATION.md)。

旧版颜色矩阵模式的实测见 [tests/RESULTS.md](tests/RESULTS.md)。`3e0fca3` 的 Gamma 与双模式验证记录见 [tests/GAMMA-RESULTS.md](tests/GAMMA-RESULTS.md)。原生亮度键研究已放弃，历史实验与结果见 [tests/NATIVE-KEY-RESULTS.md](tests/NATIVE-KEY-RESULTS.md)，实验代码不参与正式程序编译。
