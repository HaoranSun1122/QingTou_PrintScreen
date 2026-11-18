# 参与贡献 / Contributing

[简体中文](#简体中文) · [English](#english)

## 简体中文

欢迎提交问题复现、文档修正、翻译和代码改进。

### 反馈问题

使用[问题模板](https://github.com/HaoranSun1122/QingTou_PrintScreen/issues/new/choose)，提供：

- 软件版本、Windows 版本、屏幕数量及每块屏幕的缩放比例。
- 最短复现步骤、预期结果与实际结果。
- 与问题相关的截图或小型测试图片；先移除私人信息。

安全漏洞请按 [SECURITY.md](SECURITY.md) 私下报告。

### 修改与验证

1. Fork 仓库，为修改建立独立分支。
2. 按[构建指南](docs/BUILDING.md)配置 C++20 工具链。
3. 修改后运行 `.\build.ps1`。提交算法或行为修复时，增加能够复现原问题的测试。
4. UI 修改补充前后截图。默认测试不读取桌面；需要桌面采集测试时单独启用。
5. 提交 Pull Request，说明问题、最终行为和验证结果。中文或英文均可。

### 项目约定

- 保持工具轻量：优先使用现有 Win32 / Direct2D / WIC 能力；新增依赖前说明用途与体积影响。
- 用户截图、GIF、设置、测试输出、构建缓存和二进制不提交到源码仓库。
- 修改版本时同步 `CMakeLists.txt`、`resources/app.rc` 与 `resources/app.manifest`。
- 改用户行为时同步中英文文档和 `CHANGELOG.md`；不要求每次内部重构都更新版本。
- 保留已有文件风格。文档使用 UTF-8，链接使用仓库相对路径。
- 提交的贡献按本项目 [MIT License](LICENSE) 提供；请勿提交无权再分发的代码、字体或图片。

## English

Bug reproductions, documentation fixes, translations, and code improvements are welcome.

### Report a problem

Use an [issue template](https://github.com/HaoranSun1122/QingTou_PrintScreen/issues/new/choose) and include:

- App and Windows versions, monitor count, and per-monitor scaling.
- Minimal reproduction steps, expected behavior, and actual behavior.
- Relevant screenshots or small test images, with private information removed.

Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md#english).

### Change and validate

1. Fork the repository and create a branch for your change.
2. Configure a C++20 toolchain using the [build guide](docs/BUILDING.md#english).
3. Run `.\build.ps1`. For algorithm or behavior fixes, add a regression test that reproduces the original problem.
4. Include before/after screenshots for UI changes. Default tests do not read the desktop; enable desktop capture tests separately when needed.
5. Open a pull request describing the problem, final behavior, and validation. Chinese and English are both welcome.

### Project conventions

- Keep the tool small. Prefer existing Win32 / Direct2D / WIC facilities; explain the purpose and size impact of new dependencies.
- Do not commit user captures, GIFs, settings, test output, build caches, or binaries.
- Keep version metadata aligned across `CMakeLists.txt`, `resources/app.rc`, and `resources/app.manifest`.
- Update both language guides and the changelog for user-visible changes.
- Follow nearby file style. Use UTF-8 documentation and repository-relative file links.
- Contributions are provided under the project's [MIT License](LICENSE). Only submit material you have the right to redistribute.
