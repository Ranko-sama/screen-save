# 对抗性审查与消融实验

日期：2026-10-07。本次工作按当前需求进行：仅一键暗屏／恢复，保留矩阵和 Gamma 两种方式，不恢复分档调光或原生亮度键研究。

三名独立子代理分别审查正确性、最小设计、系统副作用与测试可信度；主代理核实分歧、修改产品并串行执行显示实验。没有让多个测试进程同时修改显示状态。

结论：修复了状态恢复、重复启动、模式交接和菜单焦点问题。正式消融共 24 组：6 项简化可保留，18 项删除必要行为的实验失败。另外一项 Gamma 探索暴露了测试预期错误，已撤回不必要的产品改动并重新运行正式消融。没有引入新的后台机制、框架、配置或防御性兼容层。

## 从需求推导最小状态

程序需要知道当前是否暗屏、选择哪种模式，以及本次暗屏前应恢复的亮度和颜色。这些状态不能由“当前显示状态”反推：变黑之后，原颜色和亮度信息已经丢失。因此保留一个模式枚举、一个布尔值、一份会话恢复快照。

| 操作 | 必须发生的行为 |
| --- | --- |
| 正常时选择模式 | 只修改选择，不写显示、鼠标或电源状态 |
| 正常→暗屏 | 保存会话快照，应用所选效果，申请持续保活 |
| 暗屏中切换方式 | 先应用目标效果，再撤回旧效果，共用原快照 |
| 暗屏→正常／退出 | 撤回当前效果，恢复 Gamma 模式修改的硬件亮度，释放保活 |

保留 `Query`、`ApplyOutput` 等多处复用且有具体职责的函数；删除只有一处剩余调用的 `ApplyDimming` 包装。不把 COM、显示、托盘再封装成对象体系。

## 审查发现与修复

| 发现 | 实际证据 | 最小修复 |
| --- | --- | --- |
| 多进程争用显示恢复状态；后启动的进程可记住已经变暗的表，之后无法正常恢复 | 基线矩阵与 Gamma 重复启动测试均出现颜色状态污染 | 检查现有固定快捷键的注册结果；注册失败就退出，注册成功后才初始化。退出恢复完成后才释放快捷键，不再增加互斥锁 |
| 未暗屏时切换模式也重写颜色，可覆盖外部刚设置的状态 | 外部矩阵 0.8、Gamma 0.75 条件下，基线两种闲置选择均失败 | 闲置选择只修改枚举 |
| 颜色恢复值取自程序启动，而非进入暗屏前 | 基线 Gamma 会话恢复测试失败 | 每次进入暗屏时保存矩阵和 Gamma；暗屏内切换不覆盖快照 |
| 先撤掉旧效果、再上新效果，会短暂出现原始颜色输出 | 基线和 `old_handoff_order` 都采到一次两种颜色表同时正常 | 先启用目标效果，再撤掉原效果；最终双向切换采样均未发现该间隙 |
| 取消托盘菜单并点击另一窗口后，焦点被抢回原窗口 | 修正测试导航后，基线及 `no_focus_guard` 都只在该断言失败 | 仅当程序仍拥有前台窗口时恢复原窗口 |

快捷键注册检查承担实际显示会话独占职责，不是另建兼容或恢复体系。被其他程序占用时，本程序不启动；重复启动不会出现第二个常驻实例。[RegisterHotKey 接口说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)。

原光标修复继续保留：矩阵暗屏隐藏光标，恢复时显示光标并重新设置当前物理坐标。删除隐藏或刷新操作的负对照都会失败；最终三轮恢复、两方向切换和正常退出均保持坐标与前台窗口不变。历史上用户也已目视确认静止光标立即恢复。

## 消融方法

分别保存本轮起始代码和最终优化代码：

- [baseline.cpp](artifacts/ablation/baseline.cpp)：本轮开始时的未提交光标修复版，不等同于 Git HEAD。
- [optimized.cpp](artifacts/ablation/optimized.cpp)：最终产品源码快照，保留原有相对 Gamma 缩放。

每组只删除或替换表中所述机制；`baseline_no_wrapper` 以已删除重复模式保活调用的版本为起点，这一依赖明确记录在清单中。快照均为测试资料，不参与正式构建。

候选由 [build-ablation.ps1](build-ablation.ps1) 精确匹配代码生成，独立编译为 EXE；[run-ablation.ps1](run-ablation.ps1) 串行运行测试，在每组之后从测试进程恢复矩阵、Gamma、硬件亮度和光标。失败候选不会复制到 `dist/`。每轮均完成清理，未强制结束进程；删除 WMI 代理设置的候选有异常退出，见下表。测试前后硬件亮度均为 0。

原始 [候选清单](artifacts/ablation/manifest.json) 和 [结果 JSON](artifacts/ablation/results.json) 包含每组退出码、断言、耗时、EXE 大小和 SHA-256。负对照的失败是实验结果，不是最终产品验收失败。

| 候选 | 删除／替换内容 | 失败断言数 | 决策 |
| --- | --- | ---: | --- |
| baseline_no_translate | 消息循环的字符翻译 | 0 | 删除；程序没有文本输入控件 |
| baseline_no_inactive_writes | 正常状态选择模式时的显示写入 | 0 | 删除 |
| baseline_no_mode_power | 模式切换时重复提交保活 | 0 | 删除；暗屏状态未变化 |
| baseline_local_tray | 托盘数据的全局作用域 | 0 | 改为局部变量 |
| baseline_matrix_bool | 旧矩阵 gain=0／1 乘法 | 0 | 改为暗屏时直接清零 RGB 列 |
| baseline_no_wrapper | 单次调用的 ApplyDimming 包装 | 0 | 内联到切换逻辑 |
| optimized_no_com_blanket | WMI 代理权限设置 | 12 | 保留；候选实际以 0xC0000005 退出 |
| no_old_output_restore | 切换时撤回旧模式效果 | 8 | 保留 |
| no_matrix_zero | 矩阵 RGB 清零 | 4 | 保留 |
| no_gamma_gain | Gamma 暗态表 | 5 | 保留 |
| no_hardware_write | 硬件亮度写入 | 5 | 保留 |
| no_brightness_snapshot | 会话亮度快照 | 5 | 保留 |
| no_matrix_snapshot | 会话矩阵快照 | 11 | 保留 |
| no_gamma_snapshot | 会话 Gamma 快照 | 13 | 保留 |
| no_exit_restore | 暗屏退出时的恢复 | 5 | 保留 |
| no_keepalive | 整个保活请求 | 9 | 保留 |
| no_continuous | ES_CONTINUOUS | 9 | 保留 |
| no_display_request | ES_DISPLAY_REQUIRED | 9 | 保留 |
| no_system_request | ES_SYSTEM_REQUIRED | 9 | 保留 |
| no_cursor_visibility | 光标隐藏／显示 | 10 | 保留 |
| no_cursor_refresh | 当前物理坐标刷新 | 5 | 保留 |
| no_single_owner | 快捷键独占检查 | 4 | 保留 |
| old_handoff_order | 回退到先撤旧效果的顺序 | 1 | 保留目标效果优先 |
| no_focus_guard | 无条件恢复菜单前的焦点 | 1 | 保留前台归属判断 |

这里的“保留”指本机需求及本次测试足以证明其必要性，不声称枚举了所有代码组合。消融单位是有行为意义的机制，没有尝试删除语言语法、每个 COM 释放或每个 Win32 生命周期调用。

### 撤回无必要的 Gamma 改动

审查中曾把“历史上从标准线性表测试得到的 0.5 系数”误当成所有条件下的绝对限制，尝试改为固定标准 0.5 表。随后对照的原始读回数据是：外部表白点 49151，按原语义减半后白点 24575。接口实际接受了该表，不能称为驱动拒绝。

最初测试却硬性要求 32767，因而报告一次失败。这是测试跟随新实现的错误预期，不是既有产品缺陷。产品已恢复按会话原表逐项除以 2；修正后的测试核对全部 768 项相对缩放和完整恢复，结果为 `FAILURES=0`。固定表规则没有必要，还可能比原缩放更亮，因此从交付中撤回。

保留 [首次错误预期记录](artifacts/ablation-first-pass/old_gamma_scaling-initial-invalid-oracle.txt)、[修正预期后的记录](artifacts/ablation-first-pass/old_gamma_scaling.txt) 和 [第一轮结果](artifacts/ablation-first-pass/results.json)，避免将探索历史混同最终验收。正式 24 组随后重新生成、执行，使用最终源码及修正后的测试。这里只证明接口读回，没有新一轮 Gamma 实体亮度测量。

## 最终版本回归

最终交付 EXE 的下列四套测试全部通过；[汇总记录](artifacts/review-final-summary.json) 中四个退出码均为 0。

| 测试 | 覆盖 | 结果与记录 |
| --- | --- | --- |
| toggle-probe | 两种模式、硬件亮度、连续切换、快捷键归属、暗屏退出、保活申请／释放 | [FAILURES=0](artifacts/review-final-toggle.txt) |
| review-probe | 外部颜色、会话快照、非默认 Gamma、交接采样、三种状态下重复启动、快捷键冲突 | [FAILURES=0](artifacts/review-final-adversarial.txt) |
| cursor-display-probe | DXGI 硬件光标可见性、静止恢复、坐标与前台窗口保持 | [FAILURES=0](artifacts/review-final-cursor.txt) |
| menu-hotkey-probe | 实际 Ctrl+Alt+B 输入、原生菜单方向键与子菜单选择、Esc、点击别处取消时焦点 | [FAILURES=0](artifacts/review-menu-final.txt) |

前三种状态测试通过排队消息触发实际 EXE；菜单测试在测试工具自建的窗口内发送真实输入，进入前检查前台归属，菜单选择前检查高亮命令。未向其他应用发送按键或点击。

最初菜单测试错误地假设 Home 会选中鼠标打开菜单的第一项；日志中并未高亮，因此 Enter 只是取消菜单。后改为 Down 并加入高亮检查。最初两次失败属于测试工具的导航假设错误，不作为产品缺陷证据。修正导航后的 [基线测试](artifacts/review-menu-baseline-corrected.txt) 才复现了抢焦点问题，之后又用单项回退确认。

光标测试本轮补上“暗屏必须实际隐藏”的断言，避免只检查恢复、不检查隐藏的盲点。Gamma 测试核对全部 768 项，不能仅凭接口成功或白点值认定正确。

## 构建、静态分析和边界

正式源码 222 行；本轮起始为 218 行。增加必要缺陷修复抵消了简化行数，未将行数下降当作质量目标。最终 EXE 为 **104,960 bytes（102.5 KiB）**，与本轮起始相同。

最终 EXE SHA-256：`A98492F54C9059EBC32E47CD806C482D86ACBD1D34338A3A535DF4902DE977B4`。

本机 x64 Release 使用 `/O1 /GL /MT /W4 /LTCG`，构建无编译警告。另一次 `/analyze` [记录](artifacts/review-static-analysis.txt) 有未检查 COM 返回值、依赖成功输出的指针和入口 SAL 注解警告，不能称为静态分析零警告。按用户明确要求，未增加 API 失败恢复、重试或跨机兼容层；本程序继续依赖本机现有 WMI、Magnification 和 Gamma 接口可用。未做这些依赖失效时的故障注入验收。

消融保活检查读取系统执行请求标志：暗屏时必须同时含显示与系统请求，恢复后回到基线。它不是新的 15 分钟不息屏实验，也不代表阻止手动睡眠。持续请求语义见 [SetThreadExecutionState](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate)。

模式交接采样只能验证所读取的颜色表没有同时恢复正常，不能代替高速摄影证明实体显示完全无瞬态。光标测试读取 DXGI 元数据，不保存截图。

本轮没有重跑浏览器／桌面截图准确性、15 分钟持续测试或三轮 DWM/GPU 性能对比；历史测试结论仍只适用于其记录版本。颜色矩阵的系统合成路径没有改变，不能据此宣布以前的性能偏差消失或重新通过 ±5% 门槛。正常空闲占用单次短采样见 [review-idle-sample.json](artifacts/review-idle-sample.json)，它不测暗屏时的 DWM/GPU 额外开销。

最终 EXE 在正常状态下采样 30.006 秒，累计 CPU 时间增量为 0 ms（测量分辨率内）；工作集 15,953,920 bytes，约 15.2 MiB；私有内存 2,486,272 bytes，约 2.37 MiB。这是一次短采样，不是绝对零开销或长期平均的证明。

正式文件已更新为 `dist/ScreenSave.exe` 并启动，启动显示状态见 [review-release-state.txt](artifacts/review-release-state.txt)。测试源码、快照、实验脚本和日志都不编入或随 EXE 分发。本轮未创建 Git 提交。

## 复现实验

在本机 Visual Studio 2022 x64 Developer PowerShell 中，从项目根目录先编译开发工具：

```powershell
New-Item -ItemType Directory -Path build/review -Force | Out-Null
$libs = @('User32.lib','Gdi32.lib','Shell32.lib','Magnification.lib','Ole32.lib','OleAut32.lib','WbemUuid.lib')
cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE tests/review-probe.cpp /Fobuild/review/review-probe.obj /Febuild/review/review-probe.exe /link /SUBSYSTEM:CONSOLE $libs
cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE tests/toggle-probe.cpp /Fobuild/review/toggle-probe.obj /Febuild/review/toggle-probe.exe /link /SUBSYSTEM:CONSOLE $libs PowrProf.lib
cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE tests/cursor-display-probe.cpp /Fobuild/review/cursor-probe.obj /Febuild/review/cursor-probe.exe /link /SUBSYSTEM:CONSOLE User32.lib D3D11.lib DXGI.lib Magnification.lib Dwmapi.lib
cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE tests/menu-hotkey-probe.cpp /Fobuild/review/menu-hotkey-probe.obj /Febuild/review/menu-hotkey-probe.exe /link /SUBSYSTEM:CONSOLE User32.lib Gdi32.lib Magnification.lib
.\tests\build-ablation.ps1
```

关闭正式程序，然后运行 `.\tests\run-ablation.ps1`。这是实际修改全局显示的开发实验：测试期间不要同时运行其他调光测试或手动改变亮度，菜单用例会短暂显示两个自有测试窗口。工具会逐组恢复初始状态。构建脚本本身不会启动候选。

两脚本均可用 `-Names no_single_owner,no_focus_guard` 指定组别；结果按名称更新。源码快照保证本轮候选不会随着后来产品修改而静默变化；日志中的路径、进程号和二进制哈希对应本机本次运行。
