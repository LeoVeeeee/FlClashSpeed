# FlClashSpeed

**在 Windows 任务栏中显示 FlClash 的实时上传、下载速度。**

FlClashSpeed 是 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的原生 C++ 插件。上传和下载作为两个独立项目，由 TrafficMonitor 统一处理字体、颜色、透明背景和 DPI 缩放，无需额外启动独立程序或安装 .NET。

[下载发布版](https://github.com/LeoVeeeee/FlClashSpeed/releases/latest) · [安装](#安装) · [常见问题](#常见问题) · [从源码构建](#从源码构建) · [更新记录](CHANGELOG.md)

## 功能

- 显示 `VPN↑` 上传和 `VPN↓` 下载速度，可在宿主中修改标签。
- 自动读取 FlClash 控制器地址与密钥，包括界面保存在 `shared_preferences.json` 中的覆盖设置。
- 自动切换 KB/s、MB/s，并根据数值大小调整小数位。
- 断线显示 `—`，真实空闲显示 `0.00 KB/s`；后台自动重连。
- 提供连接设置和“立即重新连接”操作。

> **统计范围：** FlClash 内核处理的流量，**包含 DIRECT 直连**。它不等于整台电脑所有网卡流量，也不只统计远程代理流量。

## 兼容性

| 项目 | 要求 / 验证范围 |
|---|---|
| 系统 | Windows 10 1607 或更新版本、Windows 11 |
| 宿主 | TrafficMonitor 1.86 的官方插件 API 7；更高版本需继续保留 API 7 兼容性 |
| 架构 | x64、x86；插件位数必须与 **TrafficMonitor** 一致 |
| FlClash | 外部控制器支持 `GET /traffic`；2.0.0 已在 FlClash 0.8.91 上验证连接 |
| 自动发现 | 标准 AppData 路径；便携版可手动选择配置文件 |

发布测试包括 32/64 位诊断宿主的 DLL 加载与接口调用、模拟控制器流量和故障恢复。它们不能代替所有 TrafficMonitor 版本、主题和 DPI 下的视觉测试。

## 安装

1. 在 FlClash 中开启 **外部控制器**，通常位于“配置 → 常规”。
2. 从 [Releases](https://github.com/LeoVeeeee/FlClashSpeed/releases/latest) 下载插件 ZIP，并解压。
3. **完全退出 TrafficMonitor**。64 位宿主使用 `x64/FlClashSpeedPlugin.dll`，32 位宿主使用 `x86/FlClashSpeedPlugin.dll`。
4. 将对应 DLL 放入 `TrafficMonitor.exe` 同级的 `plugins` 文件夹；没有该文件夹时新建。保留发布包附带的许可证和第三方声明。
5. 重新启动 TrafficMonitor，在 **任务栏窗口右键 → 显示项目** 中勾选 **FlClash 上传**、**FlClash 下载**。

目录示例：

```text
TrafficMonitor/
├── TrafficMonitor.exe
└── plugins/
    └── FlClashSpeedPlugin.dll
```

不要把两个架构的 DLL 同时放入同一个 `plugins` 文件夹。插件加载状态和选项位于 **其他功能 → 插件管理**。字体、背景、显示位置和布局请在 TrafficMonitor 中设置。

更新时退出宿主，再替换 DLL。卸载时退出宿主并删除 DLL；如需清除个人设置，再删除插件配置文件。

## 连接设置

### 自动连接

通常无需填写设置。插件在 `%APPDATA%`、`%LOCALAPPDATA%` 中查找：

```text
com.follow/clash/config.yaml
com.follow/flclash/config.yaml
```

读取 YAML 顶层的 `external-controller`、`secret`，再使用相邻 `shared_preferences.json` 中 `flutter.config → patchClashConfig` 的地址、密钥覆盖设置。地址为空时尝试 `127.0.0.1:9090`。

YAML 读取器仅支持上述两个顶层标量，不是完整 YAML 解析器；使用 YAML 锚点、复杂标量或自定义结构时，请采用手动连接。

### 手动连接

双击插件数值，或从插件管理中打开连接选项：

| 字段 | 用法 |
|---|---|
| FlClash 配置文件 | 便携版或自定义目录使用；留空自动发现 |
| 手动控制器地址 | 例如 `http://127.0.0.1:9090`；留空读取配置 |
| 手动密钥 | 与手动地址对应；不覆盖自动发现的密钥 |

手动地址优先于配置文件。只能连接本机回环地址，不支持远程控制器；HTTP、HTTPS 均可，HTTPS 使用系统证书验证。不要填写代理端口（常见为 7890），也不要在地址后追加 `/traffic`。

**保存并连接**会应用输入内容；**立即重新连接**使用当前已保存的设置重新尝试，是按钮操作，不需要勾选。

### 配置与隐私

插件配置为宿主提供的配置目录下的 `plugins/FlClashSpeedPlugin.ini`。AppData 模式下通常位于 `%APPDATA%\TrafficMonitor\plugins`；便携模式以宿主提供的目录为准。

手动密钥使用 Windows DPAPI 加密，仅同一 Windows 用户可解密。自动发现的密钥不另存。插件仅向本机控制器发送只读 `GET /traffic` 请求，不上传遥测，不修改 FlClash 配置或启停 VPN。

不要将个人 INI、FlClash 配置、密钥、订阅地址或原始日志提交到仓库或 Issue。仓库的 `.gitignore` 已排除常见个人配置与构建产物。

## 网速格式

沿用常见网速工具的标记：`1 KB = 1024 字节`，`1 MB = 1024 KB`，达到 `1024 KB/s` 切换为 MB/s。

| 当前单位下的数值 | 小数位 | 示例 |
|---|---|---|
| 小于 10 | 2 位 | `1.23 KB/s`、`1.50 MB/s` |
| 10 至小于 100 | 1 位 | `12.3 KB/s`、`12.3 MB/s` |
| 100 及以上 | 最多 2 位，去掉末尾零 | `123.45 KB/s`、`123 KB/s` |

舍入后重新判断位数，不补前导零。显示宽度按固定示例 `9999.99 MB/s` 预留，不随实时数值反复伸缩；极端速率可能超出预留宽度。

## 常见问题

| 现象 | 检查步骤 |
|---|---|
| 插件未出现在列表中 | 确认 DLL 在宿主同级 `plugins` 文件夹，架构匹配，并完全退出后重启宿主 |
| 已加载，但任务栏没有数字 | 在任务栏窗口的“显示项目”中勾选两个 FlClash 项目 |
| 显示 `—` / 无法连接 | 开启外部控制器，检查地址、端口和 FlClash 是否运行；便携版手动选择配置文件 |
| 密钥不匹配 | 核对控制器密钥；手动地址需填写对应手动密钥 |
| 保存的密钥损坏 / 更换 Windows 用户 | 打开连接选项，重新填写并保存；插件不会静默回退到其他控制器 |
| 字体、透明度或位置不合适 | 在 TrafficMonitor 中调整任务栏窗口设置 |
| 下载显示 0，但电脑仍在传输 | 只有 FlClash 内核处理的流量才会计入 |

5 秒未收到有效记录时显示离线。连接失败后等待约 3 秒重试。重连按钮会立即更新状态，旧的同步 HTTP 请求由后台线程在超时后结束，因此新连接可能需要数秒开始。

## 从源码构建

### Zig（发布构建）

需要 Windows x64 构建机、Python 3.10+ 和首次下载所需的网络。运行目录为仓库根目录：

```powershell
python -m pip install -r requirements-dev.txt
python tools/bootstrap.py
python tools/build.py --arch all
python tests/integration.py
python tools/package.py
```

`bootstrap.py` 下载官方 Zig 0.15.2 并验证官方 SHA256。JSON 头文件已随源码提供。输出 DLL 在 `dist/x64`、`dist/x86`，ZIP 和校验文件在 `dist/release`；源码包不包含编译器、缓存或个人配置。

### Visual Studio / CMake

需要 CMake 3.20+ 和 Visual Studio C++ 桌面开发工具：

```powershell
cmake -S . -B build-msvc -A x64
cmake --build build-msvc --config Release
```

32 位使用 `-A Win32`。该构建入口已提供；发布资产使用 Zig 构建，MSVC 构建兼容性需单独验证。

### 测试与发布

[CI](.github/workflows/build.yml) 在 Windows 上构建两个架构并运行本机模拟控制器测试，覆盖 DLL 导出和 API 7、数值边界、UTF-16 配置与 DPAPI、JSON 分片、认证失败、超时、重连和远程地址拒绝。测试不依赖个人 FlClash 设置。

发布 ZIP 包含 DLL、说明、许可证和 SHA256；对应源码使用同版本 Git 标签。版本变化见 [CHANGELOG.md](CHANGELOG.md)，本次审查与验证记录见 [代码审查记录](docs/CODE_REVIEW.md)。

## 贡献与问题反馈

欢迎通过 [Issues](https://github.com/LeoVeeeee/FlClashSpeed/issues) 报告问题或提交 Pull Request。请附 Windows、TrafficMonitor、FlClash 版本和宿主位数，并提供复现步骤；共享截图或日志前删除个人配置和密钥。构建和测试方法见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可与致谢

本项目自行编写的代码采用 [MIT 许可](LICENSE)。**第三方代码保留各自许可，MIT 不替代这些条件。**

- TrafficMonitor API 7：Zhong Yang，保留原始接口版权声明和 [Anti 996 许可](LICENSE.TrafficMonitor)。
- nlohmann/json 3.12.0：Niels Lohmann，[MIT 许可](LICENSE.nlohmann-json)。
- Zig 构建可能静态链接的 C++ / 编译器运行库：许可副本随发布包提供，见 [第三方声明](THIRD_PARTY_NOTICES.md)。

本项目为独立第三方插件，与 TrafficMonitor、FlClash 官方无隶属关系。接口实现参考 [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/插件开发指南)和 [1.86 官方接口](https://github.com/zhongyang219/TrafficMonitor/blob/V1.86/include/PluginInterface.h)。
