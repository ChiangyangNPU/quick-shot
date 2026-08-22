cmake_minimum_required(VERSION 3.20)
project(QuickShot VERSION 1.1.0)

# 将项目版本号作为字符串宏注入代码，避免在源码中硬编码版本号
# 使用方式: qApp->applicationVersion() 或 QUICKSHOT_VERSION 宏
add_compile_definitions(QUICKSHOT_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (WIN32)
    set(CMAKE_PREFIX_PATH "C:/Software/Qt/6.10.2/mingw_64")
endif()

# OCR 宏控开关
# ENABLE_OCR_GPU_ACCELERATION: 是否编译 GPU 加速支持
#   ON  - 编译 CoreML(macOS) / DirectML(Windows) 加速代码（默认）
#   OFF - 仅使用 CPU 推理，减小依赖和兼容性问题
option(ENABLE_OCR_GPU_ACCELERATION "Enable GPU acceleration (CoreML/DirectML) for OCR" ON)

message(STATUS "OCR GPU Acceleration: ${ENABLE_OCR_GPU_ACCELERATION}")

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        Multimedia
        Concurrent
        Sql
        Network
        REQUIRED)

# ONNX Runtime
# 方式1: 通过 ONNXRUNTIME_ROOT 环境变量或 CMake 变量指定路径
# 方式2: 如果已安装到系统路径，使用 find_package
if (DEFINED ENV{ONNXRUNTIME_ROOT})
    set(ONNXRUNTIME_ROOT $ENV{ONNXRUNTIME_ROOT})
endif()

# 尝试查找 ONNX Runtime
set(ONNXRUNTIME_FOUND FALSE)
if (DEFINED ONNXRUNTIME_ROOT AND EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime/onnxruntime_cxx_api.h")
    set(ONNXRUNTIME_FOUND TRUE)
elseif (APPLE)
    # macOS: 尝试 Homebrew 路径
    if (EXISTS "/opt/homebrew/include/onnxruntime/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_ROOT "/opt/homebrew")
        set(ONNXRUNTIME_FOUND TRUE)
    elseif (EXISTS "/usr/local/include/onnxruntime/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_ROOT "/usr/local")
        set(ONNXRUNTIME_FOUND TRUE)
    endif()
elseif (WIN32)
    if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include/onnxruntime/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
        set(ONNXRUNTIME_FOUND TRUE)
    else()
        # 自动下载 ONNX Runtime
        set(ONNXRT_VERSION "1.26.0")
        set(ONNXRT_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
        set(ONNXRT_ZIP "${CMAKE_BINARY_DIR}/onnxruntime-win-x64-${ONNXRT_VERSION}.zip")
        set(ONNXRT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRT_VERSION}/onnxruntime-win-x64-${ONNXRT_VERSION}.zip")
        message(STATUS "ONNX Runtime not found, downloading v${ONNXRT_VERSION}...")
        file(DOWNLOAD "${ONNXRT_URL}" "${ONNXRT_ZIP}"
             STATUS DOWNLOAD_STATUS
             SHOW_PROGRESS)
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if (STATUS_CODE EQUAL 0)
            message(STATUS "Download complete, extracting...")
            file(MAKE_DIRECTORY "${ONNXRT_DIR}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${ONNXRT_ZIP}"
                WORKING_DIRECTORY "${ONNXRT_DIR}"
            )
            # 移动子目录内容到 onnxruntime 根目录
            file(GLOB ONNXRT_SUBDIR "${ONNXRT_DIR}/onnxruntime-win-x64-*")
            if (EXISTS "${ONNXRT_SUBDIR}")
                file(COPY "${ONNXRT_SUBDIR}/include" DESTINATION "${ONNXRT_DIR}")
                file(COPY "${ONNXRT_SUBDIR}/lib" DESTINATION "${ONNXRT_DIR}")
                file(REMOVE_RECURSE "${ONNXRT_SUBDIR}")
            endif()
            if (EXISTS "${ONNXRT_DIR}/include/onnxruntime/onnxruntime_cxx_api.h")
                set(ONNXRUNTIME_ROOT "${ONNXRT_DIR}")
                set(ONNXRUNTIME_FOUND TRUE)
                message(STATUS "ONNX Runtime downloaded to: ${ONNXRT_DIR}")
            else()
                message(WARNING "ONNX Runtime download extracted but headers not found")
            endif()
        else()
            message(WARNING "Failed to download ONNX Runtime. OCR will be disabled.")
            message(WARNING "Please manually download from: https://github.com/microsoft/onnxruntime/releases")
            message(WARNING "Extract to: ${ONNXRT_DIR}")
        endif()
    endif()
endif()

if (ONNXRUNTIME_FOUND)
    message(STATUS "ONNX Runtime found at: ${ONNXRUNTIME_ROOT}")
    set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include" "${ONNXRUNTIME_ROOT}/include/onnxruntime")
    if (ENABLE_OCR_GPU_ACCELERATION)
        add_definitions(-DENABLE_OCR_GPU_ACCELERATION)
        message(STATUS "OCR GPU acceleration enabled")
    else()
        message(STATUS "OCR GPU acceleration disabled, using CPU only")
    endif()
    if (WIN32)
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    elseif (APPLE)
        find_library(ONNXRUNTIME_LIB onnxruntime PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH)
        if (NOT ONNXRUNTIME_LIB)
            find_library(ONNXRUNTIME_LIB onnxruntime)
        endif()
    else()
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    endif()
    add_definitions(-DENABLE_OCR)
else()
    find_package(onnxruntime QUIET)
    if (onnxruntime_FOUND)
        message(STATUS "Found onnxruntime via find_package")
        add_definitions(-DENABLE_OCR)
    else()
        message(WARNING "ONNX Runtime not found. OCR functionality will be disabled at compile time.")
    endif()
endif()

include_directories(
        src/core
        src/capture
        src/widgets
        src/recording
        src/log
        src/utils
        src/ocr
        src/history
        src/translate
)

# 添加 ONNX Runtime 头文件路径
if (ONNXRUNTIME_FOUND)
    include_directories(${ONNXRUNTIME_INCLUDE_DIRS})
endif()

if (WIN32)
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        set(WIN32_OPTION WIN32)
    else()
        set(WIN32_OPTION)
    endif()
endif()

add_executable(QuickShot ${WIN32_OPTION}
        src/main.cpp
        src/resources.qrc

        # 快捷键子系统
        src/shortcut/GlobalShortcut.cpp
        src/shortcut/GlobalShortcut.h
        src/shortcut/ShortcutTypes.h
        src/shortcut/ShortcutRegistry.cpp
        src/shortcut/ShortcutRegistry.h
        src/shortcut/ShortcutManager.cpp
        src/shortcut/ShortcutManager.h
        src/shortcut/TrayMenuBuilder.cpp
        src/shortcut/TrayMenuBuilder.h
        src/shortcut/IShortcutHandler.h
        src/shortcut/AnnotationShortcutController.cpp
        src/shortcut/AnnotationShortcutController.h

        # 核心/基础设施
        src/core/TranslationManager.cpp
        src/core/TranslationManager.h
        src/core/StyleManager.cpp
        src/core/StyleManager.h
        src/core/ConfigManager.cpp
        src/core/ConfigManager.h
        src/core/DisplayInfo.cpp
        src/core/DisplayInfo.h
        src/core/Hunter.cpp
        src/core/Hunter.h
        src/core/IPreyDetector.h

        # 截图/选区系统
        src/capture/Annotation.h
        src/capture/AnnotationManager.h
        src/capture/AnnotationInteractionHandler.cpp
        src/capture/AnnotationInteractionHandler.h
        src/capture/Resizer.h
        src/capture/Selector.cpp
        src/capture/Selector.h
        src/capture/SnipScreen.cpp
        src/capture/SnipScreen.h

        # 工具栏控件
        src/widgets/ScreenshotToolBar.cpp
        src/widgets/ScreenshotToolBar.h
        src/widgets/RecordingToolBar.cpp
        src/widgets/RecordingToolBar.h

        src/widgets/BaseToolBar.cpp
        src/widgets/BaseToolBar.h
        src/widgets/PinAnnotationToolBar.cpp
        src/widgets/PinAnnotationToolBar.h
        src/widgets/OverlayTextEdit.cpp
        src/widgets/OverlayTextEdit.h
        src/widgets/PinWindow.cpp
        src/widgets/PinWindow.h
        src/widgets/SettingsWindow.cpp
        src/widgets/SettingsWindow.h
        src/widgets/PrecisionSlider.cpp
        src/widgets/PrecisionSlider.h
        src/widgets/MessageBox.cpp
        src/widgets/MessageBox.h
        src/recording/ScreenRecorder.cpp
        src/recording/ScreenRecorder.h
        src/widgets/RecordingControlWindow.cpp
        src/widgets/RecordingControlWindow.h
        src/log/Logger.cpp
        src/log/Logger.h
        src/utils/Utils.cpp
        src/utils/Utils.h
        src/ocr/OcrEngine.cpp
        src/ocr/OcrEngine.h
        src/ocr/OcrPreprocess.cpp
        src/ocr/OcrPreprocess.h
        src/ocr/OcrDetPostprocess.cpp
        src/ocr/OcrDetPostprocess.h
        src/ocr/OcrRecPostprocess.cpp
        src/ocr/OcrRecPostprocess.h
        src/ocr/OcrResultDialog.cpp
        src/ocr/OcrResultDialog.h

        # 历史记录模块
        src/history/HistoryItem.h
        src/history/HistoryManager.cpp
        src/history/HistoryManager.h
        src/history/ClipboardMonitor.cpp
        src/history/ClipboardMonitor.h
        src/widgets/HistoryWindow.cpp
        src/widgets/HistoryWindow.h

        # 翻译模块
        src/translate/TranslateEngine.cpp
        src/translate/TranslateEngine.h
        src/translate/MyMemoryEngine.cpp
        src/translate/MyMemoryEngine.h
        src/translate/BaiduTranslateEngine.cpp
        src/translate/BaiduTranslateEngine.h
        src/translate/DeepLEngine.cpp
        src/translate/DeepLEngine.h
        src/translate/LibreTranslateEngine.cpp
        src/translate/LibreTranslateEngine.h
        src/translate/TranslateService.cpp
        src/translate/TranslateService.h
        src/widgets/TranslateOverlayWindow.cpp
        src/widgets/TranslateOverlayWindow.h
        src/update/UpdateManager.cpp
        src/update/UpdateManager.h
)

# Add platform-specific files
if (WIN32)
    target_sources(QuickShot PRIVATE
        src/recording/platform/ScreenRecorder_win.cpp
    )
elseif (APPLE)
    target_sources(QuickShot PRIVATE
        src/recording/platform/ScreenRecorder_mac.cpp
        src/recording/platform/ScreenRecorder_mac_helper.mm
    )
elseif (UNIX AND NOT APPLE)
    target_sources(QuickShot PRIVATE
        src/recording/platform/ScreenRecorder_linux.cpp
    )
endif()
target_link_libraries(QuickShot
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::Multimedia
        Qt::Concurrent
        Qt::Sql
        Qt::Network
)

# 链接 ONNX Runtime
if (DEFINED ONNXRUNTIME_LIB AND EXISTS "${ONNXRUNTIME_LIB}")
    target_link_libraries(QuickShot ${ONNXRUNTIME_LIB})
elseif (TARGET onnxruntime::onnxruntime)
    target_link_libraries(QuickShot onnxruntime::onnxruntime)
endif()

# Windows DirectML 支持（可选，运行时动态加载）
if (WIN32 AND ONNXRUNTIME_FOUND AND ENABLE_OCR_GPU_ACCELERATION)
    set(ONNXRUNTIME_DML_HEADER "${ONNXRUNTIME_ROOT}/include/onnxruntime/dml_provider_factory.h")
    set(DIRECTML_DLL "${ONNXRUNTIME_ROOT}/lib/DirectML.dll")
    if (EXISTS "${ONNXRUNTIME_DML_HEADER}" AND EXISTS "${DIRECTML_DLL}")
        message(STATUS "DirectML found, enabling GPU acceleration (runtime dynamic loading)")
        add_definitions(-DENABLE_DIRECTML)
        # 不链接 d3d12.lib dxgi.lib，避免在无 DirectX 12 的机器上启动时 0xc0000142
        # DirectML.dll 通过 LoadLibrary 运行时加载，其内部自行加载 d3d12.dll/dxgi.dll
        # 复制 DirectML.dll 到输出目录
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DIRECTML_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    else()
        message(STATUS "DirectML not found, GPU acceleration disabled on Windows")
    endif()
endif()

if (WIN32)
    target_link_libraries(QuickShot mfplat mfreadwrite mfuuid ole32 dbghelp dwmapi)
endif()

# 构建后复制 ONNX Runtime DLL 到输出目录
if (ONNXRUNTIME_FOUND AND WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

if (APPLE)
    find_library(SCREEN_CAPTURE_KIT ScreenCaptureKit)
    find_library(APP_KIT AppKit)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(CARBON Carbon)
    find_library(AV_FOUNDATION AVFoundation)
    find_library(CORE_VIDEO CoreVideo)
    find_library(CORE_MEDIA CoreMedia)
    find_library(QUARTZ_CORE QuartzCore)
    target_link_libraries(QuickShot ${SCREEN_CAPTURE_KIT} ${APP_KIT} ${CORE_FOUNDATION} ${CORE_GRAPHICS} ${CARBON} ${AV_FOUNDATION} ${CORE_VIDEO} ${CORE_MEDIA} ${QUARTZ_CORE})
endif()

# 拷贝 models 目录到构建输出目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models/ocr"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/models/ocr/mobile"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models/ocr/mobile"
)

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()

    # 复制 Qt SQL 驱动插件
    set(QT_SQL_DRIVER "${QT_INSTALL_PATH}/plugins/sqldrivers/qsqlite${DEBUG_SUFFIX}.dll")
    if (EXISTS "${QT_SQL_DRIVER}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/sqldrivers/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_SQL_DRIVER}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/sqldrivers/")
    endif ()

    # 复制 Qt TLS 后端插件（HTTPS 请求需要，如翻译功能的网络调用）
    set(QT_TLS_DIR "${QT_INSTALL_PATH}/plugins/tls")
    if (EXISTS "${QT_TLS_DIR}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/tls/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_TLS_DIR}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/tls/")
    endif ()

    # 复制 Qt SVG 插件（QSS image: url(:/icons/spinner-*.svg) 渲染 spinbox 箭头等必需）
    # 注意：QSS 的 image: 加载 SVG 走 QIcon/QImageReader，依赖这两个插件；
    # 缺失时 SVG 静默加载失败 → 箭头看不见（文字/边框不依赖插件，照常显示）。
    # 应用/托盘图标用 QSvgRenderer 直接渲染，不依赖这两个插件。
    set(QT_IMAGEFORMAT_SVG "${QT_INSTALL_PATH}/plugins/imageformats/qsvg${DEBUG_SUFFIX}.dll")
    if (EXISTS "${QT_IMAGEFORMAT_SVG}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/imageformats/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_IMAGEFORMAT_SVG}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/imageformats/")
    endif ()

    set(QT_ICONENGINE_SVG "${QT_INSTALL_PATH}/plugins/iconengines/qsvgicon${DEBUG_SUFFIX}.dll")
    if (EXISTS "${QT_ICONENGINE_SVG}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/iconengines/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_ICONENGINE_SVG}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/iconengines/")
    endif ()

    foreach (QT_LIB Core Gui Widgets Svg Multimedia Concurrent Qml Network Sql)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach (QT_LIB)

    # 复制 Qt 官方翻译文件到输出目录 translations/（QColorDialog 等标准控件的本地化）
    # 对应项目支持的 6 种语言：zh_CN/zh_TW/ja/ko（zh_HK 复用 zh_TW；en 为 Qt 默认无需翻译）
    # TranslationManager::installQtTranslations 优先从应用目录加载，随包分发后无需依赖 Qt 安装路径
    foreach (QT_QM zh_CN zh_TW ja ko)
        set(QT_QM_FILE "${QT_INSTALL_PATH}/translations/qtbase_${QT_QM}.qm")
        if (EXISTS "${QT_QM_FILE}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${QT_QM_FILE}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/translations/")
        endif ()
    endforeach ()

    endif ()
