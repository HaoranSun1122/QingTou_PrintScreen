# 首次发布与版本打包 / Publishing

[简体中文](#简体中文) · [English](#english)

## 简体中文

仓库链接目前按 `HaoranSun1122/PrintScreen` 配置，与本地仓库名称对应。如果发布时更换 owner 或仓库名，请同步更新首页、反馈入口、模板和本指南中的链接。

### 1. 发布源码仓库

1. 在 GitHub Desktop 选择本项目仓库，确认 `README.md`、`README.en.md`、`LICENSE`、源码、资源、测试和 `.github/` 均在变更列表中。
2. 检查待提交内容：不应包含 `build/`、`dist/`、`captures/`、`test-output/`、`*.ini` 或私人截图。
3. 提交初始源码与文档。
4. 点击 **Publish repository**，名称使用 `PrintScreen`；如要公开开源，选择公开仓库。
5. 发布后打开仓库的 Actions 页面，确认 **Windows build** 构建和默认测试通过。

本指南不要求在源码仓库中提交 exe。Windows 用户下载包放在 Release 中。

### 2. 生成可下载包

使用 x64 工具链从干净目录构建并测试：

```powershell
.\build.ps1
.\tools\package_release.ps1
```

生成文件：

```text
dist/
  QingTou-v0.9.1-windows-x64.zip
  QingTou-v0.9.1-source.zip
  SHA256SUMS.txt
```

版本号自动从 CMake 读取；更改版本后同步资源与清单。压缩包包含 MIT 许可、第三方说明和双语使用指南。运行库的完整再分发文本应随实际采用的工具链一并核对和保留。

在没有开发环境的 Windows x64 电脑或测试虚拟机中解压运行，验证截图、透明 PNG、收紧边界、剪贴板、GIF、热键与托盘退出。多屏、缩放、HDR 等未覆盖项要在发行说明中如实写明。

### 3. 创建第一个 Release

1. 确认当前工作区的修改已提交，CMake / RC / manifest 版本一致。
2. 在 GitHub 的 Releases 创建 `v0.9.1` 对应的 tag，标题可以是 `轻透截图 0.9.1 Preview`。
3. 首发可标记为预发布版本，引用 `CHANGELOG.md` 中的变化与已知边界。
4. 上传 Windows zip 和 `SHA256SUMS.txt`；可以附带本地源码 zip，GitHub 也会生成 tag 对应的源码归档。
5. 检查 README 中的下载链接，并将「首个 Release 尚未发布」更新成实际的发行状态。
6. 在仓库设置中按需要启用私密漏洞报告，使 `SECURITY.md` 的报告入口可用。

不要把本机 `QingTou.ini`、GIF、用户截图或构建缓存附在 Release 中。图标已经嵌入 exe，运行不依赖本机开发工具。

### 更新后续版本

- 先更新代码与测试，再同步 `CHANGELOG.md` 和中英文说明。
- 同步 `CMakeLists.txt`、`resources/app.rc`、`resources/app.manifest` 的版本号，以及首页版本标识。
- 使用新构建重新打包；脚本会拒绝版本或架构不一致的 exe。
- 软件没有自动更新功能，用户更新前应从托盘退出旧版。

## English

Links currently target `HaoranSun1122/PrintScreen`. Update them if the published owner or repository name differs.

1. Review and commit source, assets, tests, documentation, licensing, and `.github/` in GitHub Desktop. Exclude all captures, settings, build files, and binaries.
2. Publish the repository, then confirm **Windows build** passes in Actions.
3. Build and test with an x64 toolchain. Run `.\tools\package_release.ps1` to create versioned Windows/source zip files and `SHA256SUMS.txt`.
4. Check the specific toolchain's runtime redistribution terms and retain its required notices. Smoke-test the portable zip on a Windows machine without a development environment.
5. Create the matching version tag and a preview release. Attach the Windows zip and checksums, and describe validation limits honestly.
6. Update the README's release-availability notice. Enable private vulnerability reporting if you want to use that channel.

Keep `CMakeLists.txt`, Windows resource/manifest versions, the changelog, and both home pages aligned for future releases. QingTou currently has no auto-update feature; exit the old instance before replacing files.
