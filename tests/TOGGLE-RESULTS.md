# 一键黑屏版本验证

后续独立审查、状态修复与消融实验见 [REVIEW-ABLATION.md](REVIEW-ABLATION.md)。以下保留各历史版本当时的实现与测试结果；当前 Gamma 使用本次暗屏前的表进行缩放和恢复。

日期：2026-10-07。以本地提交 `3e0fca3` 为基础，简化为一键黑屏版本。

## 变更

- 删除分档状态、逐级亮度调节和 Ctrl+Alt+PageDown / PageUp，只保留 Ctrl+Alt+B 切换。
- 颜色矩阵模式直接黑屏，不写系统亮度。
- Gamma 模式同时设置系统亮度 0 与原始 Gamma 表的 0.5 倍；恢复或正常退出时还原颜色和本次黑屏前的亮度。
- 两种方式共用一个布尔状态和一次亮度快照。暗屏中切换模式不重取快照，避免覆盖恢复值；退出 Gamma 切回矩阵时还原硬件亮度。
- 保留托盘、暗屏保活、阻塞消息循环；没有动画、轮询或新后台机制。

## 实际进程验收

使用 [独立测试工具](toggle-probe.cpp) 启动编译后的候选 EXE，通过排队的 Win32 热键／菜单消息触发实际处理函数，并从系统接口读取完整矩阵、完整 Gamma 表、硬件亮度和电源请求状态。模式切换涉及 WMI，因此消息使用 PostMessage，避免跨进程同步消息禁止出站 COM 调用的问题。

以下检查全部通过，日志末尾 `FAILURES=0`：

- 启动与未激活退出均保持原显示状态；重启默认选择颜色矩阵。
- 原 PageDown/PageUp 快捷键可以被测试进程注册，旧调节消息不再改变显示；Ctrl+Alt+B 已由应用注册。
- 矩阵开启时，硬件亮度保持 60；Gamma 与矩阵切换后分别达到硬件 0 与原亮度 60。
- 重复选择 Gamma、快速连续切换两种模式后，原始亮度快照仍为 60。
- 启动后将正常亮度改为 37，再进入／退出 Gamma，恢复到 37，未误用启动时的 60。
- Gamma 暗屏中退出，硬件亮度与全部颜色数据正确恢复，进程退出码 0。
- 矩阵暗屏期间由测试工具将硬件亮度改为 43，程序不纠正它，退出时仍保留 43。
- 暗屏期间执行请求标志包含显示与系统保活；恢复／退出后回到基线 0。
- 测试自身的硬件修改最终恢复为测试前的 40，Gamma 与矩阵均恢复原值。

原始记录：[toggle-integration-20261007-213009.txt](artifacts/toggle-integration-20261007-213009.txt)。

这是功能与恢复流程验收；没有重新运行旧版的 15 分钟持续测试或三轮性能对比，也未在本轮重新采集浏览器和桌面截图。原有颜色处理 API 与最低 Gamma 系数保持不变，历史测试报告保留其原适用版本。

## 交付

已将通过验证的同一候选文件复制为 `dist/ScreenSave.exe` 并启动，启动后硬件亮度为 40，矩阵与 Gamma 正常，未申请保活。

- EXE：104,960 bytes，102.5 KiB。
- SHA-256：`B00FC6D3314EB35E32AF112B923CB447C737EC6340B24368187A4AFD7FDFD5F9`。
- 正式程序不包含测试计时、测试命令或日志；`tests/` 和 `build/` 不随 EXE 发布。

## 矩阵黑屏的光标修复（2026-10-07）

在 `e0a0813` 的基础上，矩阵分支增加一次 `MagShowSystemCursor(!dark)` 调用：黑屏时隐藏系统光标，恢复、切换到 Gamma 或从矩阵黑屏正常退出时重新显示。沿用现有状态和恢复路径，没有新增循环、计时器或输入拦截。

本机直接调用隐藏和显示均返回 TRUE，隐藏调用后的错误码为 0；鼠标位置在两次采样中保持 `(1283, 995)`。不过 `GetCursorInfo` 的 flags 在隐藏前后均为 1，因此该查询不能作为此接口实际隐藏光标的证明。用户随后目视确认：黑屏时光标消失，但恢复后必须轻微移动鼠标，光标才重新出现。因此该版本的静止光标恢复尚未通过。

候选 EXE 已重新运行同一独立进程测试，全部通过，`FAILURES=0`。覆盖矩阵和 Gamma 的进入、恢复、双向切换、连续切换及正常退出；原始矩阵、Gamma、硬件亮度和保活状态恢复正确。测试前硬件亮度为 100，测试结束恢复到 100。此测试读取的是颜色、亮度和电源状态，不验证光标是否在实体屏幕上可见；也没有重新进行截图、实际点击或性能测试。

原始记录：[cursor-integration-20261007-220039.txt](artifacts/cursor-integration-20261007-220039.txt)。

已将通过功能测试的文件更新为 `dist/ScreenSave.exe` 并启动：104,960 bytes（102.5 KiB），与上一版本体积相同。SHA-256：`821BBCC6369EFC3A4B025E8A4A66DDEFC8B05EF0B52C8476CD39686AC28D6FDE`。

接口说明：[Microsoft：MagShowSystemCursor](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magshowsystemcursor)。

### 静止光标恢复的调用顺序验证

针对用户报告，候选版本将 `MagShowSystemCursor(!dark)` 移到 `MagSetFullscreenColorEffect(&colors)` 之前，尝试在颜色切换前先更新光标可见性。正式逻辑仅调整现有两次调用的顺序；没有增加鼠标移动、输入事件、延时或循环。该顺序能否解决实体光标延迟恢复，属于待验证的假设。

已有双模式与退出恢复测试再次全部通过，`FAILURES=0`，测试后硬件亮度恢复到 100。记录：[cursor-order-integration-20261007-220542.txt](artifacts/cursor-order-integration-20261007-220542.txt)。测试工具仍不能验证实体光标是否立即出现。

候选文件为 `build/ScreenSave-cursor-refresh.exe`，104,960 bytes，SHA-256：`8627BC2E7DEAD4F48B34A133A7A0837D0DE773B718715F7FD85515FEDE975B5E`。用户目视反馈仍需移动鼠标才出现，此方案未解决问题；调用顺序已撤回，未发布此候选文件。

### 单次零位移刷新候选版

恢复原调用顺序后，在矩阵分支显示光标时，通过 `SendInput` 提交一个仅含 `MOUSEEVENTF_MOVE` 的鼠标输入，dx、dy 和按钮数据均为 0。仅在该恢复调用中执行一次，无定时器或持续输入。用户目视确认仍需手动移动鼠标，此方案无效，新增的 SendInput 代码已撤回。

双模式与退出恢复测试全部通过，`FAILURES=0`：[cursor-input-integration-20261007-220954.txt](artifacts/cursor-input-integration-20261007-220954.txt)。

[开发输入观察工具](cursor-input-probe.cpp) 验证了测试步骤前后鼠标物理位置与前台窗口一致，但预期的三个注入移动事件均未被低级鼠标钩子观察到，记录为 `FAILURES=3`：[cursor-input-events-20261007-221158.txt](artifacts/cursor-input-events-20261007-221158.txt)。因此不能根据此工具声称已验证事件数量或实际重绘。补充直接调用比较中，普通零位移 SendInput、不合并零位移 SendInput、设置原物理位置三个请求都返回成功、位置保持不变，但钩子计数仍均为 0：[cursor-refresh-api-comparison.txt](artifacts/cursor-refresh-api-comparison.txt)。测试与前台 ChatGPT 进程的完整性级别均为 8192（Medium）。

该失败候选文件为 `build/ScreenSave-cursor-input.exe`，104,960 bytes，SHA-256：`331030DE047F7C9345EBD3E4E1B180E15C3D8984729303E2E96520CE544F9351`，未替换 `dist/ScreenSave.exe`。

接口依据：[Microsoft：MOUSEINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)。

### 当前物理位置刷新（软件对照与实体效果均通过）

撤掉无效的 SendInput 后，恢复时用 `GetPhysicalCursorPos` 取得当前位置，再用 `SetPhysicalCursorPos` 重新设置该物理位置。这两次调用只在恢复矩阵输出时执行，无后台任务、计时器、模拟点击或相对移动。使用物理坐标避免 DPI 坐标换算。

新增 [DXGI 光标观察工具](cursor-display-probe.cpp)，读取桌面复制接口的硬件光标可见状态及更新时间，不保存屏幕图像。最初的接口探针中，重新设置当前物理位置产生了光标更新，而先前的普通恢复未产生更新：[cursor-display-probe.txt](artifacts/cursor-display-probe.txt)。低级鼠标钩子不是 SetPhysicalCursorPos 是否刷新硬件光标的判断依据。

实际 EXE 的旧版／新版对照结果：

| 检查 | 原光标隐藏版 | 物理位置刷新候选版 |
| --- | --- | --- |
| 连续三轮矩阵恢复 | 三轮均无光标更新，仍为隐藏 | 三轮均有光标更新，变为可见 |
| 黑屏中切换到 Gamma | 无更新，仍为隐藏 | 有更新，变为可见 |
| 矩阵黑屏中正常退出 | 无更新，仍为隐藏 | 有更新，变为可见 |
| 物理坐标和前台窗口 | 各步骤保持不变 | 各步骤保持不变 |

新版整个测试期间光标位置为 `(1466, 1934)`。新版每次黑屏均观察到可见状态 0，恢复后为 1，`FAILURES=0`：[cursor-position-dxgi.txt](artifacts/cursor-position-dxgi.txt)。旧版在同一检查中恢复失败五次，`FAILURES=5`，符合已知缺陷：[cursor-original-dxgi-control.txt](artifacts/cursor-original-dxgi-control.txt)。因此此次软件检查能够区分该缺陷与修复效果，不再仅依赖接口返回成功。

完整的双模式、硬件亮度、颜色与保活恢复测试也全部通过：[cursor-position-integration-20261007-223133.txt](artifacts/cursor-position-integration-20261007-223133.txt)。没有重跑长期性能或 agents 截图／输入验收。

用户在鼠标静止时，通过 Ctrl+Alt+B 黑屏并恢复，确认：“立即出现，已经正常”。测试时 DXGI 探针已退出。

同一候选文件已更新为正式 `dist/ScreenSave.exe` 并启动，104,960 bytes（102.5 KiB），SHA-256：`C639A82D2E03AEC6393FC053ECBA1AC8B79084149BE290CB3BDCA13F4A882B9C`。启动后颜色矩阵与 Gamma 正常，保活标志为 0，硬件亮度为 100。发布程序仅新增光标隐藏／显示及原物理位置刷新，不包含任何开发探针。

接口依据：[SetPhysicalCursorPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setphysicalcursorpos)、[DXGI_OUTDUPL_POINTER_POSITION](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_pointer_position)、[DXGI_OUTDUPL_FRAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info)。
