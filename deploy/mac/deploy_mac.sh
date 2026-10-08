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
    # Qt 前缀可用环境变量 QUICKSHOT_QT_PREFIX 覆盖（CI 上 brew 的 qt 为 keg-only，不在 /opt/homebrew）
    local QT_PREFIX="${QUICKSHOT_QT_PREFIX:-/opt/homebrew}"
    local CMAKE_ARGS="-DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_PREFIX_PATH=${QT_PREFIX} -DCMAKE_OSX_ARCHITECTURES=arm64"
    if [ "$NO_GPU_ACCELERATION" = true ]; then
        CMAKE_ARGS="$CMAKE_ARGS -DENABLE_OCR_GPU_ACCELERATION=OFF"
        echo "GPU acceleration disabled"
    fi
    cmake -S "$PROJECT_ROOT" -B . $CMAKE_ARGS

    # 构建项目
    echo "Building project..."
    cmake --build . --parallel

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

    # 生成应用图标：icons/app.svg（矢量）→ QuickLook 渲染 1024 主图 → 多尺寸 .icns，
    # 并在 Info.plist 中以 CFBundleIconFile 声明
    # （否则 Finder/Dock/DMG 安装窗口里 app 显示的是通用占位图标）
    echo "Generating app icon..."
    local ICONSET="$SCRIPT_DIR/app.iconset"
    rm -rf "$ICONSET" && mkdir -p "$ICONSET"
    # SVG 原始画布只有 32x32：改写宽高声明后让 QuickLook 按矢量渲染出满幅 1024 主图
    local SVG_MASTER="$SCRIPT_DIR/app-icon-1024.png"
    sed 's/width="32" height="32"/width="1024" height="1024"/' "$PROJECT_ROOT/icons/app.svg" \
        > "$SCRIPT_DIR/app-icon-master.svg"
    if qlmanage -t -s 1024 -o "$SCRIPT_DIR" "$SCRIPT_DIR/app-icon-master.svg" >/dev/null 2>&1 \
        && [ -f "$SCRIPT_DIR/app-icon-master.svg.png" ]; then
        mv "$SCRIPT_DIR/app-icon-master.svg.png" "$SVG_MASTER"
    else
        # QuickLook 渲染失败时回退到 200x200 的位图源
        echo "Warning: SVG 图标渲染失败，回退使用 icons/app.png"
        cp "$PROJECT_ROOT/icons/app.png" "$SVG_MASTER"
    fi
    rm -f "$SCRIPT_DIR/app-icon-master.svg"
    sips -z 16 16     "$SVG_MASTER" --out "$ICONSET/icon_16x16.png"        >/dev/null
    sips -z 32 32     "$SVG_MASTER" --out "$ICONSET/icon_16x16@2x.png"     >/dev/null
    sips -z 32 32     "$SVG_MASTER" --out "$ICONSET/icon_32x32.png"        >/dev/null
    sips -z 64 64     "$SVG_MASTER" --out "$ICONSET/icon_32x32@2x.png"     >/dev/null
    sips -z 128 128   "$SVG_MASTER" --out "$ICONSET/icon_128x128.png"      >/dev/null
    sips -z 256 256   "$SVG_MASTER" --out "$ICONSET/icon_128x128@2x.png"   >/dev/null
    sips -z 256 256   "$SVG_MASTER" --out "$ICONSET/icon_256x256.png"      >/dev/null
    sips -z 512 512   "$SVG_MASTER" --out "$ICONSET/icon_256x256@2x.png"   >/dev/null
    sips -z 512 512   "$SVG_MASTER" --out "$ICONSET/icon_512x512.png"      >/dev/null
    sips -z 1024 1024 "$SVG_MASTER" --out "$ICONSET/icon_512x512@2x.png"   >/dev/null
    iconutil -c icns "$ICONSET" -o "$APP_BUNDLE/Contents/Resources/QuickShot.icns"
    rm -rf "$ICONSET" "$SVG_MASTER"

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
    <key>CFBundleIconFile</key>
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
    <key>LSUIElement</key>
    <string>1</string>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2026 QuickShot Team. All rights reserved.</string>
</dict>
</plist>
EOF

    # 运行macdeployqt
    echo "Running macdeployqt..."
    if [ -x "${QT_PREFIX}/bin/macdeployqt" ]; then
        "${QT_PREFIX}/bin/macdeployqt" "$APP_BUNDLE"
    elif [ -f "/opt/homebrew/bin/macdeployqt" ]; then
        /opt/homebrew/bin/macdeployqt "$APP_BUNDLE"
    elif [ -f "/usr/local/bin/macdeployqt" ]; then
        /usr/local/bin/macdeployqt "$APP_BUNDLE"
    else
        MACDEPLOYQT="$(command -v macdeployqt || true)"
        if [ -n "$MACDEPLOYQT" ]; then
            "$MACDEPLOYQT" "$APP_BUNDLE"
        else
            echo "Error: macdeployqt not found" >&2
            exit 1
        fi
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
    elif [ -n "$ONNXRT_DYLIB" ] && [ -f "$APP_BUNDLE/Contents/Frameworks/$(basename "$ONNXRT_DYLIB")" ]; then
        # macdeployqt 已拷入 Frameworks 并改写为 @rpath / @executable_path，无需手动处理
        echo "ONNX Runtime already deployed by macdeployqt ($ONNXRT_DYLIB)."
    else
        echo "Warning: ONNX Runtime not found in binary dependencies."
    fi

    # 补齐依赖闭包：macdeployqt 处理 brew 动态库时偶发漏拷传递依赖（如 libonnxruntime
    # 的 libonnx/libonnx_proto/libprotobuf-lite/libre2），改写了依赖路径却没拷文件，
    # 启动时 dyld 直接 abort。此处扫描主程序与 Frameworks 全部库的依赖，把缺失的
    # 非系统依赖补拷进 Frameworks，绝对路径依赖统一改写为 @rpath（解析到主程序 rpath）。
    echo "Verifying dylib dependency closure..."
    local ROUND=0 COPIED=1
    while [ "$COPIED" -eq 1 ] && [ "$ROUND" -lt 6 ]; do
        ROUND=$((ROUND+1)); COPIED=0
        local SCAN LIB DEP SRC TGT NEWDEP FWDIR SUFFIX NAME
        SCAN="$APP_BUNDLE/Contents/MacOS/QuickShot"
        SCAN+=$'\n'"$(ls "$APP_BUNDLE/Contents/Frameworks/"*.dylib 2>/dev/null)"
        while IFS= read -r LIB; do
            [ -f "$LIB" ] || continue
            while IFS= read -r DEP; do
                [ -z "$DEP" ] && continue
                case "$DEP" in
                    /opt/homebrew/*|/usr/local/*)
                        # brew 绝对路径依赖：补拷（framework 连目录）并改写为
                        # @executable_path/../Frameworks/<相同内部布局>
                        if [[ "$DEP" == *".framework/"* ]]; then
                            FWDIR="${DEP%%.framework/*}.framework"
                            SUFFIX="${DEP#*.framework}"
                            TGT="$APP_BUNDLE/Contents/Frameworks/$(basename "$FWDIR")$SUFFIX"
                            if [ ! -f "$TGT" ]; then
                                echo "  fixup: 补拷 framework $(basename "$FWDIR")"
                                cp -R "$FWDIR" "$APP_BUNDLE/Contents/Frameworks/"
                                COPIED=1
                            fi
                            NEWDEP="@executable_path/../Frameworks/$(basename "$FWDIR")$SUFFIX"
                        else
                            NAME="$(basename "$DEP")"
                            TGT="$APP_BUNDLE/Contents/Frameworks/$NAME"
                            if [ ! -f "$TGT" ]; then
                                echo "  fixup: 补拷 $NAME ← $DEP"
                                cp "$DEP" "$TGT"
                                COPIED=1
                            fi
                            NEWDEP="@executable_path/../Frameworks/$NAME"
                        fi
                        if [ "$NEWDEP" != "$DEP" ]; then
                            /usr/bin/install_name_tool -change "$DEP" "$NEWDEP" "$LIB"
                        fi
                        ;;
                    @executable_path/*)
                        # 主程序相对路径依赖：校验目标存在（framework 保持内部布局），
                        # 缺失时按名从 brew 前缀找回
                        TGT="$APP_BUNDLE/Contents/Frameworks/$(basename "$DEP")"
                        case "$DEP" in
                            *.framework/*) TGT="$APP_BUNDLE/Contents/Frameworks/${DEP#*../Frameworks/}" ;;
                        esac
                        [ -f "$TGT" ] && continue
                        NAME="$(basename "$DEP")"
                        if [[ "$DEP" == *".framework/"* ]]; then
                            SRC="$(find /opt/homebrew/opt -maxdepth 4 -type d -name "$NAME.framework" 2>/dev/null | head -1)"
                            if [ -n "$SRC" ]; then
                                echo "  fixup: 补拷 framework $NAME.framework"
                                cp -R "$SRC" "$APP_BUNDLE/Contents/Frameworks/"
                                COPIED=1
                            fi
                        else
                            SRC="/opt/homebrew/lib/$NAME"
                            [ -f "$SRC" ] || SRC="$(find /opt/homebrew/lib -maxdepth 2 -name "$NAME" 2>/dev/null | head -1)"
                            if [ -f "$SRC" ]; then
                                echo "  fixup: 补拷 $NAME ← $SRC"
                                cp "$SRC" "$APP_BUNDLE/Contents/Frameworks/"
                                COPIED=1
                            fi
                        fi
                        [ -f "$TGT" ] || echo "  Warning: 依赖缺失且无法找回: $DEP"
                        ;;
                esac
            done <<< "$(/usr/bin/otool -L "$LIB" | tail -n +2 | awk '{print $1}')"
            # 修正裸 dylib 自身的 install name（ID）为包内路径：ID 不参与加载解析，
            # 但改写后包内不再残留绝对路径痕迹
            case "$LIB" in
                *.dylib)
                    local CURID
                    CURID=$(/usr/bin/otool -D "$LIB" 2>/dev/null | awk 'NR==2{print $1}')
                    case "$CURID" in
                        /opt/homebrew/*|/usr/local/*)
                            /usr/bin/install_name_tool -id "@executable_path/../Frameworks/$(basename "$CURID")" "$LIB" ;;
                    esac
                    ;;
            esac
        done <<< "$SCAN"
    done

    # 签名应用程序
    echo "Signing application..."
    codesign --force --deep --sign - "$APP_BUNDLE"

    # 验证签名
    echo "Verifying signature..."
    codesign --verify -v "$APP_BUNDLE"

    # 创建DMG（引导式安装窗口）：staging 放入 .app、Applications 快捷方式和背景图，
    # 先打读写盘，用 Finder AppleScript 设置背景/图标布局，再转压缩只读盘。
    # 任一环节失败自动退回「纯 app」朴素布局，不阻塞打包。
    echo "Creating DMG..."
    local DMG_STAGING="$SCRIPT_DIR/dmg-staging"
    local DMG_RW="$SCRIPT_DIR/${CONFIG}-rw.dmg"
    local VOLNAME="QuickShot ${CONFIG} v${VERSION}"
    rm -rf "$DMG_STAGING" "$DMG_RW"
    mkdir -p "$DMG_STAGING/.background"
    # DMG 内的 app 统一命名为 QuickShot.app（对外分发名；构建中间产物叫 QuickShot-Release.app）
    cp -R "$APP_BUNDLE" "$DMG_STAGING/QuickShot.app"
    ln -s /Applications "$DMG_STAGING/Applications"
    local HAS_BG=false
    # 优先用 Pillow 现场生成带版本号的背景图；Pillow 不可用时回退到仓库内静态背景（无版本号）
    if python3 -c "import PIL" >/dev/null 2>&1 \
        && python3 "$SCRIPT_DIR/make_dmg_background.py" "$VERSION" "$DMG_STAGING/.background/background.png" >/dev/null 2>&1; then
        HAS_BG=true
    elif [ -f "$SCRIPT_DIR/dmg-background.png" ]; then
        cp "$SCRIPT_DIR/dmg-background.png" "$DMG_STAGING/.background/background.png"
        HAS_BG=true
        echo "Note: 未检测到 Pillow，使用静态背景图（无版本号文本）"
    fi
    # 同名卷已被挂载（如用户正开着旧 DMG）会让 AppleScript 定位错卷：先弹出
    if [ -d "/Volumes/$VOLNAME" ]; then
        echo "Warning: 检测到同名卷已挂载，先弹出: $VOLNAME"
        hdiutil eject "/Volumes/$VOLNAME" >/dev/null 2>&1 || hdiutil eject -force "/Volumes/$VOLNAME" >/dev/null 2>&1 || true
    fi
    hdiutil create -srcfolder "$DMG_STAGING" -volname "$VOLNAME" -format UDRW -ov "$DMG_RW" >/dev/null
    # 用默认挂载点（/Volumes）挂载——自定义挂载点的卷 Finder 无法按卷名识别，AppleScript 会失
    # 败；从 attach 输出的最后一列解析真实挂载路径，卸载时按该路径操作
    local DMG_MOUNT
    DMG_MOUNT=$(hdiutil attach "$DMG_RW" | tail -1 | awk -F'\t' '{print $NF}' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
    if [ -z "$DMG_MOUNT" ] || [ ! -d "$DMG_MOUNT" ]; then
        echo "Warning: DMG 挂载失败或挂载点解析失败，使用朴素布局"
        DMG_MOUNT=""
    fi
    if [ "$HAS_BG" = true ] && [ -n "$DMG_MOUNT" ]; then
        sleep 2
        # AppleScript 的报错写入打包日志（失败仍有 Warning 提示，不阻塞打包）
        osascript <<OSA >/dev/null || echo "Warning: DMG 布局设置失败，使用朴素布局"
tell application "Finder"
    tell disk "$VOLNAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 860, 520}
        set viewOptions to icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 96
        set background picture of viewOptions to file ".background:background.png"
        set position of item "QuickShot.app" of container window to {170, 240}
        set position of item "Applications" of container window to {490, 240}
        close
        open
        update without registering applications
    end tell
end tell
OSA
        sleep 2
        hdiutil detach "$DMG_MOUNT" >/dev/null 2>&1 || hdiutil detach -force "$DMG_MOUNT" >/dev/null 2>&1 || true
    fi
    # Finder 窗口可能仍占用卷：转换失败则强制卸载后重试一次
    if ! hdiutil convert "$DMG_RW" -format UDZO -o "$DMG_FILE" >/dev/null 2>&1; then
        echo "Warning: DMG 转换被占用，强制卸载后重试"
        hdiutil detach -force "$DMG_MOUNT" >/dev/null 2>&1 || true
        sleep 1
        hdiutil convert "$DMG_RW" -format UDZO -o "$DMG_FILE" >/dev/null
    fi
    rm -f "$DMG_RW"
    rm -rf "$DMG_STAGING" "$DMG_MOUNT"

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
