# 内存优化与消融实验

日期：2026-10-08。范围：当前 Xiaomi Book Pro 14；功能保持不变。

## 结论

只保留一项变更：通过系统 advapi32.dll 的原生 WMI 导出读写硬件亮度，替换本进程的 COM/WMI 对象。保持标准 C++ 启动代码与原有显示逻辑。正式 EXE 为 **25,600 字节（25 KiB）**；手动使用后的私有驻留约 **1.75–1.80 MiB**，旧版约 2.2 MiB；**未达到低于 1 MiB 的目标**。

没有裁剪工作集、删除功能或引入汇编。内存数值受首次调用、菜单、进程运行时间和系统回收影响，不是固定上限。

## 测试方式

用户要求降频后，停止自动反复开关、快速循环和多轮显示测试。后续显示写入由用户手动完成：Gamma 开／关一次，矩阵开／关一次。使用 manual-resource-snapshot.cpp 单次只读采样，Gamma 的硬件亮度另用系统 CIM 接口交叉确认。

不重新运行压力负载、动画页面、浏览器截图回归或 DWM/GPU 对比。源码对照确认输出、状态切换、窗口逻辑不变；历史截图、操作、模式交接和退出测试只能作为未改代码的证据，不能冒充本轮完整重测。

memory-state-probe.cpp 已默认禁用旧自动测量入口。历史探针与脚本不随 EXE 发布；本轮未重新运行自动切换探针。

## 消融与取舍

| 尝试 | 实测／发现 | 决定 |
| --- | --- | --- |
| 每次操作后释放 COM/WMI | 使用后私有驻留 2,453,504 字节，对照 2,428,928 字节 | 无明确收益，不保留 |
| 每次恢复后释放矩阵组件 | 使用后 2,441,216 字节 | 无明确收益，不保留 |
| 组合上述两项 | 早先 toggle/review 探针通过；状态测量中断，无完成标记 | 不作为通过结果，不保留 |
| DDC/CI | 能枚举，能力及亮度查询错误 31 | 不采用 |
| 旧 LCD IOCTL | 只确认能打开设备；查询接口已弃用 | 不追加写入测试，不采用 |
| 原生 WMI＋标准启动 | 手动亮度设置、恢复通过；启动及使用后内存下降 | 保留 |
| 原生 WMI＋自定义启动入口 | EXE 13 KiB；启动私有驻留 1,306,624 字节，对照 1,294,336 字节 | 文件更小，本次无内存收益，不保留 |

各单项与已有基线比较，未因单项通过就判定组合通过。生命周期候选使用降频要求之前的记录，不重复跑。自定义启动候选只启动一次，没有切换显示。正常启动机制够用，无需加入汇编。

早期快速测试在原版也出现过硬件亮度恢复检查失败，650 ms 间隔后的原版单轮通过；根因未确认，不能据此认定驱动、硬件损伤或某项优化有问题。baseline-fast-readback-*.csv 保留诊断数据。

## 内存对照

单位 KiB（1,024 字节）。私有提交包含未驻留的私有内存；总工作集还包含共享系统 DLL 页面。

| 阶段 | 旧版私有驻留 | 新版私有驻留 | 旧版私有提交 | 新版私有提交 |
| --- | ---: | ---: | ---: | ---: |
| 启动 | 1,720 | 1,264 | 2,424 | 1,884 |
| 矩阵使用后恢复 | 2,240 | 1,788 | 3,480 | 2,880 |
| Gamma 使用后恢复 | 2,244 | 1,848 | 3,480 | 2,960 |

旧版来自 baseline-state-1.csv；新版启动与 Gamma 来自 PID 20588，矩阵来自重启后的 PID 25824。操作顺序、次数、采样时间不完全一致，不能称为严格随机对照或同一个进程完整使用后的内存上限。观测减少约 0.39–0.45 MiB；为降低频率，不重复测量以追求精确百分比。

新版 Gamma 恢复后的总工作集为 19,886,080 字节（约 18.96 MiB），其中私有驻留为 1,892,352 字节（约 1.80 MiB）。总工作集不能等同于独占 RAM。

## 验证记录

- Gamma 变暗：用户确认明显变暗；独立 CIM 查询硬件 0，Gamma 白端 32767，系统执行请求状态 3。
- Gamma 恢复：用户确认与测试前一致；硬件 100，Gamma 白端 65535，执行请求状态 0。
- 矩阵：用户确认全黑、恢复正常、光标立即出现；恢复后硬件仍为 100，矩阵系数 1，Gamma 白端 65535，执行请求状态 0。
- 静态复核原生 WMI 原型、GUID、实例名和参数布局。方法 1 的输入是 uint32 超时（0）加 uint8 亮度，共 5 字节。
- Release 构建成功，保留 ASLR、高熵地址空间和 NX；未引入额外 VC++ 运行库安装要求。
- 正式源码与手动测试候选除注释外相同；ApplyOutput、ToggleBlack、SelectMode、WindowProcedure 与旧版相同。
- 正式 EXE 与测试候选的全部二进制差异仅位于 PE 头及调试目录的构建时间戳；机器码、导入、数据和重定位相同。
- 老探针改为引用保存的 COM 基线，保持观察器独立于被测程序的新亮度实现，不再依赖正式源码中的旧 COM 全局变量。

后台观察：PID 25824 从 10:33:56 至 10:49:20 连续运行约 924 秒，包含一次用户手动矩阵开关。结束时私有驻留 1,753,088 字节（1.67 MiB），私有提交 2,842,624 字节，硬件亮度 100、矩阵和 Gamma 已恢复、执行请求状态 0。与 10:39:52 的恢复后快照相比，568 秒内 CPU 时间增加 15.625 ms，私有驻留减少 77,824 字节，未观察到增长；该区间约占单个 CPU 核心 0.0028%，包含启动和操作的整个进程存活期约 0.0101%。这不是宣称 CPU 绝对为零或任意场景的性能上限，也没有测量新的 DWM/GPU 开销。

该观察没有重新执行 15 分钟持续暗屏、不息屏或浏览器渲染性能验收。完整数据在 native-wmi-observation-end.json。随后正常退出测试副本、启动正式 dist/ScreenSave.exe，未执行额外暗屏切换；启动状态记录在 final-release-running.json。

## 适用范围

原生 WMI 导出来自系统 DLL，但未列于当前 SDK 的公开头文件和导入库。原型核对自 [Wine wmium.h](https://github.com/wine-mirror/wine/blob/master/include/wmium.h)，数据结构来自 SDK wmistr.h，GUID 和方法输入顺序来自本机 WMI 元数据。

本机返回一个 296 字节 WNODE 和一个内置屏实例；实现使用固定 512 字节读取缓冲区、128 个 wchar_t 的实例名缓冲区。没有添加多屏适配、兼容层或失败回退。**系统 DLL 自带不意味着调用路径受公开兼容承诺保护；不能保证其他 Windows 10/11 设备或未来系统、驱动版本可用。** 本轮仅在当前设备条件下验证。

参考：[旧亮度 IOCTL 弃用说明](https://learn.microsoft.com/en-us/windows/win32/power/ioctl-video-query-display-brightness)、[WNODE_ALL_DATA](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wmistr/ns-wmistr-tagwnode_all_data)、[WmiSetBrightness](https://learn.microsoft.com/en-us/windows/win32/wmicoreprov/wmisetbrightness-method-in-class-wmimonitorbrightnessmethods)、[工作集定义](https://learn.microsoft.com/en-us/windows/win32/procthread/process-working-set)。

## 文件与可复查证据

- 正式源码：../screen-save.cpp，SHA-256 F7EDED03ECF11EFD70C094AAE46F44E6496FF43F609757576C9F6CEE5A265E29。
- 正式 EXE：../dist/ScreenSave.exe，SHA-256 0697798FDAAF72C928C260E8D5C9BF4EE3E2481FC4C0517E5A2E15D76A99BD0E。
- 旧版源码、构建命令和信息保存在 artifacts/memory-optimization/baseline.cpp、baseline-build.cmd、baseline.json；旧 EXE 副本在 ../build/memory-optimization/baseline.exe。
- 全部测量、候选和机器码对照：artifacts/memory-optimization/。final-static-check.json、final-pe*.json/txt 记录交付核对。
- 测试工具不发布；本报告记录实验结果，存档版本以 Git 历史为准。
