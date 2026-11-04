# 维护与验证记录

## 维护约定

功能开发已完成，自 2026-10-08 起进入维护阶段，保留现有双模式一键黑屏／恢复功能。

维护范围为已确认的缺陷、维持本机运行所必需的调整和文档修正。除非用户提出新需求，不新增功能，不主动开展重构或无明确依据的性能优化。

修复遵循最小改动原则，只执行与改动相关的必要回归，优先使用静态检查和隔离测试。涉及实体屏幕时保持低频，不自动反复切屏或做压力测试。已有的光标并发风险继续作为非阻断记录，仅在出现实际症状或找到有明确依据的简单方案时重新评估。

## 硬件接口范围

硬件亮度通过系统 `advapi32.dll` 的原生 WMI 导出调用，减少本进程中 COM/WMI 对象的开销。这条调用路径未列入当前 SDK 的公开接口，仅在本机验证；系统 DLL 自带并不意味着其他电脑或未来系统版本必然兼容。

## 历史验证

最新的真实 Computer Use / 内置 Browser Use 复现尝试见 [tests/REAL-CURSOR-RESULTS.md](../tests/REAL-CURSOR-RESULTS.md)：两轮未复现并发旧坐标覆盖或误点击，但工具实际将拖动与恢复排成先后顺序，未命中竞争窗口；另观察到恢复时各轴增加 1 像素，原因未定位。

会话结束恢复修复、回归和消融结果见 [tests/RECOVERY-FIX.md](../tests/RECOVERY-FIX.md)。颜色矩阵恢复光标使用“读取并回写当前位置”：本机静止光标能立即恢复；特定并发交错可能覆盖新位置，但尚未观察到本机 agent 因此回跳或误点击。两个无坐标回写候选均未通过实体光标测试，当前保留有效实现并记录该条件性风险。

最新结构消融的三项精简、编译对照和收益边界见 [tests/STRUCTURE-ABLATION.md](../tests/STRUCTURE-ABLATION.md)。本轮没有自动反复切换屏幕。

私有内存组成、输入组件优化与低频验证见 [tests/MEMORY-ATTRIBUTION.md](../tests/MEMORY-ATTRIBUTION.md)。

本轮内存优化的保留项、舍弃项、手动测试与测量限制见 [tests/MEMORY-OPTIMIZATION.md](../tests/MEMORY-OPTIMIZATION.md)。

此前复用系统 UCRT 的体积、依赖和功能回归见 [tests/SYSTEM-UCRT-RESULTS.md](../tests/SYSTEM-UCRT-RESULTS.md)。

当前一键黑屏版本的功能与恢复流程测试见 [tests/TOGGLE-RESULTS.md](../tests/TOGGLE-RESULTS.md)。

独立对抗审查、代码简化与消融实验见 [tests/REVIEW-ABLATION.md](../tests/REVIEW-ABLATION.md)。

旧版颜色矩阵模式的实测见 [tests/RESULTS.md](../tests/RESULTS.md)。`3e0fca3` 的 Gamma 与双模式验证记录见 [tests/GAMMA-RESULTS.md](../tests/GAMMA-RESULTS.md)。原生亮度键研究已放弃，历史实验与结果见 [tests/NATIVE-KEY-RESULTS.md](../tests/NATIVE-KEY-RESULTS.md)，实验代码不参与正式程序编译。
