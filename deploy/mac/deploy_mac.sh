#!/bin/bash

set -e

# 参数解析
# -d: 仅构建 Debug
# -r: 仅构建 Release
# --no-gpu-acceleration: 不编译 GPU 加速 (CoreML)
# 不指定 -d/-r 时默认同时构建 Debug 和 Release（与 Windows 脚本一致）
BUILD_DEBUG=false
BUILD_RELEASE=false
NO_GPU_ACCELERATION=false
EXPLICIT_CONFIG=false
for arg in "$@"; do
    case $arg in
        -d)
            BUILD_DEBUG=true
            EXPLICIT_CONFIG=true
            ;;
        -r)
            BUILD_RELEASE=true
            EXPLICIT_CONFIG=true
            ;;
        --no-gpu-acceleration) NO_GPU_ACCELERATION=true ;;
    esac
done

# 同时指定 -d 和 -r 报错（与 Windows 一致）
if [ "$BUILD_DEBUG" = true ] && [ "$BUILD_RELEASE" = true ]; then
    echo "Error: Cannot specify both -d and -r parameters."
    exit 1
fi

# 未显式指定 -d/-r 时，默认同时构建 Debug 和 Release
if [ "$EXPLICIT_CONFIG" = false ]; then
    BUILD_DEBUG=true
    BUILD_RELEASE=true
fi

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 项目根目录
PROJECT_ROOT="$SCRIPT_DIR/../.."

# 从 CMakeLists.txt 自动读取版本号 (project(QuickShot VERSION x.y.z))
CMAKE_LISTS="$PROJECT_ROOT/CMakeLists.txt"
VERSION=$(sed -nE 's/^project\([[:space:]]*QuickShot[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*\)/\1/p' "$CMAKE_LISTS")
if [ -z "$VERSION" ]; then
    echo "Error: Failed to parse version from CMakeLists.txt"
    exit 1
fi
echo "Version read from CMakeLists.txt: $VERSION"

# --- 构建函数 ---
# @param $1 配置名称 (Debug/Release)
build_config() {
    local CONFIG=$1
    local BUILD_DIR="$SCRIPT_DIR/build_$(echo $CONFIG | tr '[:upper:]' '[:lower:]')"
    local APP_BUNDLE="$SCRIPT_DIR/QuickShot-${CONFIG}.app"
    local DMG_FILE="$SCRIPT_DIR/QuickShot-${CONFIG}-v${VERSION}.dmg"

    echo ""
    echo "========================================"
    echo "Building ${CONFIG} Configuration"
    echo "========================================"

    # 清理旧的构建产物
    echo "Cleaning previous build artifacts..."
    rm -rf "$BUILD_DIR"
    rm -rf "$APP_BUNDLE"
    rm -f "$DMG_FILE"

    # 创建构建目录
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"

    # 进入构建目录
    cd "$BUILD_DIR"

    # 配置CMake
    echo "Configuring CMake..."
    local CMAKE_ARGS="-DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_OSX_ARCHITECTURES=arm64"
    if [ "$NO_GPU_ACCELERATION" = true ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DENABLE_OCR_GPU_ACCELERATION=OFF"
        echo "GPU acceleration disabled"
    fi
    cmake -S "$PROJECT_ROOT" -B . $CMAKE_ARGS

    # 构建项目
    echo "Building project..."
    cmake --build .

    # 回到项目根目录
    cd "$PROJECT_ROOT"

    # 创建应用程序包结构
    echo "Creating app bundle..."
    mkdir -p "$APP_BUNDLE/Contents/MacOS"
    mkdir -p "$APP_BUNDLE/Contents/Resources"

    # 复制可执行文件
    echo "Copying executable..."
    cp "$BUILD_DIR/QuickShot" "$APP_BUNDLE/Contents/MacOS/"
    chmod +x "$APP_BUNDLE/Contents/MacOS/QuickShot"

    # 复制图标
    echo "Copying icon..."
    cp "$PROJECT_ROOT/icons/app.png" "$APP_BUNDLE/Contents/Resources/"

    # 复制语言文件
    echo "Copying language files..."
    mkdir -p "$APP_BUNDLE/Contents/Resources/languages"
    cp -r "$PROJECT_ROOT/src/languages/"* "$APP_BUNDLE/Contents/Resources/languages/"

    # 复制模型文件
    echo "Copying model files..."
    mkdir -p "$APP_BUNDLE/Contents/MacOS/models/ocr"
    # 始终复制 mobile 模型
    cp -r "$PROJECT_ROOT/models/ocr/mobile" "$APP_BUNDLE/Contents/MacOS/models/ocr/"
    echo "Mobile OCR models copied."

    # 创建Info.plist
    echo "Creating Info.plist..."
    cat > "$APP_BUNDLE/Contents/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>QuickShot</string>
    <key>CFBundleIdentifier</key>
    <string>com.quickshot.app</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>QuickShot</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2026 QuickShot Team. All rights reserved.</string>
</dict>
</plist>
EOF

    # 运行macdeployqt
    echo "Running macdeployqt..."
    if [ -f "/opt/homebrew/bin/macdeployqt" ]; then
        /opt/homebrew/bin/macdeployqt "$APP_BUNDLE"
    elif [ -f "/usr/local/bin/macdeployqt" ]; then
        /usr/local/bin/macdeployqt "$APP_BUNDLE"
    else
        echo "Warning: macdeployqt not found. Trying to find in Qt installation..."
        which macdeployqt && macdeployqt "$APP_BUNDLE"
    fi

    # 拷贝 ONNX Runtime 动态库到 Frameworks 目录
    echo "Copying ONNX Runtime..."
    ONNXRT_DYLIB=$(otool -L "$APP_BUNDLE/Contents/MacOS/QuickShot" | grep libonnxruntime | awk '{print $1}')
    if [ -n "$ONNXRT_DYLIB" ] && [ -f "$ONNXRT_DYLIB" ]; then
        mkdir -p "$APP_BUNDLE/Contents/Frameworks"
        cp "$ONNXRT_DYLIB" "$APP_BUNDLE/Contents/Frameworks/"
        ONNXRT_NAME=$(basename "$ONNXRT_DYLIB")
        # 修改链接路径为 @rpath
        install_name_tool -change "$ONNXRT_DYLIB" "@rpath/$ONNXRT_NAME" "$APP_BUNDLE/Contents/MacOS/QuickShot"
        # 添加 @executable_path/../Frameworks 到 rpath
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP_BUNDLE/Contents/MacOS/QuickShot"
        echo "ONNX Runtime copied and rpath fixed."
    else
        echo "Warning: ONNX Runtime not found in binary dependencies."
    fi

    # 签名应用程序
    echo "Signing application..."
    codesign --force --deep --sign - "$APP_BUNDLE"

    # 验证签名
    echo "Verifying signature..."
    codesign --verify -v "$APP_BUNDLE"

    # 创建DMG
    echo "Creating DMG..."
    hdiutil create -srcfolder "$APP_BUNDLE" -volname "QuickShot ${CONFIG} v${VERSION}" -format UDZO -ov "$DMG_FILE"

    # 清理构建目录
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"

    # 清理 .app 中间产物（DMG 已生成，不再需要 app bundle）
    echo "Cleaning app bundle..."
    rm -rf "$APP_BUNDLE"

    echo "Package created at: $DMG_FILE"
}

# --- 主执行逻辑 ---
echo "=== QuickShot Mac Build and Package Script ==="

if [ "$BUILD_DEBUG" = true ]; then
    build_config "Debug"
fi
if [ "$BUILD_RELEASE" = true ]; then
    build_config "Release"
fi

echo ""
echo "=== Build(s) completed successfully! ==="
