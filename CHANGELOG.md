# 更新记录 / Changelog

[简体中文首页](README.md) · [English home](README.en.md)

这里记录可感知的功能变化和修复。版本号以 `CMakeLists.txt`、Windows 资源和应用清单为准。

This file records user-visible changes. Version metadata is defined in `CMakeLists.txt`, the Windows resources, and the application manifest.

## 0.9.1 — 2026-11-18 · Preview / 预览版

当前本地预览版本，尚未发布为 GitHub Release。 / Current local preview; not yet published as a GitHub Release.

### 裁切与透明输出 / Cropping and transparency

- 抠图选好后可直接拖动蓝色边框和控制点裁切，保留已有选择与编辑。
- 新增「收紧到选区」，按活动选区或已保留图片的非透明内容裁掉四周空白。
- 校验实际保存的 PNG 尺寸，覆盖单像素内容、空画布和内部透明区域。
- Resize the blue capture border directly after selecting content, preserving selections and edits.
- Trim to selected or nontransparent content with **收紧到选区**.
- Verify saved PNG dimensions, including one-pixel content, empty canvases, and interior transparency.

### 已包含的主要能力 / Included capabilities

- 截图标注、圆形拖动选色、累积 / 减选、整笔撤销、独立大小与容差滑条。
- 连续 / 全图选色、局部边界识别、柔边、保留 / 移除、Alpha 保存与复制。
- 区域 GIF、系统托盘、全局快捷键、隐藏主面板截图、Esc 一次退出截图。
- Screenshot annotations, circular selection strokes, accumulation/subtraction, stroke undo, and separate size/tolerance sliders.
- Contiguous/global matching, local edge checks, feathering, Keep/Remove, and PNG alpha preservation.
- Region GIF recording, tray integration, global shortcuts, hide-on-capture, and one-press Esc cancellation.

### 发布准备 / Repository preparation

- 增加中英文首页与指南、MIT 许可、贡献和安全说明、反馈模板及发布文档。
- Add bilingual home pages and guides, MIT licensing, contribution/security documentation, feedback templates, and release instructions.
