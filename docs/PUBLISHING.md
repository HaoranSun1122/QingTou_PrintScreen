# 版本打包与发布 / Publishing

[简体中文](#简体中文) · [English](#english)

## 简体中文

源码仓库已上线：[HaoranSun1122/QingTou_PrintScreen](https://github.com/HaoranSun1122/QingTou_PrintScreen)。发布操作应针对关联这个远程地址的本地 Git 工作目录。

### 1. 提交发布前的修正

1. 在 GitHub Desktop 选择本项目仓库，使用 **Repository → Show in Explorer** 确认目录中有本项目的源码和文档。
2. 检查待提交内容：不应包含 `build/`、`dist/`、`captures/`、`test-output/`、`*.ini` 或私人截图。
3. 提交发布准备的变更，例如 `Prepare v0.9.2 preview release`。
4. 点击 **Push origin**，将变更推送到 `main`。
5. 打开 [Actions](https://github.com/HaoranSun1122/QingTou_PrintScreen/actions)，确认这次提交的 **Windows build** 构建、默认测试和打包全部通过。失败的旧提交不能证明新版本已验证。

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
  QingTou.exe
  QingTou-v0.9.2-windows-x64.zip
  QingTou-v0.9.2-source.zip
  SHA256SUMS.txt
```

版本号自动从 CMake 读取；更改版本后同步资源与清单。压缩包包含 MIT 许可、第三方说明和双语使用指南。运行库的完整再分发文本应随实际采用的工具链一并核对和保留。

在没有开发环境的 Windows x64 电脑或测试虚拟机中解压运行，验证截图、透明 PNG、收紧边界、剪贴板、GIF、热键与托盘退出。多屏、缩放、HDR 等未覆盖项要在发行说明中如实写明。

### 3. 创建当前版本的 Release

1. 确认当前工作区的修改已提交并推送，CMake / RC / manifest 版本一致，`main` 最新提交的 Actions 已通过；发布包应来自同一份源码。
2. 打开 [新建 Release](https://github.com/HaoranSun1122/QingTou_PrintScreen/releases/new)。在 **Choose a tag** 输入 `v0.9.2`，点击 **Create new tag**，**Target** 选择 `main`。
3. **Release title** 填写 `轻透截图 0.9.2 Preview`；正文可复制 [当前版本发行说明](releases/v0.9.2.md) 的内容。
4. 在附件区域上传 `dist/QingTou.exe`、`dist/QingTou-v0.9.2-windows-x64.zip` 和 `dist/SHA256SUMS.txt`；可一并上传 `dist/QingTou-v0.9.2-source.zip`。发行说明优先给出 EXE 下载链接，说明双击即可使用；完整 ZIP 另含指南和许可文件。GitHub 自动生成的 **Source code** 是源码，不能直接运行。
5. 等所有附件上传完成，勾选 **This is a pre-release**。可先点 **Save draft** 检查，确认后点 **Publish release**，此时才会公开这个版本。操作参考 [GitHub 官方指南](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)。
6. 发布后从 [Releases](https://github.com/HaoranSun1122/QingTou_PrintScreen/releases) 实际下载 EXE 和完整 ZIP，确认均能运行；再将中英文 README 和 CHANGELOG 的「尚未发布」说明更新为实际发行状态。
7. 在仓库设置中按需要启用私密漏洞报告，使 `SECURITY.md` 的报告入口可用。

不要把本机 `QingTou.ini`、GIF、用户截图或构建缓存附在 Release 中。图标已经嵌入 exe，运行不依赖本机开发工具。

### 更新后续版本

- 先更新代码与测试，再同步 `CHANGELOG.md` 和中英文说明。
- 同步 `CMakeLists.txt`、`resources/app.rc`、`resources/app.manifest` 的版本号，以及首页版本标识。
- 使用新构建重新打包；脚本会拒绝版本或架构不一致的 exe。
- 软件没有自动更新功能，用户更新前应从托盘退出旧版。

## English

The source is published at [HaoranSun1122/QingTou_PrintScreen](https://github.com/HaoranSun1122/QingTou_PrintScreen). Use the local Git checkout associated with this remote repository.

1. Review and commit source, assets, tests, documentation, licensing, and `.github/` in GitHub Desktop. Exclude all captures, settings, build files, and binaries.
2. Commit the release preparation changes and **Push origin**, then confirm **Windows build** passes for the latest `main` commit in Actions.
3. Build and test with an x64 toolchain. Run `.\tools\package_release.ps1` to create versioned Windows/source zip files and `SHA256SUMS.txt`.
4. Check the specific toolchain's runtime redistribution terms and retain its required notices. Smoke-test the portable zip on a Windows machine without a development environment.
5. Open [New release](https://github.com/HaoranSun1122/QingTou_PrintScreen/releases/new), create tag `v0.9.2` targeting the verified `main` commit, and title it `轻透截图 0.9.2 Preview`. Copy the [release notes](releases/v0.9.2.md), attach `QingTou.exe`, the full Windows zip, and checksums (optionally the source zip), and select **This is a pre-release**. Lead the download instructions with the EXE link and explain that users can double-click it to run; the full ZIP also includes guides and licensing. Wait for uploads to finish before **Publish release**; **Save draft** lets you review first. GitHub's automatic source archives are not runnable Windows packages.
6. Download and check the published EXE and full Windows zip. Update both READMEs and the changelog to reflect release availability. Enable private vulnerability reporting if you want to use that channel.

Keep `CMakeLists.txt`, Windows resource/manifest versions, the changelog, and both home pages aligned for future releases. QingTou currently has no auto-update feature; exit the old instance before replacing files.
