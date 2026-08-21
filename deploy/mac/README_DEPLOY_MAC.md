# QuickShot Mac 打包与部署指南

本文档说明如何使用 `deploy_mac.sh` 脚本自动化打包 QuickShot 应用程序，专为 M 系列芯片的 Mac 设计。

## 目录结构

```text
QuickShot/
├── deploy/
│   └── mac/
│       ├── deploy_mac.sh               # Mac 打包脚本
│       ├── README_DEPLOY_MAC.md         # 本文档
│       ├── build_debug/                # Debug 构建目录（自动生成，打包后删除）
│       ├── build_release/              # Release 构建目录（自动生成，打包后删除）
│       ├── QuickShot-Debug.app/        # Debug 应用程序包（中间产物，打包后删除）
│       ├── QuickShot-Release.app/      # Release 应用程序包（中间产物，打包后删除）
│       ├── QuickShot-Debug-v1.0.0.dmg   # Debug DMG 安装包（最终产物）
│       └── QuickShot-Release-v1.0.0.dmg # Release DMG 安装包（最终产物）
├── src/                                # 源代码
└── CMakeLists.txt                      # CMake 构建配置（版本号来源）
```

> 版本号从 `CMakeLists.txt` 的 `project(QuickShot VERSION x.y.z)` 自动读取，无需在脚本中手动维护。

## 环境要求

- **macOS 11.0+** (支持 M 系列芯片)
- **Qt 6.10.2 (arm64)**
- **CMake 3.20+**
- **Xcode Command Line Tools**

## 如何使用

1. 打开终端。
2. 进入 `deploy/mac` 目录：
   ```bash
   cd deploy/mac
   ```
3. 运行打包脚本：
   ```bash
   # 默认同时构建 Debug 和 Release 两个包
   ./deploy_mac.sh

   # 仅构建 Release 包
   ./deploy_mac.sh -r

   # 仅构建 Debug 包
   ./deploy_mac.sh -d

   # 不编译 GPU 加速（CoreML），仅使用 CPU 推理
   ./deploy_mac.sh --no-gpu-acceleration

   # 组合使用：仅构建 Release + 禁用 GPU 加速
   ./deploy_mac.sh -r --no-gpu-acceleration
   ```

> `-d` 与 `-r` 互斥，同时指定会报错。不指定任何配置参数时默认同时构建 Debug 和 Release。

## 脚本功能

脚本会按顺序执行以下操作（每个配置各执行一次）：

1. **读取版本号**：从 `CMakeLists.txt` 自动解析版本号，用于 DMG 文件名、卷名和 Info.plist。
2. **清理旧的构建产物**：删除之前的构建目录、应用程序包和 DMG 文件。
3. **创建构建目录**：自动创建 `deploy/mac/build_debug` 或 `deploy/mac/build_release` 目录。
4. **配置 CMake**：使用 CMake 配置项目，指定 M 系列芯片架构和构建类型（Debug/Release）。
5. **构建项目**：编译生成可执行文件。
6. **创建应用程序包**：
   - 创建标准的 macOS 应用程序包结构
   - 复制可执行文件和资源文件
   - 复制语言文件（支持中文和英文）
   - 创建 Info.plist 配置文件（版本号自动注入）
7. **复制依赖**：运行 `macdeployqt` 自动复制 Qt 依赖库。
8. **复制 ONNX Runtime**：自动检测并复制 ONNX Runtime 动态库到 Frameworks 目录，并修复 rpath。
9. **代码签名**：对应用程序进行 Ad Hoc 签名，解决代码签名无效的问题。
10. **验证签名**：验证应用程序签名是否成功。
11. **创建 DMG**：生成最终的 DMG 安装包。
12. **清理中间产物**：打包完成后自动删除构建目录和 `.app` 应用程序包，只保留 DMG 文件。

## 输出结果

打包完成后，您将在 `deploy/mac` 目录下看到（以默认同时构建为例）：

- **QuickShot-Debug-v1.0.0.dmg**: Debug 版 DMG 安装包，用于开发调试。
- **QuickShot-Release-v1.0.0.dmg**: Release 版 DMG 安装包，可直接分发给用户。

> 构建目录（`build_*`）和应用程序包（`*.app`）作为中间产物已自动清理，只保留最终的 DMG 文件。

## 语言支持

应用程序支持中文和英文，语言文件已正确打包到应用程序中：
- `Contents/Resources/languages/en_US.json` - 英文语言包
- `Contents/Resources/languages/zh_CN.json` - 中文语言包

## 宏控参数说明

| 参数 | 作用 | 影响 |
|:---|:---|:---|
| `-d` | 仅构建 Debug 配置 | 产物为 `QuickShot-Debug-v{VERSION}.dmg` |
| `-r` | 仅构建 Release 配置 | 产物为 `QuickShot-Release-v{VERSION}.dmg` |
| `--no-gpu-acceleration` | 不编译 CoreML GPU 加速 | OCR 仅使用 CPU 推理，减少依赖 |

参数说明：
- `-d` 与 `-r` 互斥，同时指定会报错；都不指定时默认同时构建两个配置。
- `--no-gpu-acceleration` 会传递给 CMake 的对应 option：`-DENABLE_OCR_GPU_ACCELERATION=OFF`。
- 以上参数可组合使用，例如 `-r --no-gpu-acceleration`。

## 注意事项

1. **M 系列芯片优化**：脚本专门针对 M 系列芯片优化，使用 `arm64` 架构。
2. **代码签名**：脚本会自动对应用程序进行 Ad Hoc 签名，确保应用程序能够正常启动。
3. **依赖管理**：使用 `macdeployqt` 自动复制所有必要的 Qt 依赖，确保应用程序能够独立运行。
4. **语言文件**：已修复语言文件路径问题，应用程序能够正确加载中文语言包。
5. **ONNX Runtime**：脚本会自动检测并复制 ONNX Runtime 动态库，并修复链接路径。
6. **路径配置**：脚本会自动根据脚本所在位置计算项目根目录，无需手动指定路径。

## 故障排除

如果遇到以下问题：

- **代码签名错误**：脚本已包含自动签名步骤，解决了代码签名无效的问题。
- **语言加载失败**：已修复语言文件路径，应用程序能够正确加载语言文件。
- **依赖缺失**：`macdeployqt` 会自动复制所有必要的依赖库。
- **ONNX Runtime 找不到**：脚本会自动从可执行文件的依赖中提取 ONNX Runtime 路径。

## 发布步骤

1. 运行 `./deploy_mac.sh -r` 生成 Release 版 DMG 文件。
2. 测试生成的 DMG 文件，确保应用程序能够正常启动和运行。
3. 分发生成的 DMG 文件给用户。

用户只需双击 DMG 文件，然后将 QuickShot.app 拖动到 Applications 文件夹即可完成安装。