# 第三方组件与许可证声明

WorkBuddy2API Qt Manager 除本项目管理器代码外，还使用并随 Windows x64 运行包分发下列第三方运行组件。各第三方组件仍受其自身许可证约束；仓库根目录的 PolyForm Noncommercial License 仅适用于本项目管理器代码。

## Qt 6.11.1

Windows 包使用 Qt Core、Gui、Network、Svg、Widgets 及相应平台、图像、TLS 等插件。Qt 组件以独立动态库和插件形式随包提供，未修改 Qt 库。该构建按 Qt LGPL v3 许可选项使用 Qt；关于 Qt 的许可和再分发说明，请参阅 [Qt LGPL 文档](https://doc.qt.io/qt-6/lgpl.html)。

发布包的 `licenses/qt/` 目录包含 GNU GPL v3、GNU LGPL v3 文本及 Qt SBOM（软件物料清单），其中列出相关 Qt 模块和所含第三方组件。Qt 6.11.1 对应源码可从 [Qt 官方源码归档](https://download.qt.io/official_releases/qt/6.11/6.11.1/single/qt-everywhere-src-6.11.1.tar.xz) 获取。动态库与应用程序分开提供，用户可按适用许可证替换 Qt 运行库。

## MinGW 运行库

Windows 包使用 GCC 13.1.0 工具链构建，并随包提供 `libgcc_s_seh-1.dll`、`libstdc++-6.dll` 和 `libwinpthread-1.dll`。对应的 GCC Runtime Library Exception、MinGW-w64 runtime 与 winpthreads 许可证副本位于 `licenses/mingw/`。上游源码可从 [GCC](https://gcc.gnu.org/) 和 [MinGW-w64](https://github.com/mingw-w64/mingw-w64) 获取。

## 其他随包组件

运行包还含 Qt 部署工具提供的 `opengl32sw.dll` 软件 OpenGL 组件和 `D3Dcompiler_47.dll`。Qt SBOM 中包含可识别的组件及其许可证信息；这些第三方文件均不属于本项目的 PolyForm 许可范围。

本声明用于标识随包组件和许可证文件，不替代各组件许可证原文。请同时阅读发布包内 `licenses/` 目录中的文本与 SBOM。
