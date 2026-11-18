<p align="center">
  <img src="resources/brand/icon.png" alt="轻透截图 Logo" width="88" height="88">
</p>

<h1 align="center">轻透截图 <sub>QingTou</sub></h1>

<p align="center">截图 · 透明 PNG · GIF 录制</p>

<p align="center">
  <strong>简体中文</strong> · <a href="README.en.md">English</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4?style=flat-square" alt="Windows 10 / 11">
  <img src="https://img.shields.io/badge/C%2B%2B-20-555555?style=flat-square" alt="C++20">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-18A999?style=flat-square" alt="MIT License"></a>
  <img src="https://img.shields.io/badge/Preview-0.9.1-555555?style=flat-square" alt="0.9.1 Preview">
</p>

<p align="center">
  <a href="#下载与运行">下载与运行</a> ·
  <a href="docs/USAGE.md">使用指南</a> ·
  <a href="docs/BUILDING.md">从源码构建</a> ·
  <a href="CONTRIBUTING.md">参与贡献</a>
</p>

---

**轻透截图**是一个原生 Windows 截图工具。日常截图后可以标注，也可以用圆形笔刷提取 Logo、图标等内容，保存为透明 PNG，再把多余空白一键收紧。

不需要登录，图像处理在本机完成。界面使用系统中性色，跟随 Windows 应用浅色 / 深色设置；当前软件界面为中文，项目文档提供中英文版本。

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/interface-dark.png">
    <img src="docs/interface-light.png" alt="轻透截图主界面" width="568">
  </picture>
</p>

## 能做什么

| 日常截图 | 透明 PNG | 区域 GIF |
| --- | --- | --- |
| 框选、矩形、箭头、画笔、文字、马赛克、撤销 | 圆形拖选、多次累积、Alt 减选、透明导出、裁掉空白 | 框选录制、快捷键停止、自动保存并复制为文件 |

- **魔棒抠图**：按相似颜色选取，可处理近似纯色背景和轻度渐变、阴影、压缩杂色。
- **连续涂选**：按住左键拖动圆圈，经过的颜色扩展为相似区域；一笔可整体撤销。
- **收紧到选区**：选好内容后直接拖蓝色边框裁切，或一键让 PNG 画布贴合主体。
- **保留透明度**：PNG 保存与复制保留 Alpha；棋盘格和蓝色选区只用于预览。
- **随手调用**：系统托盘、自定义全局快捷键、截图时隐藏窗口、单实例运行。
- **原生实现**：C++20、Win32、Direct2D / DirectWrite、Windows WIC，无需额外图像处理服务。

## 下载与运行

**当前源码版本为 `0.9.1` 预览版，首个 GitHub Release 尚未发布。** 发布后的 Windows 包将放在 [Releases](https://github.com/HaoranSun1122/QingTou_PrintScreen/releases)。现在也可以按[构建指南](docs/BUILDING.md)自行编译。

1. 下载 Windows x64 压缩包，解压到有写入权限的文件夹。
2. 运行 `QingTou.exe`；使用软件不需要安装编译器或 Python。
3. 更新前先从托盘退出旧版，再替换文件。

面向 **Windows 10 2004+ / Windows 11 的 SDR 桌面**。关闭主面板后软件留在托盘，右键托盘图标可以退出。设置保存在 exe 旁的 `QingTou.ini`，GIF 默认保存在 `captures/`。

## 三步做一张透明 PNG

**框选 → 魔棒抠图 → 收紧并保存**

1. 按 `Ctrl + Alt + Q` 框选包含 Logo 的画面，打开「魔棒抠图」。
2. 沿主体内部拖动圆圈，或依次点击不同颜色，累计选中所需部分。误选时用 `Alt` 减选，`Ctrl + Z` 撤销。
3. 点「保留」去掉背景，再点「收紧到选区」裁掉透明空白，最后保存 PNG。选区尚未应用时，直接保存也会保留所选部分。

![魔棒抠图工具栏：容差、圆圈大小、保留与收紧到选区](docs/toolbar-cutout.png)

也可以点击背景后选择「移除」。圆圈大小与容差分别控制涂选范围和颜色匹配；详情见[完整使用指南](docs/USAGE.md)。

## 常用快捷键

| 操作 | 快捷键 |
| --- | --- |
| 截图 / 抠图 | `Ctrl + Alt + Q` |
| 开始 / 停止 GIF | `Ctrl + Alt + G` |
| 魔棒抠图 | `W` |
| 复制 / 保存 PNG | `Enter` / `Ctrl + S` |
| 撤销一次选点或整笔涂选 | `Ctrl + Z` |
| 减选 / 重新选择 | `Alt + 点击或拖动` / `Ctrl + 点击或拖动` |
| 调整圆圈大小 | 「圆圈」滑条、滚轮、`[` / `]` |
| 退出当前截图 | `Esc` |

## 当前边界

- 抠图依据颜色和连通性。主体与背景同色、强阴影、复杂照片、发丝、玻璃等仍可能需要手动追加 / 减选。
- GIF 不含声音，目标 10 帧/秒，最长 60 秒，宽度最多 1280 像素；实际帧率受画面与设备性能影响。
- 部分软件粘贴图片时会丢失透明度，遇到这种情况请保存 PNG 后以文件方式导入。
- HDR、远程桌面、受保护内容和混合 DPI 多屏仍需进一步验证。

## 开发与贡献

```powershell
.\build.ps1
```

需要 Windows、CMake 3.20+ 和 C++20 工具链。默认测试不读取用户桌面；桌面采集测试单独启用。

[构建与测试](docs/BUILDING.md) · [贡献指南](CONTRIBUTING.md) · [更新记录](CHANGELOG.md) · [发布指南](docs/PUBLISHING.md)

发现问题时，请提交 Windows 版本、缩放比例、软件版本和复现步骤；[问题反馈模板](https://github.com/HaoranSun1122/QingTou_PrintScreen/issues/new/choose)已包含这些字段。截图中请移除私人信息。安全问题请参考 [SECURITY.md](SECURITY.md)。

## 许可与资源

项目代码、文档及仓库内提供的品牌资源按 [MIT License](LICENSE) 发布。第三方组件保留其各自许可，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。图标来源和重新打包方式见[品牌资源说明](resources/brand/README.md)。
