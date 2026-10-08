# 私有内存归因与定向优化

2026-10-08，基线提交 cf1cfaf。遵循低频测试要求：先读取现有进程，再仅验证有具体证据的候选；不运行压力负载或自动黑屏循环。

## 现有进程快照

PID 26240，进程从 10:59:24 运行至读取时；正式 EXE SHA-256 为 0697798FDAAF72C928C260E8D5C9BF4EE3E2481FC4C0517E5A2E15D76A99BD0E。

一次外部读取耗时 94 ms，只读进程查询和少量 PEB/TEB 元数据；没有注入、挂起、写入目标内存、裁剪工作集或调用显示调整接口。驻留页在读取 84 字节元数据之前采集；读取前后系统计数一致。

| 类型 | 私有驻留 KiB |
| --- | ---: |
| 系统 DLL 映像的私有页 | 1,072 |
| ScreenSave.exe 映像的私有页 | 8 |
| 默认进程堆根所在分配 | 424 |
| 尚未归属的私有分配 | 148 |
| 主线程栈 | 24 |
| TEB 所在分配，可能与其他环境块共用 | 12 |
| 页枚举与系统计数尚未解释的差额 | 32 |
| 系统报告的私有驻留合计 | **1,720** |

私有提交总量为 2,801,664 字节，总工作集为 19,460,096 字节。虚拟保留地址不计为实际驻留内存。MEM_IMAGE 的整段已提交大小不能当成私有提交；其共享代码页不纳入上表。

映像私有页最多的模块包括 Shell32（76 KiB）、Windows.Storage（64 KiB）、NTDLL（52 KiB）、KernelBase（44 KiB）与 COMBase（40 KiB）。发现一个进程堆和一个线程。堆中的具体分配者尚未追踪；堆根以外的区域未凭地址外观强行归为堆。

EXE 自身只有 8 KiB 私有映像页，并不意味着其余内存可以不算程序成本。系统组件的私有分配也是本程序引入的占用。当前只解释了区域分布，没有证明 1,720 KiB 是最低要求。

数据：artifacts/memory-map/current-process.json、current-summary.json。首次工具按 VirtualQueryEx 与 QueryWorkingSetEx 统计，保留 32 KiB 未覆盖差额；后续工具增加 QueryWorkingSet 交叉读取，不修改或覆盖首次原始记录。

## 初始化归因

制作两个临时诊断副本，各启动一次，约 1.5 秒后自动退出。副本使用单独窗口类和临时快捷键，不接管正式程序的 Ctrl+Alt+B，不改变显示。分阶段内存和模块清单保存在 baseline-init.json 与 no-ime-init.json。

基线在进入 wWinMain 时已加载图形及托盘依赖，仪器化副本私有驻留为 1,060 KiB；创建隐藏窗口后加载 UxTheme 和 MSCTF；托盘注册再加载 SHCore、Windows.Storage 与 SHLWAPI，最终为 1,280 KiB。

这些是含诊断记录数组的启动值，不能直接和长时间使用后的正式进程比较。诊断函数自身也会触及内存，仅用于同样记录方式下的阶段归因。

## 候选：关闭本线程不需要的 IME

程序没有文本输入控件。候选在创建窗口之前增加 ImmDisableIME(GetCurrentThreadId())，仅作用于自身线程；按微软说明在第一个顶层窗口创建前调用。构建额外链接 Windows 自带的 Imm32.lib，不更改系统输入法或其他进程。

同一诊断流程中，该候选没有加载 MSCTF，结束时私有驻留 1,256 KiB，私有提交 1,864 KiB；基线分别为 1,280 和 1,900 KiB。本次各运行一次的观察差额为 24 KiB / 36 KiB，不能据此断言稳定或长期收益；在 IME 调用前的 entry 阶段，两者已分别存在 +4 KiB 驻留、−4 KiB 提交差异。

正常副本的模块清单还包括 TextInputFramework、CoreMessaging 等，单凭出现这些模块不能断言全由 IME 引入。补充真实托盘操作后的内存快照，观察使用后的输入组件及私有分配。

用户按提示完成一次托盘选择 Gamma、Ctrl+Alt+B 最暗及恢复，回复“操作完成”。软件读回硬件亮度 100、矩阵系数 1、Gamma 白端 65535、执行请求状态 0，确认恢复状态。没有再次自动切换显示，也没有要求重复颜色矩阵、截图或压力测试。

上述历史读回仅验证列出的字段，没有逐元素比较全部 25 项矩阵和 768 项 Gamma 表，不能由这些抽样字段单独证明完整恢复。

## 操作后的结果

| 状态 | 私有驻留 KiB | 私有提交 KiB | 线程数 |
| --- | ---: | ---: | ---: |
| 旧版现有进程，已使用且空闲 | 1,720 | 2,736 | 1 |
| 候选完成本次 Gamma 操作后 | 1,576 | 2,596 | 6 |
| 候选再空闲约 160 秒 | **1,496** | **2,472** | 1 |

两次候选读数之间 CPU 累计时间均为 140.625 ms，增量为 0（受计时精度限制，不代表绝对无 CPU 使用）。私有驻留自然减少 80 KiB，没有进行工作集裁剪。候选最后为约 **1.46 MiB**，比旧版该次记录少 224 KiB，**仍未达到低于 1 MiB 的目标**。

操作后内存映射中，映像私有页为 960 KiB、堆根分配私有驻留为 360 KiB，基线分别为 1,080 和 424 KiB。TextInputFramework、CoreMessaging、CoreUIComponents 等模块未出现；MSCTF 在托盘实际使用后仍然出现，因此本改动只避免部分输入支持成本。

新旧进程启动时间、闲置时长与完整操作历史不同，不能视为完全匹配的随机对照。本轮候选只补了一次 Gamma 操作；没有重新执行矩阵暗屏。部分 DLL 的加载也可能取决于是否使用过矩阵光标路径，不能把每个缺失模块或全部 224 KiB 差额都归因于 IME。可直接支持的结论是：本次同流程启动观察中候选低 24 KiB，实际菜单与 Gamma 使用后也观察到较低占用，且未加载部分输入组件；不承诺稳定节省 24 KiB 或所有状态都维持 1.46 MiB。

第二次外部内存映射读取耗时 125 ms，前后总内存计数相同。QueryWorkingSet 与按区域汇总的 QueryWorkingSetEx 私有驻留都为 1,544 KiB，系统总计数为 1,576 KiB；仍差 32 KiB，原因未确定，保留为未解释差额。为控制测试频率，不再为这 32 KiB 反复采样。

原始数据：no-ime-after-use.json、no-ime-manual-result.json、after-use-comparison.json、no-ime-idle.json，均在 artifacts/memory-map/。

## 保留项与验证边界

仅保留创建窗口前的一次 ImmDisableIME(GetCurrentThreadId()) 调用及 Imm32.lib 链接。没有删除模式、托盘或恢复功能，没有新增后台线程、定时器、轮询或配置。此调用只作用于程序自身线程；诊断工具的记录与自动退出定时器不进入正式程序。

标准 Release 构建通过。正式源码与用户操作的候选完全一致，EXE 仅构建时间戳不同，体积仍为 **25,600 字节（25 KiB）**。程序的消息循环、显示处理、亮度接口、光标恢复、模式交接及保活逻辑均未更改。

本轮完成一个有依据的功能保留候选及对应启动消融；不为了凑数增加第二个候选。没有重新测量 DWM/GPU，也没有重做浏览器输入和截图测试；本轮没有证据表明这两项开销进一步下降。源码和构建结果已更新，未创建新 Git 提交。

- 正式源码 SHA-256：4EEB70910320B2D11512F8CF21727B6161366850278E846938851471FFE4D19C。
- 正式 EXE SHA-256：FDC5EE5EF067CFF420A5A2D33E4DEF02C8C871864A1F50A709E48EC57720AEE0。
- 构建及机器码对应记录：artifacts/memory-map/final-build.txt、final-build-check.json。
- 基线源码保存在 artifacts/memory-map/baseline.cpp；已提交版本为 cf1cfaf。

## 参考与范围

- [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex)：写时复制之后，页仍可报告为 MEM_IMAGE/MEM_MAPPED。
- [QueryWorkingSetEx](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-queryworkingsetex)：结合 Valid 和 Shared 统计已驻留的私有页，不为测量而锁页或触发非驻留页读取。
- [ImmDisableIME](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime)：候选 API 的作用域及调用时点。
- PEB 堆目录布局交叉核对自 [System Informer 的 phnt](https://github.com/winsiderss/phnt/blob/master/ntpebteb.h)，仅限本机 x64 开发探针；不加入正式程序。

测试工具均在 tests/ 和 build/，不随正式 EXE 发布。

2026-10-08 复现脚本修正：`build-startup-memory.ps1` 固定读取上述历史基线，避免当前源码已有 IME 调用后生成一次／两次调用的错误对照。重建副本及其输出位于 `build/memory-map/reproduce-startup/`，不会覆盖本报告的历史原始记录；本次修复未重新测量内存。
