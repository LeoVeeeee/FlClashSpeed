# 第三方版权与许可

本仓库的 [MIT 许可](LICENSE) 适用于自行编写的插件、脚本和文档，不重新许可以下第三方内容。分发源码或二进制时，请保留相关版权、许可与本声明。

| 组件 | 来源 / 版本 | 许可副本 |
|---|---|---|
| TrafficMonitor 插件接口 | Zhong Yang，官方 V1.86 / API 7；`sdk/PluginInterface.h` | [Anti 996](LICENSE.TrafficMonitor)，SDK 中保留原始版权头与许可副本 |
| nlohmann/json | Niels Lohmann，3.12.0；`third_party/json.hpp` | [MIT](LICENSE.nlohmann-json)，头文件保留 SPDX 版权声明 |
| Zig compiler-rt / 运行库代码 | Zig contributors，Zig 0.15.2 工具链 | [Zig MIT](LICENSES/Zig-MIT.txt) |
| LLVM libc++ | LLVM Project，Zig 0.15.2 随附版本 | [Apache 2.0 with LLVM Exceptions / 历史许可](LICENSES/LLVM-libcxx.txt) |
| LLVM libc++abi | LLVM Project，Zig 0.15.2 随附版本 | [许可](LICENSES/LLVM-libcxxabi.txt) |
| LLVM libunwind | LLVM Project，Zig 0.15.2 随附版本 | [许可](LICENSES/LLVM-libunwind.txt) |
| MinGW-w64 CRT | mingw-w64 project，Zig 0.15.2 随附版本 | [ZPL 2.1 与上游文件声明的例外](LICENSES/MinGW-w64.txt) |

Zig 发布构建可能静态链接上述运行库的部分代码，因此许可副本同时包含在发布 ZIP 和源码仓库中。工具链自身、Windows 系统 DLL 和其他项目的可执行程序不包含在发布包内。使用其他工具链重新分发时，应检查该工具链的运行库与许可要求。

TrafficMonitor 和 FlClash 名称仅用于说明兼容性，不表示上游项目的认可、担保或隶属关系。
