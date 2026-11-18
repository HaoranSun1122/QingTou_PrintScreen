# 第三方说明 / Third-party notices

本项目自身的代码、文档及仓库提供的品牌资源按 [MIT License](LICENSE) 提供。以下组件保留各自的许可；项目的 MIT 许可不会替换它们的条款。

The project's own code, documentation, and supplied brand assets are provided under the [MIT License](LICENSE). Components below retain their own terms. Upstream runtime texts and their versioned sources are preserved in [licenses/](licenses/README.md).

## Windows 系统组件 / Windows components

应用通过 Windows SDK 使用 Win32、Direct2D、DirectWrite、WIC、COM、DWM 和系统 Shell 接口。它们由操作系统提供，本仓库不打包这些系统 DLL，也不再授权 Windows 本身。

The application calls Windows-provided APIs. This repository does not bundle or relicense the operating system DLLs.

## 编译器与运行库 / Compiler runtimes

最终 exe 的运行库取决于所用工具链；发行者应保留所用发行版附带的许可和声明。

| 构建方式 / Build | 说明 / Notes | 上游 / Upstream |
| --- | --- | --- |
| MinGW-w64 GCC | CMake 请求静态链接 GCC / C++ 运行库。GCC runtime libraries carry the GPL with the GCC Runtime Library Exception; MinGW-w64 has its own notices and exceptions. | [GCC runtime exception](https://www.gnu.org/licenses/gcc-exception-3.1.html), [FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html), [MinGW-w64 copying terms](https://github.com/mingw-w64/mingw-w64/blob/master/COPYING) |
| MSVC | CMake 请求静态 C/C++ 运行库。Use a properly licensed Microsoft toolchain and follow the applicable redistributable terms. | [Microsoft redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files) |

本地预览曾使用 MinGW-w64 GCC 13.2.0 构建；GitHub Actions 使用 Windows runner 的默认 Visual Studio 工具链。发行前请确认实际构建方式，而不是仅根据文件名判断。这里提供组件和许可入口，完整条款以实际工具链附带文本为准。

Local previews have been built with MinGW-w64 GCC 13.2.0. GitHub Actions uses the runner's default Visual Studio toolchain. Identify the toolchain for the specific binary you distribute and retain its supplied license texts.

## 可选资源工具 / Optional asset tooling

`tools/build_assets.py` 使用 [Pillow](https://github.com/python-pillow/Pillow/blob/main/LICENSE) 将项目图标打包成 ICO 与 SVG 字标。Pillow 只在重新生成资源时使用；Python 和 Pillow 不包含在本项目 Windows 包中。

Pillow is used only to regenerate assets. Neither Python nor Pillow is bundled in the Windows package.

## 品牌与字体 / Brand assets and fonts

- 图形最初由 ImageGen 生成，随后由本仓库脚本打包；见[资源说明](resources/brand/README.md)。仓库提供的图标和 Logo 按项目 MIT 许可提供。
- 应用和 SVG 引用系统字体。本仓库不附带 Microsoft YaHei UI、Segoe UI 或其他字体文件。
- Icon provenance and packaging are documented in the [asset notes](resources/brand/README.md). Supplied icon/logo files are provided under the project license.
- The app and SVG refer to system fonts; no font files are bundled.
