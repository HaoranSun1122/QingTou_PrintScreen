# 品牌资源

- `icon.png`：恢复的亮色图形，薄荷绿截图框与日光黄魔棒星光；图形最初由内置 ImageGen 生成。
- `app.ico`：用于 exe、窗口、托盘和主面板的多尺寸 Windows 图标。
- `logo.svg`：相同图形配合「轻透截图」字标；字标颜色适配浅色 / 深色外观。

修改 `icon.png` 后运行 `python tools/build_assets.py` 重新打包，需要 Pillow。
构建软件只使用已提交的 `app.ico`，不需要 Python 或在线服务。

仓库提供的这些图标和 Logo 按项目根目录的 [MIT License](../../LICENSE) 提供。SVG 引用系统字体，不附带字体文件。

## English

- `icon.png`: mint capture frame and yellow star graphic, originally generated with ImageGen.
- `app.ico`: multi-size icon embedded in the executable and used in the window, tray, and main panel.
- `logo.svg`: the same graphic with the Chinese wordmark; its text color follows light/dark preference.

Run `python tools/build_assets.py` after deliberately changing the source icon. This optional step needs Pillow; normal builds use the committed ICO and need neither Python nor an online service.

The supplied icon and logo files are provided under the project [MIT License](../../LICENSE). The SVG references system fonts and does not bundle font files.
