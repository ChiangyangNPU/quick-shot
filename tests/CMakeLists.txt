# QuickShot 单元测试（Qt Test）
#
# 每个测试可执行文件直接编译被测源文件（依赖闭包见各组 SOURCES），
# 与主目标互不影响。QUICKSHOT_BUILD_TESTS=OFF 可整体关闭。

find_package(Qt6 REQUIRED COMPONENTS Test Core Gui Widgets Network Svg)

# 被测类共同依赖的核心源文件闭包
set(QS_TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/log/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StyleManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TranslationManager.cpp
)

# ---- UpdateManager：版本比较与 release API 解析 ----
add_executable(UpdateManagerVersionTest UpdateManagerVersionTest.cpp
    ${CMAKE_SOURCE_DIR}/src/update/UpdateManager.cpp
    ${QS_TEST_CORE_SOURCES})
target_include_directories(UpdateManagerVersionTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(UpdateManagerVersionTest PRIVATE
    Qt6::Test Qt6::Core Qt6::Network Qt6::Widgets Qt6::Svg)
add_test(NAME UpdateManagerVersionTest COMMAND UpdateManagerVersionTest)

# ---- StyleManager：颜色表完整性与存取回环 ----
add_executable(StyleManagerTest StyleManagerTest.cpp ${QS_TEST_CORE_SOURCES})
target_include_directories(StyleManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(StyleManagerTest PRIVATE
    Qt6::Test Qt6::Core Qt6::Widgets Qt6::Svg)
add_test(NAME StyleManagerTest COMMAND StyleManagerTest)

# ---- TranslationManager：翻译键回退行为 ----
add_executable(TranslationManagerTest TranslationManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TranslationManager.cpp
    ${QS_TEST_CORE_SOURCES})
target_include_directories(TranslationManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(TranslationManagerTest PRIVATE
    Qt6::Test Qt6::Core Qt6::Widgets Qt6::Svg)
add_test(NAME TranslationManagerTest COMMAND TranslationManagerTest)

# ---- OcrDetPostprocess：检测后处理（纯计算，无 GUI 依赖）----
add_executable(OcrDetPostprocessTest OcrDetPostprocessTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/OcrDetPostprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/log/Logger.cpp)
target_include_directories(OcrDetPostprocessTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(OcrDetPostprocessTest PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME OcrDetPostprocessTest COMMAND OcrDetPostprocessTest)

# ---- OcrRecPostprocess：CTC 解码（纯计算，无 GUI 依赖）----
add_executable(OcrRecPostprocessTest OcrRecPostprocessTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/OcrRecPostprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/log/Logger.cpp)
target_include_directories(OcrRecPostprocessTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(OcrRecPostprocessTest PRIVATE Qt6::Test Qt6::Core)
add_test(NAME OcrRecPostprocessTest COMMAND OcrRecPostprocessTest)

# ---- OcrPreprocess：det/rec 缩放 + 归一化 + CHW 转换（纯计算）----
add_executable(OcrPreprocessTest OcrPreprocessTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/OcrPreprocess.cpp
    ${CMAKE_SOURCE_DIR}/src/log/Logger.cpp)
target_include_directories(OcrPreprocessTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(OcrPreprocessTest PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME OcrPreprocessTest COMMAND OcrPreprocessTest)

# ---- GlobalShortcut 键码映射表：Qt 键 → 平台虚拟键码（纯数据，无 GUI 依赖）----
add_executable(GlobalShortcutKeyMapTest GlobalShortcutKeyMapTest.cpp
    ${CMAKE_SOURCE_DIR}/src/shortcut/ShortcutKeyMap.cpp)
target_include_directories(GlobalShortcutKeyMapTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(GlobalShortcutKeyMapTest PRIVATE Qt6::Test Qt6::Core)
if (APPLE)
    # ShortcutKeyMap.cpp 的 macOS 分支使用 Carbon kVK_* 常量
    target_link_libraries(GlobalShortcutKeyMapTest PRIVATE "-framework Carbon")
endif()
add_test(NAME GlobalShortcutKeyMapTest COMMAND GlobalShortcutKeyMapTest)

# ---- HistoryManager：分页/清理/搜索（内存 SQLite，QTEST_MAIN 需 QApplication 跑 QPixmap）----
add_executable(HistoryManagerTest HistoryManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/history/HistoryManager.cpp
    ${CMAKE_SOURCE_DIR}/src/history/ClipboardMonitor.cpp
    ${QS_TEST_CORE_SOURCES})
target_include_directories(HistoryManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(HistoryManagerTest PRIVATE
    Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Svg)
add_test(NAME HistoryManagerTest COMMAND HistoryManagerTest)

# ---- Resizer：选区几何（header-only 纯逻辑，QTEST_MAIN 需 QApplication 跑 QCursor）----
add_executable(ResizerTest ResizerTest.cpp)
target_include_directories(ResizerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ResizerTest PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME ResizerTest COMMAND ResizerTest)

# ---- 马赛克两级绘制缓存：输出等价性（QTEST_MAIN 需 QGuiApplication 跑 QPixmap）----
add_executable(MosaicPaintCacheTest MosaicPaintCacheTest.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/AnnotationInteractionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/AnnotationManager.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/Annotation.cpp)
target_include_directories(MosaicPaintCacheTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MosaicPaintCacheTest PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME MosaicPaintCacheTest COMMAND MosaicPaintCacheTest)

# ---- AnnotationManager：撤销/重做/清除快照/移动记录/缩放（纯逻辑，无 GUI 依赖）----
add_executable(AnnotationManagerTest AnnotationManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/AnnotationManager.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/Annotation.cpp)
target_include_directories(AnnotationManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(AnnotationManagerTest PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME AnnotationManagerTest COMMAND AnnotationManagerTest)
