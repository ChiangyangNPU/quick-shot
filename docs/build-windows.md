# QuickShot Windows 构建指南

本文档说明在 Windows 上从源码构建 QuickShot 所需的环境配置：Qt、MinGW、CMake、ONNX Runtime 的路径如何指定，以及常见问题排查。

> 打包发布（windeployqt、zip 产物）见 [Windows 打包指南](../deploy/win/README_DEPLOY.md)；
> macOS 构建见 [Mac 打包指南](../deploy/mac/README_DEPLOY_MAC.md)；
> CI 自动构建见 `.github/workflows/ci.yml`（不依赖本文档中的任何本地路径）。

## 1. 环境要求

| 组件 | 版本 | 说明 |
|---|---|---|
| Windows | 10 / 11 | 64 位 |
| Qt | 6.10.2 (MinGW 64-bit) | 框架与 UI，安装时勾选 MinGW 套件 |
| MinGW | 13.1.0 | 必须使用 Qt 自带/匹配版本，详见 §2.4 |
| CMake | 3.20+ | Qt Tools 自带或独立安装 |
| Ninja | 任意近期版本 | 可选，推荐（deploy.ps1 与 CI 均优先使用） |
| ONNX Runtime | 1.26.0 | 仓库已内置，零配置，详见 §4 |

## 2. Qt 安装与路径解析

### 2.1 安装组件

Qt 在线安装器中勾选：

- **Qt 6.10.2** → *MinGW 64-bit*（预编译库，位于 `<Qt根目录>\6.10.2\mingw_64`）
- **Developer and Designer Tools** → *MinGW 13.1.0 64-bit*、*CMake 64-bit*、*Ninja*（位于 `<Qt根目录>\Tools\`）

默认安装路径为 `C:\Software\Qt`，下文均以此为例，自定义路径请同步替换。

### 2.2 CMake 如何找到 Qt（三条入口，按优先级）

`CMakeLists.txt` 中的解析逻辑（仅在未显式传入 `CMAKE_PREFIX_PATH` 时生效）：

| 优先级 | 入口 | 配置方式 | 适用场景 |
|---|---|---|---|
| 1 | 命令行 `-DCMAKE_PREFIX_PATH=...`（或同名环境变量） | 每次 configure 时传入 | 一次性构建、CI |
| 2 | 环境变量 `QT_DIR` | 指向 Qt 安装前缀 | 长期固定路径 |
| 3 | CMakeLists 兜底路径 | 无需配置 | 兜底路径真实存在时默认走此条 |

兜底路径为维护者本机默认值 `C:/Software/Qt/6.10.2/mingw_64`，**仅在路径真实存在时启用**——其他机器上不存在则安静跳过，Qt 找不到时 `find_package(... REQUIRED)` 会给出明确报错。

持久设置 `QT_DIR`（PowerShell，写入用户环境变量，重开终端生效）：

```powershell
[Environment]::SetEnvironmentVariable("QT_DIR", "C:/Software/Qt/6.10.2/mingw_64", "User")
```

### 2.3 CLion 配置

1. **Toolchains**（`Settings → Build, Execution, Deployment → Toolchains`）：添加 MinGW 类型工具链，路径指向 `C:\Software\Qt\Tools\mingw1310_64`。
2. **CMake Profile**（`Settings → Build → CMake`）：Toolchain 选择上一步添加的 MinGW；CMake options 可选填 `-DCMAKE_PREFIX_PATH=C:/Software/Qt/6.10.2/mingw_64`（该值存于 `.idea/`，不入库，不填则走 §2.2 的解析链）。
3. **缓存刷新**：修改 CMakeLists、环境变量或 Qt 版本后，需 **Tools → CMake → Reset Cache and Reload Project**，否则旧缓存中仍保留上次的 `CMAKE_PREFIX_PATH`。

### 2.4 编译器 ABI 约束（重要）

Qt 预编译的 MinGW 库与编译器版本强绑定：**必须使用 Qt 安装器提供的 MinGW 13.1.0**（`C:\Software\Qt\Tools\mingw1310_64\bin`）。使用 MSVC、或版本不匹配的独立 MinGW（如 Strawberry Perl 自带的旧版 gcc），会导致 `find_package` 成功但链接失败、或运行时崩溃。

## 3. 命令行构建

```powershell
# 1) 将 Qt、MinGW、Ninja 加入当前会话 PATH（QT_DIR 环境变量为永久方式，见 §2.2）
$env:Path = "C:\Software\Qt\6.10.2\mingw_64\bin;C:\Software\Qt\Tools\mingw1310_64\bin;C:\Software\Qt\Tools\Ninja;$env:Path"

# 2) 配置（Ninja 优先；无 Ninja 时改为 -G "MinGW Makefiles"）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 3) 构建
cmake --build build --parallel
```

产物为 `build\QuickShot.exe`。构建后脚本会自动将 Qt DLL、插件（platforms/sqldrivers/tls/imageformats）、ONNX Runtime DLL、OCR 模型（`models/ocr/mobile`）复制到输出目录，**开发机上可直接运行**（VC++ Runtime 系统自带；分发时由打包脚本补齐，见 §5）。

可用的 CMake option：`-DENABLE_OCR_GPU_ACCELERATION=OFF` 关闭 GPU 加速编译（仅 CPU 推理，减小依赖）。

## 4. ONNX Runtime（OCR）

Windows 上**默认零配置**，`CMakeLists.txt` 按以下顺序解析：

1. 环境变量 `ONNXRUNTIME_ROOT`（指向包含 `include/onnxruntime/onnxruntime_cxx_api.h` 的目录）；
2. 仓库内 `third_party/onnxruntime`（include 与 lib 随 git 提交，常规 clone 即可用）；
3. 均缺失时自动从 GitHub 下载 `onnxruntime-win-x64-1.26.0.zip` 解压到 `third_party/`。

三处均失败时仅输出 WARNING，OCR 功能编译期整体禁用，其余功能不受影响。

GPU 加速（DirectML）说明：`dml_provider_factory.h` 与 `DirectML.dll` 齐备时自动启用 `ENABLE_DIRECTML`；DirectML.dll 通过 `LoadLibrary` 运行时加载，不链接 d3d12/dxgi，无 DirectX 12 的机器上启动不受影响。

## 5. 打包发布

```powershell
cd deploy\win
.\deploy.ps1 -r    # 仅 Release；-d 仅 Debug；默认两者都打
```

脚本自动完成 windeployqt、依赖复制与 zip 打包，产物名 `QuickShot-Release-v{version}-Windows-x64.zip`（与自动更新的命名约定一致）。若 Qt / MinGW / CMake 安装路径与默认不同，修改 `deploy.ps1` 开头 Configuration 段的 `$QtRoot`、`$QtVersion`、`$QtKit`、`$MingwBinDir`、`$CMakeBinDir`、`$NinjaDir`，或设同名环境变量 `QUICKSHOT_QT_PREFIX` / `QUICKSHOT_QT_BIN` / `QUICKSHOT_MINGW_BIN` / `QUICKSHOT_CMAKE_BIN` / `QUICKSHOT_NINJA_DIR`（环境变量优先，CI 即采用此方式）。详见 [Windows 打包指南](../deploy/win/README_DEPLOY.md)。

## 6. CI 自动构建

`.github/workflows/ci.yml` 在 push（`main`/`master`）与 PR 时于 GitHub Actions 上构建 Windows（MinGW）与 macOS 两个目标并上传产物，用于编译验证（产物为裸可执行文件，非发布包）。CI 中的 Qt 路径由 `install-qt-action` 安装后经 `-DCMAKE_PREFIX_PATH="$env:QT_ROOT_DIR"` 显式传入，不依赖本机任何路径。Gitee 侧不运行该 workflow。

发版走 `.github/workflows/release.yml`：push `v*` 标签时触发，两个平台分别**调用现有 deploy 脚本**（`deploy.ps1 -r` / `deploy_mac.sh -r`，路径经环境变量覆盖为 CI 安装位置），产出与本地打包完全同名的发布包——`QuickShot-Release-v{version}-Windows-x64.zip` 与 `QuickShot-Release-v{version}.dmg`——并自动挂载到 GitHub Release。workflow 会先校验标签版本与 `CMakeLists.txt` 的 `PROJECT_VERSION` 一致，不一致直接失败。

## 7. 常见问题

| 现象 | 原因与处理 |
|---|---|
| configure 报 `Could not find a package configuration file provided by "Qt6"` | §2.2 三条入口均未命中：确认 Qt 已安装、路径正确后，显式传 `-DCMAKE_PREFIX_PATH` 或设 `QT_DIR` |
| 链接阶段大量 undefined reference / 运行闪退 | 编译器与 Qt ABI 不匹配：确认使用 Qt 自带 MinGW 13.1.0（§2.4），检查 Toolchains 配置 |
| `MinGW Makefiles` 生成器报 `sh.exe was found in the PATH` | Git 的 `usr\bin` 在 PATH 中：改用 Ninja 生成器，或构建会话中将其从 PATH 移除 |
| 修改环境变量 / CMakeLists 后行为不变 | CMake 缓存残留：删除 build 目录重新 configure，或 CLion 中 Reset Cache and Reload Project |
| 需要指定自编译/其他版本的 ONNX Runtime | 设环境变量 `ONNXRUNTIME_ROOT` 指向对应目录（§4） |
