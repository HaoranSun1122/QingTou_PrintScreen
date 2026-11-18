# 更新记录 / Changelog

[简体中文首页](README.md) · [English home](README.en.md)

这里记录可感知的功能变化和修复。版本号以 `CMakeLists.txt`、Windows 资源和应用清单为准。

This file records user-visible changes. Version metadata is defined in `CMakeLists.txt`, the Windows resources, and the application manifest.

## 0.9.2 — 2026-11-18 · Preview / 预览版

### 拖动抠图提速 / Faster cutout strokes

- 连续拖选按实际连通区域扩展，复用一笔内的边界判断，减少对整张截图的重复扫描。
- 按选区变化增量更新像素计数和预览，只重绘变化区域及相邻轮廓；松开鼠标后更新收紧按钮状态。
- 新增点选直接累积，避免每次重放完整选取历史；调整容差、撤销及裁框仍可重算已有操作。
- 增加缓存复用、加减选计数、局部预览和相邻轮廓更新的回归测试。
- Grow contiguous strokes through their actual regions and reuse edge checks within each stroke.
- Update counts and preview pixels incrementally, including adjacent selection outlines; update trim availability on release.
- Apply new clicks incrementally while retaining history replay for tolerance changes, undo, and reframing.
- Add regression coverage for session reuse, additive/subtractive counts, partial previews, and outline updates.

### 下载与验证 / Downloads and validation

- README 增加中英文 EXE 直达下载入口和更新说明，下载后双击即可运行。
- 本机 1920×1080 离屏模拟拖选（包含选区与预览计算）从约 2.26 秒降到 63 毫秒；实际速度随画面与设备变化。
- 1,764 组随机点选、拖选与加减选序列的新旧选区和预览像素一致，默认图像及界面回归测试通过。
- Add direct EXE downloads and update instructions to both READMEs.
- A local offscreen 1920×1080 stroke fixture, including selection and preview computation, improved from about 2.26 s to 63 ms. Results depend on image content and hardware.
- 1,764 randomized click, drag, additive, and subtractive sequences matched the previous selection and preview pixels exactly; default image and UI regression tests passed.

## 0.9.1 — 2026-11-18 · Preview / 预览版

首个公开预览版本。 / First public preview release.

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
