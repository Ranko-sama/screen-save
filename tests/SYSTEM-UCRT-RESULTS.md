# 复用系统 UCRT 的体积优化

日期：2026-10-08。只调整链接方式，`screen-save.cpp` 未修改。

## 结果

| 项目 | 原正式版 | 本次正式版 |
| --- | ---: | ---: |
| EXE 字节数 | 104,960 | 26,112 |
| EXE 大小 | 102.5 KiB | 25.5 KiB |
| 功能代码 | 相同 | 相同 |
| VCRuntime 与 CRT 启动代码 | 静态链接 | 静态链接 |
| UCRT | 静态链接 | 使用 Windows 系统组件 |
| 额外 VC++ 运行库安装要求 | 无 | 无 |

减少 **78,848 bytes（77 KiB），约 75.1%**。没有使用压缩壳、自定义程序入口或关闭编译器保护。EXE 仍为 x64，保留原托盘图标、两种暗屏方式、快捷键、保活与恢复行为。

源码 SHA-256：`28095AE0117F991CE6AA202A242E2E1A36A6BB8D2BC6B1AF89C338CE82E6765C`，与修改链接方式前一致。

最终 EXE SHA-256：`D0659A7385B7510F706894D5AE4A6F2F63D2E1725BD0D1571CFC677E6A78BE21`。

## 构建与依赖

[build.cmd](../build.cmd) 保持 `/MT` 等原编译选项，链接参数增加：

```text
/NODEFAULTLIB:libucrt.lib ucrt.lib
```

只将 UCRT 改为通过导入库调用系统 DLL，编译器支持代码和启动代码仍留在 EXE 内。UCRT 是 Windows 10 及后续版本的系统组件，具有稳定 ABI；不能将它与额外部署的 `VCRUNTIME140.dll` 混为一谈。[微软 UCRT 说明](https://learn.microsoft.com/en-us/cpp/porting/upgrade-your-code-to-the-universal-crt)、[运行库组成](https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features)。

实际检查最终候选的 [DLL 依赖](artifacts/system-ucrt/dependencies.txt) 与 [导入函数](artifacts/system-ucrt/imports.txt)：只有原先的 Windows 系统 DLL 和 `api-ms-win-crt-*` 系统 UCRT 接口；没有 `VCRUNTIME140.dll`、`MSVCP140.dll` 或随程序部署的第三方 DLL。

对照 [原 PE 配置](artifacts/system-ucrt/baseline-pe-config.txt) 与 [新 PE 配置](artifacts/system-ucrt/pe-config.txt)，两者都含高熵 ASLR、动态基址、NX 标志和安全 Cookie；没有通过禁用保护来缩小体积。

## 实际 EXE 回归

依次测试独立候选进程，四套工具的退出码均为 0，候选正常退出。见 [汇总](artifacts/system-ucrt/regression.json)。

| 测试 | 覆盖 | 结果 |
| --- | --- | --- |
| toggle-probe | 双模式、硬件亮度、连续切换、暗屏退出、保活与恢复 | [FAILURES=0](artifacts/system-ucrt/toggle.txt) |
| review-probe | 会话颜色、非默认 Gamma、模式交接、重复实例与快捷键占用 | [FAILURES=0](artifacts/system-ucrt/adversarial.txt) |
| cursor-display-probe | 实际光标隐藏与恢复、静止恢复、位置与焦点保持 | [FAILURES=0](artifacts/system-ucrt/cursor.txt) |
| menu-hotkey-probe | 实际 Ctrl+Alt+B、原生菜单选择、取消菜单与焦点 | [FAILURES=0](artifacts/system-ucrt/menu.txt) |

测试前硬件亮度为 30，测试工具修改后已恢复到 30。通过测试的同一候选文件复制为 `dist/ScreenSave.exe` 并启动。[启动状态](artifacts/system-ucrt/release-state.txt) 显示颜色矩阵与 Gamma 正常，执行请求为 0。新文件与原文件信息及一次正常状态下的 30 秒空闲采样见 [release.json](artifacts/system-ucrt/release.json)。

测试只在当前笔记本进行，没有在全新 Windows 10／11 虚拟机或其他硬件上实际验收。系统运行库依赖的适用性依据微软文档和最终导入表；原有 WMI 亮度接口、Gamma 和颜色矩阵的硬件／驱动限制仍然存在。

本次没有重做 15 分钟持续测试或 DWM/GPU 基准，不把文件缩小等同于渲染开销下降。之前的代码消融实验仍对应其原构建，历史报告未改写。

原 102.5 KiB 文件保留在 `build/size/ScreenSave-before-system-ucrt.exe`，哈希见 [baseline.json](artifacts/system-ucrt/baseline.json)。这是本地回退副本，不随程序发布。未创建 Git 提交。
