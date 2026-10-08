# 当前版本的结构消融

日期：2026-10-08。

本轮以已完成原生 WMI、系统 UCRT 和本线程 IME 优化的工作区为基线，检查正式程序所有状态、函数和消息路径。没有把过去的功能回归计作本轮测试，也没有重复历史上的自动暗屏实验。用户要求降低测试频率，本轮采用四项单因素编译消融、机器码检查与一次正常重启。

保留三项修改，舍弃一项展开方案。现有功能、系统接口、初始化顺序、恢复顺序和依赖保持一致；没有增加生产代码机制。

## 实验与决策

每项候选独立从 [baseline.cpp](artifacts/structure-ablation/baseline.cpp) 生成，另编译组合版本。所有六个版本各编译一次，采用正式 Release 参数；没有启动任何实验候选。编译无警告。生成脚本为 [build-structure-ablation.ps1](build-structure-ablation.ps1)，原始数据为 [results.json](artifacts/structure-ablation/results.json)，[build.txt](artifacts/structure-ablation/build.txt) 保存构建输出。

| 单项消融 | 证据 | 决策 |
| --- | --- | --- |
| 移除 `nativeOpen` 的全局作用域，改为初始化函数局部变量 | 全部引用均在初始化内；Release `.data` 虚拟长度减少 8 字节、`.text` 减少 16 字节 | 保留；初始化完成后无需保存此指针 |
| 移除仅调用一次、只含三条清理语句的 `CloseBrightness` 包装，把原语句放在退出清理处 | 除 COFF/debug 时间戳外，整个 EXE 与基线逐字节等价 | 保留；减少一层跳转阅读，运行时本来已被编译器展开 |
| 移除 Gamma 恢复时生成临时表的循环；压暗时仍生成原表的一半，恢复直接提交原表 | SDK 将 Gamma 表标为只读输入；反汇编确认恢复绕过循环，压暗改用向量移位 | 保留；消除实际重复计算，不改变表值 |
| 移除 `OpenBrightness` 函数，把初始化全部放入程序入口 | `.text` 减少 64 字节、`.rdata` 减少 16 字节，但 EXE 和映像尺寸不变；入口混入 WMI GUID、导出解析、WNODE 名称解析 | 舍弃；这个函数仍承担明确的协议初始化职责，展开没有带来值得牺牲可读性的体积收益 |

不是所有单次调用函数都应删除。清理只有三条语句，且入口已有同级资源清理；WMI 初始化则有一整段独立协议细节，两者分别判断。

## Gamma 路径的等价性与实际变化

原恢复路径对 3 × 256 个 `WORD` 元素执行 `x / 1`，写入临时表；新路径直接提交保存的原表。`WORD` 的所有取值都满足 `x / 1 == x`。压暗前后都使用整数 `x / 2`，全部结果处于 `WORD` 范围，舍入方式未变。条件为恢复时不会读取未初始化的临时数组。

本机 Windows SDK `10.0.26100.0/um/wingdi.h:5370` 将 `SetDeviceGammaRamp` 的表参数标记为 `_In_reads_bytes_(3*256*2)`，所以恢复不需要另备可写副本。

基线机器码中，`ApplyOutput` 从 `0x14000124D` 起，不区分压暗／恢复均进入循环，内层 `0x140001263` 是运行时 `div`。组合版本在 `0x140001243` 的条件跳转绕过恢复循环；压暗循环的 `0x140001252` 为 `psrlw xmm0,1`，每次处理八个表项。由此确认删除的是实际执行的重复工作，而不只是源码上的写法。

[基线反汇编](artifacts/structure-ablation/baseline-disassembly.txt) 与 [最终反汇编](artifacts/structure-ablation/optimized-disassembly.txt) 保留作证据。地址仅针对这些固定构建。

## 对剩余设计的检查

| 保留内容 | 对应必要需求 |
| --- | --- |
| 一个模式枚举、一个暗态标志 | 两种方式可选，共用黑屏／恢复状态；枚举没有额外调度对象 |
| 系统亮度、矩阵、Gamma 的会话快照 | 改写后无法从暗态反推原状态；暗屏内切换方式仍须恢复进入前的状态 |
| `OpenBrightness`、`ReadBrightness`、`SetBrightness` | 集中本机 WMI 协议；读取与写入没有进一步的接口层、对象层或插件层 |
| `ApplyOutput`、`ToggleBlack`、`SelectMode` | 分别负责输出、暗态切换、模式交接，多个入口复用同一份逻辑 |
| 原生窗口过程、菜单命令、阻塞消息循环 | 接收系统快捷键和托盘操作；空闲不轮询 |
| 热键独占、保活、光标显示与物理位置刷新、焦点归属判断 | 先前实际负对照已证明删除会破坏需求；见 [上一轮消融](REVIEW-ABLATION.md) |
| 正常退出的清理及恢复顺序 | 先恢复显示，最后释放快捷键归属；本轮只展开清理语句，未删除释放操作 |
| 本线程 `ImmDisableIME` | 已有单因素对照支持的小幅内存收益，且没有文本输入需求；见 [内存归因](MEMORY-ATTRIBUTION.md) |

没有再次删除已知必要的机制；没有为了缩短源码而把多次使用的协议操作复制进调用方，也没有引入新的基类、回调表或统一资源框架。

## 体积、性能与验收边界

| 指标 | 本轮基线 | 最终组合 |
| --- | ---: | ---: |
| 正式源码行数（含空行和注释，不含末尾空行） | 199 | 195 |
| `.text` 虚拟长度 | 11,648 bytes | 11,632 bytes |
| `.data` 虚拟长度 | 3,936 bytes | 3,928 bytes |
| EXE 文件大小 | 25,600 bytes | 25,600 bytes |
| PE `SizeOfImage` | 40,960 bytes | 40,960 bytes |

这些段长度不是进程内存占用。8 字节的全局数据删减没有跨越映像分配边界，不能宣称 RAM 少了一页。Gamma 调用时少了计算，但空闲循环和显示合成路径未变；本轮没有 CPU、GPU、DWM 或稳态 RAM 性能对照，不声称这些指标明显改善，也不把历史实测当成本轮结果。

正式文件重新构建并正常重启一次。旧进程退出码为 0；退出后矩阵系数 1、Gamma 白点 65535、系统保活标志 0。重启后只读状态相同，见 [normal-exit.txt](artifacts/structure-ablation/normal-exit.txt) 和 [release-state.json](artifacts/structure-ablation/release-state.json)。没有发送暗屏／恢复命令、鼠标输入或模式切换。

[release-verification.json](artifacts/structure-ablation/release-verification.json) 比较正式 EXE 与组合候选：仅忽略 PE 定义的时间戳字段，其余所有字节一致。[release.json](artifacts/structure-ablation/release.json) 记录正式源码、EXE 的 SHA-256 与启动进程。正式构建无警告。

这是结构和编译层面的消融，不是新一轮实体屏幕功能验收。未重测实体黑屏、光标恢复、agents 截图或 15 分钟运行；这些沿用此前记录。新增测试资料不随单 EXE 发布。

复现：运行 `tests/build-structure-ablation.ps1` 只编译历史候选并生成比较数据；运行 `tests/build-structure-ablation.ps1 -VerifyRelease` 只比较现有构建，不启动程序、不调节屏幕。
