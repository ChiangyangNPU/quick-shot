# QuickShot 打包与部署指南

本文档说明如何使用 `deploy.ps1` 脚本自动化打包 QuickShot 应用程序。

## 目录结构

```text
QuickShot/
├── deploy/
│   └── win/
│       ├── deploy.ps1                                   # 打包脚本
│       ├── README_DEPLOY.md                             # 本文档
│       ├── build_Debug/                                 # Debug 构建目录（自动生成）
│       ├── build_Release/                               # Release 构建目录（自动生成）
│       ├── QuickShot-Debug-v{version}-Windows-x64/       # Debug 版本包（自动生成）
│       ├── QuickShot-Debug-v{version}-Windows-x64.zip   # Debug zip 包（自动生成）
│       ├── QuickShot-Release-v{version}-Windows-x64/     # Release 版本包（自动生成）
│       └── QuickShot-Release-v{version}-Windows-x64.zip  # Release zip 包（自动生成）
├── src/                                                 # 源代码
└── CMakeLists.txt                                       # CMake 构建配置（含版本号）
```

## 版本号管理

版本号由 [CMakeLists.txt](file:///e:/develop/Code/github_new/quick-shot/CMakeLists.txt#L2) 的 `project(QuickShot VERSION x.y.z)` 统一管理。打包脚本通过正则表达式自动读取版本号，无需手动指定。修改版本号后需在 CLion 中 **Reset Cache and Reload CMake Project**。

## 环境要求

- **Windows 10/11**
- **Qt 6.10.2 (MinGW 64-bit)**
- **CMake 3.20+**
- **Ninja** (推荐) 或 MinGW Makefiles

脚本会自动检测以下路径（如果您的环境不同，请修改脚本中的 Configuration 部分）：
- Qt: `C:\Software\Qt\6.10.2\mingw_64`
- MinGW: `C:\Software\Qt\Tools\mingw1310_64\bin`
- CMake: `C:\Software\Qt\Tools\CMake_64\bin`

## 如何使用

1. 打开 PowerShell 终端。
2. 进入 `deploy\win` 目录：
   ```powershell
   cd deploy\win
   ```
3. 运行打包脚本：
   ```powershell
   # 同时打包 Debug 和 Release 版本（默认行为）
   .\deploy.ps1

   # 仅打包 Release 版本
   .\deploy.ps1 -r

   # 仅打包 Debug 版本
   .\deploy.ps1 -d

   # 仅打包 Release，不包含 GPU 加速依赖（DirectML.dll）
   .\deploy.ps1 -r -NoGpuAcceleration
   ```

## 脚本功能

脚本会按顺序执行以下操作：
1. **读取版本号**：从 CMakeLists.txt 的 `project(QuickShot VERSION x.y.z)` 中自动解析版本号。
2. **环境检查**：验证 Qt、MinGW、CMake 等工具是否存在。
3. **构建 Debug 版本**：
   - 清理旧的构建和输出目录。
   - 使用 CMake 配置并构建项目（源码路径指向项目根目录）。
   - 将生成的可执行文件复制到 `QuickShot-Debug-v{version}-Windows-x64`。
   - 运行 `windeployqt` 自动复制依赖的 DLL 文件。
   - 注意：如果未找到 Debug 版 Qt 库（`Qt6Cored.dll`），脚本会自动降级使用 Release 库进行打包。
4. **构建 Release 版本**：
   - 类似 Debug 流程，生成优化后的发布版本到 `QuickShot-Release-v{version}-Windows-x64`。
5. **打包 zip**：将每个输出目录打包为同名 zip 文件，可直接用于发布或自动更新。

## 输出结果

打包完成后，您将在 `deploy\win` 目录下看到：
- **QuickShot-Debug-v{version}-Windows-x64/**: 包含调试符号和未优化的可执行文件，适合开发和测试。
- **QuickShot-Debug-v{version}-Windows-x64.zip**: 上述目录的 zip 包。
- **QuickShot-Release-v{version}-Windows-x64/**: 包含优化后的可执行文件和必要的依赖库，适合最终用户分发。
- **QuickShot-Release-v{version}-Windows-x64.zip**: 上述目录的 zip 包，可直接上传到 GitHub/Gitee Release 作为自动更新包。

> **自动更新文件命名约定**：UpdateManager 代码中 GitHub/Gitee 下载 URL 格式为 `QuickShot-Release-v{version}-Windows-x64.zip`，请确保上传的 zip 包名与此一致。

## 宏控参数说明

| 参数 | 作用 | 影响 |
|:---|:---|:---|
| `-NoGpuAcceleration` | 不打包 DirectML.dll 等 GPU 加速依赖 | OCR 仅使用 CPU 推理，减小包体积 |

这些参数会传递给 CMake 的对应 option：
- `-NoGpuAcceleration` → `-DENABLE_OCR_GPU_ACCELERATION=OFF`

## 注意事项

1. **路径配置**：脚本会自动根据脚本所在位置计算项目根目录，无需手动指定路径。
2. **依赖复制**：脚本会自动复制 ONNX Runtime DLL、DirectML DLL（如启用）、VC++ Runtime DLL 等必要依赖。
3. **OCR 模型**：脚本会自动复制 mobile 模型。
4. **zip 打包**：脚本使用 PowerShell `Compress-Archive` 生成 zip 包，输出文件名格式为 `QuickShot-{Config}-{Version}-Windows-x64.zip`。