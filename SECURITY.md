# 安全与隐私 / Security and privacy

[简体中文](#简体中文) · [English](#english)

## 简体中文

### 数据处理

当前实现不要求账号，也没有上传截图、上传 GIF、遥测或自动更新功能。截图和抠图处理在本机执行。

- 截图与选区在编辑时保存在进程内存中；复制操作会写入 Windows 剪贴板。
- PNG 保存在用户选择的位置；GIF 自动写入 exe 旁的 `captures/`。
- 快捷键和隐藏窗口设置保存在 exe 旁的 `QingTou.ini`。
- 关闭面板只隐藏到托盘；从托盘退出才结束进程。
- 系统剪贴板、云盘同步目录及其他软件可能有自己的数据同步行为。

### 报告漏洞

请优先使用仓库 [Security 页面](https://github.com/HaoranSun1122/QingTou_PrintScreen/security)的私密漏洞报告入口（由维护者启用后可用）。如果入口尚未启用，可以发一个不包含漏洞细节或私人数据的 Issue，请维护者提供私下联系方式。

请说明受影响版本、影响范围和最小复现方式。未修复前，请勿在公开 Issue 中发布敏感截图、秘密信息或可直接利用的详细步骤。项目目前优先处理最新 `0.9.x` 预览版本的问题，不承诺固定响应时限。

## English

### Data handling

The current implementation requires no account and contains no screenshot/GIF upload, telemetry, or automatic-update feature. Capture and cutout processing run locally.

- Captures and selections stay in process memory while editing; copying writes to the Windows clipboard.
- PNGs are saved to the chosen location; GIFs are saved to `captures/` beside the executable.
- Hotkeys and the hide-window preference are stored in `QingTou.ini` beside the executable.
- Closing the panel hides it to the tray. Exiting from the tray ends the process.
- The system clipboard, synced folders, and other applications may have their own synchronization behavior.

### Report a vulnerability

Prefer private vulnerability reporting from the repository's [Security page](https://github.com/HaoranSun1122/QingTou_PrintScreen/security), once enabled by the maintainer. If it is unavailable, open an issue without sensitive details and ask for a private contact channel.

Include affected versions, impact, and a minimal reproduction. Do not publish private captures, secrets, or detailed exploitation steps in public issues before a fix. The project prioritizes the latest `0.9.x` preview and does not promise a fixed response time.
