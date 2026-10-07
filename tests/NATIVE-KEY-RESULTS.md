# 本机原生亮度键接入验证

日期：2026-10-07。设备：Xiaomi Book Pro 14，内置键盘标识 ACPI/MSFT0001。

用户确认直接 F5/F6 为亮度降低/提高，Fn+F5/F6 才是普通功能键。测试由用户实际按键完成，未用软件模拟 F5 代替硬件亮度键。

## 结果

| 操作 | 实测 |
| --- | --- |
| 直接 F5 三次 | 系统亮度 100 → 90 → 80 → 70；没有普通 F5 原始输入或标准亮度 HID usage 记录 |
| 直接 F6 三次 | 系统亮度 70 → 80 → 90 → 100；没有普通 F6 原始输入或标准亮度 HID usage 记录 |
| Fn+F5、Fn+F6 | Raw Input 分别收到虚拟键 0x74/0x75，扫描码 0x3F/0x40，按下和释放均可见 |
| F5 降到 0，继续按 F5 三次 | 亮度通知停在 0；没有可区分三次额外降低操作的记录 |
| 随后直接 F6 | 原生系统亮度按每次 10 个百分点提高 |

初次观察包含 Raw Input 键盘、Consumer Usage Page、GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS 通知。标准亮度 HID usage 为 0x6F/0x70。记录器还安装了低级键盘钩子，但该轮钩子没有捕获到作为阳性对照的 Fn+F5/F6，因此不把钩子的空记录当作排除其他输入路径的证据；普通功能键的阳性证据来自 Raw Input。

随后独立订阅本机 `WmiMonitorBrightnessEvent`。用户再次从 30 降到 0，额外按 F5 三次，再按 F6 一次。记录依次为：

```
elapsed_ms=34262 brightness=20
elapsed_ms=34661 brightness=10
elapsed_ms=35164 brightness=0
elapsed_ms=40399 brightness=10
```

最低档的三次按键也没有 WMI 事件。事件订阅只读、临时有效，测试结束取消；没有永久 WMI 订阅。

测试工具与事件监听均已退出。测试末尾系统亮度为 10，已恢复到本轮实体按键测试开始时的 100；正式 EXE 未修改。

## 修正探针后的补测（00:40:41 开始）

开发探针的低级钩子改为只 `PostMessage`，文件和界面更新移到消息循环；扩大函数键/媒体键观察范围，记录 Consumer HID 原始报告、解析状态和错误。厂商 WMI 脚本同时临时订阅 HID_EVENT20、21、22、23 以及 WmiMonitorBrightnessEvent。正式程序未增加这些机制。

00:36:36 的准备记录与 00:38:52 的失误轮不计入以下结论；后者由用户明确要求重做。有效补测由用户确认全部按顺序完成。

| 实体操作 | 有效补测记录 |
| --- | --- |
| 直接 F6 三次 | 系统亮度 0 → 10 → 20 → 30；只收到亮度变化通知 |
| Fn+F5、Fn+F6 各一次 | 34.032–36.485 秒，Hook 与 Raw Input 都收到按下/释放；扫描码 0x3F/0x40；Hook 标记 injected=0 |
| 直接 F5 降到 0 | 39.547–40.782 秒，系统亮度 30 → 20 → 10 → 0 |
| 系统 0 后继续 F5 五次 | 用户确认按顺序完成；40.782–47.969 秒之间没有 Hook、Raw Input、Consumer HID、厂商 WMI 或亮度变化事件 |
| 直接 F6 一次 | 47.969 秒，系统亮度升至 10 |
| 再次 Fn+F5、Fn+F6 | 50.016–51.422 秒，Hook 与 Raw Input 再次完整记录按下/释放；确认测试末尾仍能观察普通功能键 |
| Caps Lock 两次 | HID_EVENT20 的 EventDetail 前三项分别为 1,9,1 与 1,9,0；确认小米厂商事件通道有效，且两次操作恢复原状态 |

这一轮没有 Consumer HID 报告到达探针，不是收到报告后解析为未知亮度键；本轮登记范围仍为标准 Consumer Usage Page，不代表覆盖全部厂商私有 HID 页。WM_APPCOMMAND 也未记录到，但未单独保证探针全程为前台窗口，不将这一点用作排除所有应用命令来源的证据。

此次补测补齐了低级键盘钩子和厂商 WMI 的阳性对照，结果仍不能提供最低档时逐次继续调暗的信号。它不证明所有用户态路径都不可能，仅说明当前已验证的轻量路径没有实现所需行为。

原始记录：[Hook / Raw Input / 亮度通知](artifacts/native-key-probe-20261007-004041-1188.txt)、[厂商 WMI / 亮度事件](artifacts/native-key-vendor-20261007-004041-11300.txt)。

探针在 180 秒正常退出，WMI 脚本取消所有临时订阅并正常退出（exit 0）。WMI 日志末尾还包含用户在既定测试序列之外的亮度操作，不纳入上表。清理时读到亮度为 10，已恢复至本次开始按键测试前的 100。正式 EXE 的 SHA-256 仍为 `4C334C81CCEA50739D5095EC18CF2C1071A6CE2228E217B842E02C5C6A4A1429`。

## 对实现的影响

目前测试到的接口可以观察亮度已经发生的变化，却没有提供最低档继续降低的操作次数。因此不能据此实现“原生 F5 降到系统最低后，继续按 F5 进入 20 档额外压暗”。仅监测或轮询当前亮度也无法区分“在最低档按了 F5”和“没有按键”。

直接注册普通 F5/F6 会作用于 Fn+F5/F6，与用户原有的直接按键习惯不符。原生 F6 在软件压暗期间如何避免系统亮度同时提高，也尚无已验证的接管路径。

本结果不证明所有用户态接口都绝对不可实现，也未确证底层一定采用哪种固件/驱动路径。在保持不装驱动、不增加复杂兼容机制的项目边界下，当前没有验证通过的原生键接入方案，不能宣称已经支持。正式 EXE 保持原有备用组合键，未作功能修改。

## 记录与参考

- [后续重开探针的记录](artifacts/native-key-probe.txt)：旧版使用固定文件名，后续启动已覆盖首轮日志。上文首轮 Fn+F5/F6 的 Raw Input 阳性来自当时实际读取的工具输出；不能将这个后续文件当作首轮完整原始记录。新版探针使用时间戳和 PID 命名。
- [WMI 补测记录](artifacts/native-key-wmi-events.txt)
- [测试工具源码](native-key-probe.cpp)，只用于开发，不随正式 EXE 发布。
- [Microsoft：HID 亮度控制](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/display-brightness-control)
- [Microsoft：WmiMonitorBrightnessEvent](https://learn.microsoft.com/en-us/windows/win32/wmicoreprov/wmimonitorbrightnessevent)
- [Microsoft：内置显示器亮度控制及 ACPI 热键](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-brightness-controls-on-integrated-display-panels)

## 开源实现与新接口核查

2026-10-07，三个子代理分别检查 GitHub 项目、Windows ACPI/WNF 接口和本地探针。

| 路径 | 实际实现与本机意义 |
| --- | --- |
| [Twinkle Tray 原生键代码](https://github.com/xanderfrangos/twinkle-tray/blob/master/src/modules/tt-windows-utils/windows_brightness_keys.cc) / [PR 1291](https://github.com/xanderfrangos/twinkle-tray/pull/1291) | Consumer Raw Input 0x6F/0x70；与原探针路径重合。它观察按键而不阻止 Windows 调整内屏，不能直接解决本项目软件优先上调的要求。 |
| [XiControl 同型号键码表](https://github.com/Oksion/XiControl/blob/main/docs/07-keymap.md) / [WMI 监听代码](https://github.com/Oksion/XiControl/blob/main/src/Wmi/MifsEventWatcher.cs) | 通过 HID_EVENT20 接收小米厂商热键；作者明确记载屏幕亮度键不经过此厂商事件。本机补测得到一致观察：Caps Lock 有事件，原生亮度键没有该事件。 |
| [Monitorian 亮度监听](https://github.com/emoacht/Monitorian/blob/master/Source/Monitorian.Core/Models/Watcher/BrightnessWatcher.cs) | 使用亮度变化事件；无法从没有变化的 0 值推算继续按键的次数。 |
| [monctl 原生键监听](https://github.com/bacnh85/monctl/blob/main/hotkey_media_windows.go) | 同样依赖 Consumer Raw Input，未提供阻止系统内屏同步调节的实现。 |
| [Underlit 钩子源码](https://github.com/DietervanDortmont/underlit/blob/main/src/Underlit/Input/LowLevelKeyboardHook.cs) | 不能依据 README 的 Fn 支持声明采用；源码已撤销误认音量键/下一曲虚拟键的亮度处理。 |

WNF 线索为 `WNF_PO_BRIGHTNESS_HOTKEY_NOTIFICATION = 0x41C6013DA3BD5075`，公开表描述 1 为增亮、0 为减亮：[原始名称表](https://www.kokosoftware.pl/)。它是 Windows 内部接口，没有作为正式 Win32 接口的稳定性保证。

开发用 [WNF 探针](wnf-brightness-probe.cpp) 使用 [phnt 声明](https://github.com/winsiderss/phnt/blob/master/ntrtl.h)，仅查询/订阅/取消订阅，不发布状态。在沙箱及沙箱外当前用户的非管理员令牌下，导出函数均存在，但查询都返回 `0xC0000022`。该探针在查询失败后退出，没有实际订阅成功或执行物理热键验证。

进一步只读读取本机 `HKLM\SYSTEM\CurrentControlSet\Control\Notifications` 的该状态值，其访问控制描述为：

```
D:(A;;DC;;;SY)(A;;CC;;;S-1-5-80-3914275374-678031271-1603343729-3906112567-2888048264)
```

解析结果：SYSTEM 获得 mask 2；DisplayEnhancementService 服务 SID 获得 mask 1；没有普通用户或 Administrators 组授权。[Project Zero 的权限声明](https://github.com/googleprojectzero/sandbox-attacksurface-analysis-tools/blob/main/NtCoreLib/NtWnfNative.cs#L121) 定义 READ=1、WRITE=2；[Windows DLL 原始描述的导出](https://redplait.blogspot.com/2017/08/wnf-ids-from-perfntcdll.html) 将同一 bit 1 称为订阅权限。因此跳过查询没有证据能绕开该读取/订阅要求，普通管理员提权也没有相应 ACL 授权。不采用更改 ACL、服务注入或驱动来接入。

记录：[沙箱查询](artifacts/wnf-brightness-api-sandbox.txt)、[普通用户查询](artifacts/wnf-brightness-api-user.txt)。正式 EXE 没有增加 WNF、厂商 WMI 监听或键盘钩子。

## 剩余的简单折中方向

若用户接受改变 Fn Lock 的原有行为，可另行验证：将实体 F5/F6 切换为普通功能键，再用无修饰键的 RegisterHotKey 接到现有共用亮度逻辑。这样硬件到 0 后仍有普通键事件，且上调也由本程序处理，可避免原生系统动作同步加亮的问题。

代价是 Fn+F5/F6 及其他顶排键的默认行为会随 Fn Lock 改变；不能宣称保留用户原来的 Fn 语义。本轮没有切换 Fn Lock，也没有实现或验证这个模式。它是待用户选择的折中方案，不是已通过的原生亮度键接入方案。

## 保留一档硬件余量的独立实验（2026-10-07 下午）

用户同意尝试后，增加开发用 [brightness-headroom-probe.cpp](brightness-headroom-probe.cpp)，正式 EXE 未修改。该探针复用正式程序的 WMI 连接与 Gamma 数据，仅使用现有有效的 20 档 Gamma 系数。没有黑色遮罩，没有键盘拦截，没有连续轮询；定时器仅用于实验启动、自动预检步骤及三分钟结束。Ctrl+Alt+Shift+Q 可提前正常结束，退出恢复启动时的硬件亮度与 Gamma。

本机 WmiMonitorBrightness 报告 101 档：0–100，每档 1。实验将硬件保持在 1；观察到 0 则增加软件压暗一档，观察到大于 1 则撤销软件压暗一档，然后写回硬件 1。程序自身的亮度 1 通知不算按键。本探针只验证低亮度区间，没有实现完整的正常硬件范围切换，也没有验证颜色矩阵模式或长期运行性能。

### 预检

[有效预检日志](artifacts/headroom-readback-selftest-20261007-134816.txt) 中，程序主动写入 0、2、0、2，两个方向各识别两次，末尾硬件读回 100，Gamma 与原表完全一致，exit 0。这些是软件写入，不是实体键测试。

另一次预检曾错误地要求 WmiSetBrightness 返回非空输出对象，导致测试提前结束；该检测错误已修正，原日志标记为无效。WMI 写入可以异步完成，立即查询仍可能读到旧值；最终恢复检查采用一次固定 500 毫秒等待，不轮询、不重试。

### 实体按键结果

第一轮慢按与快速连按混合测试，用户表示操作失误，日志已标记无效，不据此判断漏键率。随后改为单独慢按，每次约两秒，用户确认完成，并报告：**“按 F6 提升亮度之后会回退变暗。”**

[有效慢按记录](artifacts/headroom-physical-slow-20261007-135138.txt)：

- 连续记录 5 次下降，软件档位 0 → 5；这说明保留余量确实能让本机继续产生下降事件。
- 记录 6 次上升通知，前 5 次使软件档位 5 → 0；操作说明要求 5 次，额外一次是否来自多按或按住重复没有独立按键证据，因此不宣称整轮逐键计数验收通过，也不据此宣称误报。
- 每次上升的硬件通知值均为 11，随后程序写回 1。用户观察到的先亮后暗与该变化一致。
- 11 次调节的“收到变化通知 → 收到回到 1 的通知”间隔为 187–219 毫秒。这是通知往返测量，不是对实体屏幕亮度或光学闪烁时长的测量。WMI 写入及立即读回合计约 0–16 毫秒，部分读回仍为旧亮度。
- 所有 Gamma 写入返回成功；48.672 秒收到退出快捷键。退出后硬件读回 100，Gamma 与原表完全一致，exit 0。

**结论：这条路线部分验证了最低档继续调暗的可行性，但原生 F6 的硬件先增亮、再由程序压回会造成用户可见的亮度回退，未满足平顺调亮的预期。** 既然慢按已有这一明确缺点，不再追加快速连按或性能验收，也不将该实现加入正式程序。额外重试、持续纠正或增加其他补偿机制均未添加。

正式 EXE SHA-256 保持 `4C334C81CCEA50739D5095EC18CF2C1071A6CE2228E217B842E02C5C6A4A1429`。
