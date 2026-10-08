# 贡献指南

请先阅读 [README](README.md) 的构建、统计范围和兼容性说明。

提交修改前运行：

```powershell
python -m pip install -r requirements-dev.txt
python tools/bootstrap.py
python tools/build.py --arch all
python tests/integration.py
```

代码修改应包含能复现问题的测试，并保持 x64、x86 构建通过。文档修改请核对相对链接、安装步骤和许可文件引用。

Pull Request 请说明具体问题、修改后的行为、验证结果和已知限制。不要修改官方 SDK 的虚函数顺序或接口版本；不要在绘制回调或 `DllMain` 中发起网络请求或等待线程。

不要提交编译器、缓存、DLL、个人 INI、订阅信息、密钥或 FlClash 配置。发布资产通过构建和打包脚本生成，第三方版权与许可必须保留。
