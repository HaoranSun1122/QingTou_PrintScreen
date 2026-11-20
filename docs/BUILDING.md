# 构建与测试 / Build and test

[简体中文](#简体中文) · [English](#english)

## 简体中文

### 要求

- Windows，CMake 3.20+，支持 C++20 的 x64 编译器。
- MinGW-w64 GCC + MinGW Make，或 Visual Studio / Build Tools 的 C++ 桌面开发组件与 Windows SDK。
- 普通构建使用已提交的 ICO 资源，无需 Python 或 Pillow。

### 快速构建

在仓库根目录运行：

```powershell
.\build.ps1
```

脚本优先使用 PATH 中的 MinGW 工具链，否则由 CMake 选择默认生成器；它会运行默认测试。更换工具链时使用不同构建目录，避免复用不兼容缓存。

### MinGW-w64

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DQINGTOU_ENABLE_DESKTOP_TESTS=OFF
cmake --build build-mingw --parallel 2
ctest --test-dir build-mingw --output-on-failure
```

### Visual Studio / MSVC

在已配置 Visual Studio C++ 工具链的终端中，使用其 CMake Visual Studio 生成器：

```powershell
cmake -S . -B build-msvc -A x64 -DBUILD_TESTING=ON -DQINGTOU_ENABLE_DESKTOP_TESTS=OFF
cmake --build build-msvc --config Release --parallel 2
ctest --test-dir build-msvc -C Release --output-on-failure
```

如果设置过其他默认生成器，可通过 `cmake --help` 查看本机生成器，并用 `-G` 显式指定已安装的 Visual Studio 版本。

### 输出与打包

输出统一为 `dist/QingTou.exe`。CMake 请求静态 C++ 运行库；图标嵌入 exe。

```powershell
.\tools\package_release.ps1
```

打包脚本检查 exe 版本与 x64 架构，再生成 Windows 压缩包、源码压缩包和 `SHA256SUMS.txt`。不收集截图、GIF、设置或构建输出。运行库许可说明见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

### 测试层次

默认 4 项测试覆盖 PNG Alpha 往返、GIF 编码、魔棒与智能抠图、拖动笔刷、窗口预选、裁切与真实 PNG 导出尺寸，并通过隐藏窗口 / 离屏画布验证控件。智能抠图包含大图宽框细线、单像素、空背景与修补标记用例。它们不读取用户桌面。

桌面采集与录制测试需要可交互 Windows 桌面，可能短暂显示测试窗口并读取屏幕。单独启用：

```powershell
.\build.ps1 -DesktopTests
```

GitHub Actions 默认只运行不读取桌面的测试。测试图片和临时录制位于被忽略的 `test-output/` 或构建目录中，不提交到仓库。

### 代码结构

| 路径 | 职责 |
| --- | --- |
| `src/main.cpp` | Win32 窗口、托盘、热键、工具栏、选区和编辑交互 |
| `src/selection.*` | 颜色匹配、连通性、圆形笔刷、蒙版、内容边界与透明预览 |
| `src/smart_cutout.*` | 背景估计、带标记图割、原图边缘与细节恢复 |
| `src/raster.cpp` | 增量笔段覆盖率、透明混合与可调马赛克 |
| `src/image.*` | 桌面采集、D2D 标注、WIC PNG / GIF、剪贴板 |
| `src/recorder.*` | 录制线程、计时、停止与文件结束处理 |
| `src/theme.*` | 系统浅色 / 深色配色 |
| `resources/` | 清单、图标、版本与原生对话框 |
| `tests/` | 图像、选区、编辑交互和可选桌面测试 |

修改图标后，可安装 Pillow 并运行 `python tools/build_assets.py` 重新生成 ICO / SVG。不要提交临时生成草图或用户截图。

## English

### Requirements

Use Windows, CMake 3.20+, and an x64 C++20 toolchain: MinGW-w64 GCC with MinGW Make, or Visual Studio / Build Tools with the Desktop C++ workload and Windows SDK.

The committed ICO is used during normal builds. Python and Pillow are only needed to regenerate brand assets.

### Build

Run `.\build.ps1` from the repository root. The script prefers MinGW when it is available on PATH and runs default tests. For explicit toolchains, use the MinGW or Visual Studio command blocks above. Keep separate build directories when changing generators.

The executable is written to `dist/QingTou.exe`. Use `.\tools\package_release.ps1` to validate version/architecture and create portable and source archives plus checksums. Packaging excludes captures, settings, and build output.

### Testing

The four default tests cover image codecs, alpha, color/guided cutouts, strokes, window preselection, crop bounds, exported PNG dimensions, and native controls rendered on hidden/offscreen canvases. Guided cutouts include loose-box thin details, isolated pixels, empty gradients, and correction priority. They do not read the user's desktop.

`.\build.ps1 -DesktopTests` additionally runs interactive desktop capture and recording tests. These can display test windows and read the screen; run them in a suitable test session. GitHub Actions leaves these tests disabled.

Generated test files are ignored by Git. For component responsibilities, see the source map above. Run `python tools/build_assets.py` only when intentionally regenerating brand assets.
