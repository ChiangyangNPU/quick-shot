#include "StyleManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QPointer>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QLineEdit>

#include "Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#ifdef Q_OS_MACOS
#include <objc/message.h>
#include <objc/runtime.h>
#endif

// .cpp 级循环依赖：ConfigManager.cpp 会 include StyleManager.h（用 DEFAULT_* 播种），
// 此文件 include ConfigManager.h（initFromConfig 用 value() 读取持久化）。
// 头文件间仅前向声明 class ConfigManager，见 StyleManager.h 顶部注释。
#include "ConfigManager.h"

// 静态成员变量初始化
QString StyleManager::s_toolbarButtonStyle = "text";                    ///< 工具栏按钮当前样式（文字模式）
QString StyleManager::s_theme = "light";                                ///< 当前主题（浅色）
int StyleManager::s_defaultPenWidth = 5;                                      ///< 画笔默认粗细
int StyleManager::s_defaultFontSize = 28;                                     ///< 文本默认字号
int StyleManager::s_defaultEraserWidth = 5;                                   ///< 橡皮擦默认粗细
int StyleManager::s_defaultMosaicSize = 5;                                    ///< 马赛克默认大小

// 默认颜色
const QColor StyleManager::DEFAULT_RECORD_BORDER_COLOR = QColor("#4CAF50");   ///< 录屏框默认颜色
const QColor StyleManager::DEFAULT_CAPTURE_BORDER_COLOR = QColor("#4da6ff"); ///< 截屏框默认颜色
const QColor StyleManager::DEFAULT_TOOLBAR_BG_COLOR = QColor("#cfcfcf");         ///< 工具栏默认背景颜色
const QColor StyleManager::DEFAULT_RECORD_CONTROL_BG_COLOR = QColor("#cfcfcf");  ///< 录屏控制栏默认背景颜色
const QColor StyleManager::DEFAULT_TOOLBAR_BTN_COLOR = QColor("#e0e0e0");         ///< 工具栏按钮默认颜色
const QColor StyleManager::DEFAULT_TOOLBAR_TEXT_COLOR = QColor("#000");        ///< 工具栏按钮文字默认颜色
const QColor StyleManager::DEFAULT_TOOLBAR_BUTTON_HOVER_COLOR = QColor("#fff"); ///< 工具栏按钮悬停默认颜色
const QColor StyleManager::DEFAULT_TOOLBAR_BUTTON_DISABLED_COLOR = QColor("#d1d1d1"); ///< 工具栏按钮禁用默认颜色
const QColor StyleManager::DEFAULT_SUB_TOOLBAR_BG_COLOR = QColor("#cfcfcf");      ///< 子工具栏默认背景颜色
const QColor StyleManager::DEFAULT_SETTING_BUTTON_BG_COLOR = QColor("#e0e0e0"); ///< 设置窗口按钮默认背景颜色
const QColor StyleManager::DEFAULT_SETTING_BUTTON_TEXT_COLOR = QColor("#000000"); ///< 设置窗口按钮文字默认颜色
const QColor StyleManager::DEFAULT_TOOLBAR_BUTTON_CHECKED_COLOR = QColor("#0078d7"); ///< 工具栏按钮选中默认颜色
const QColor StyleManager::DEFAULT_CLOSE_BUTTON_BG_COLOR = QColor("#cc0000");     ///< 关闭按钮默认背景颜色
const QColor StyleManager::DEFAULT_CLOSE_BUTTON_HOVER_COLOR = QColor("#ff3333");  ///< 关闭按钮默认悬停颜色
const QColor StyleManager::DEFAULT_TAB_WIDGET_BG_COLOR = QColor("#ffffff");       ///< 选项卡背景默认颜色
const QColor StyleManager::DEFAULT_TAB_BUTTON_BG_COLOR = QColor("#e0e0e0");       ///< 选项卡按钮背景颜色
const QColor StyleManager::DEFAULT_TAB_BUTTON_TEXT_COLOR = QColor("#000000");     ///< 选项卡按钮文字默认颜色
const QColor StyleManager::DEFAULT_TAB_BUTTON_SELECTED_BG_COLOR = QColor("#ffffff");  ///< 选项卡按钮选中背景默认颜色
const QColor StyleManager::DEFAULT_TAB_BUTTON_SELECTED_TEXT_COLOR = QColor("#000000"); ///< 选项卡按钮选中文字默认颜色
const QColor StyleManager::DEFAULT_HANDLE_CIRCLE_COLOR = QColor("#2563EB");            ///< 角手柄圆形默认颜色
const QColor StyleManager::DEFAULT_HANDLE_CLOSE_COLOR = QColor("#DC2626");             ///< 角手柄关闭按钮默认颜色
const QString StyleManager::DEFAULT_TOOLBAR_BUTTON_STYLE = "text";              ///< 工具栏按钮默认样式（文字模式）
const QString StyleManager::DEFAULT_THEME = "light";                            ///< 默认主题（浅色）

/**
 * @brief 颜色转 QSS 字符串（保留 alpha 通道）
 * @param c 颜色
 * @return alpha<255 时输出 rgba(r,g,b,a)，否则输出 #rrggbb
 * @note QColor::name() 会丢弃 alpha，深色主题色板大量依赖半透明色，
 *       所有拼 QSS 处必须经此工具，否则深色样式串失效
 * @author chiangyang
 */
static QString qssColor(const QColor &c) {
    if (c.alpha() < 255) {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    return c.name();
}

// ============ 颜色存取（表驱动） ============
// 21 个颜色的当前值按 StyleColorId 枚举顺序存放于 Meyers 单例，
// 首次访问时以 DEFAULT_* 初始化；initFromConfig/resetToDefaults/设置页均经此存取。
static std::array<QColor, StyleManager::kStyleColorCount>& colorStore() {
    static std::array<QColor, StyleManager::kStyleColorCount> store = {
        StyleManager::DEFAULT_CAPTURE_BORDER_COLOR,
        StyleManager::DEFAULT_RECORD_BORDER_COLOR,
        StyleManager::DEFAULT_TOOLBAR_BG_COLOR,
        StyleManager::DEFAULT_SUB_TOOLBAR_BG_COLOR,
        StyleManager::DEFAULT_RECORD_CONTROL_BG_COLOR,
        StyleManager::DEFAULT_TOOLBAR_BTN_COLOR,
        StyleManager::DEFAULT_TOOLBAR_TEXT_COLOR,
        StyleManager::DEFAULT_TOOLBAR_BUTTON_HOVER_COLOR,
        StyleManager::DEFAULT_TOOLBAR_BUTTON_DISABLED_COLOR,
        StyleManager::DEFAULT_TOOLBAR_BUTTON_CHECKED_COLOR,
        StyleManager::DEFAULT_CLOSE_BUTTON_BG_COLOR,
        StyleManager::DEFAULT_CLOSE_BUTTON_HOVER_COLOR,
        StyleManager::DEFAULT_SETTING_BUTTON_BG_COLOR,
        StyleManager::DEFAULT_SETTING_BUTTON_TEXT_COLOR,
        StyleManager::DEFAULT_TAB_WIDGET_BG_COLOR,
        StyleManager::DEFAULT_TAB_BUTTON_BG_COLOR,
        StyleManager::DEFAULT_TAB_BUTTON_TEXT_COLOR,
        StyleManager::DEFAULT_TAB_BUTTON_SELECTED_BG_COLOR,
        StyleManager::DEFAULT_TAB_BUTTON_SELECTED_TEXT_COLOR,
        StyleManager::DEFAULT_HANDLE_CIRCLE_COLOR,
        StyleManager::DEFAULT_HANDLE_CLOSE_COLOR
    };
    return store;
}

/**
 * @brief 读取指定颜色的当前值
 * @author chiangyang
 */
QColor StyleManager::color(StyleColorId id) {
    return colorStore()[static_cast<size_t>(id)];
}

/**
 * @brief 设置指定颜色的当前值
 * @author chiangyang
 */
void StyleManager::setColor(StyleColorId id, const QColor &color) {
    colorStore()[static_cast<size_t>(id)] = color;
}

// 逐色 getter/setter：公共 API 保留，实现委托 color()/setColor()
QColor StyleManager::getCaptureBorderColor() { return color(StyleColorId::CaptureBorder); }
void StyleManager::setCaptureBorderColor(const QColor &color) { setColor(StyleColorId::CaptureBorder, color); }
QColor StyleManager::getRecordBorderColor() { return color(StyleColorId::RecordBorder); }
void StyleManager::setRecordBorderColor(const QColor &color) { setColor(StyleColorId::RecordBorder, color); }
QColor StyleManager::getToolbarBgColor() { return color(StyleColorId::ToolbarBg); }
void StyleManager::setToolbarBgColor(const QColor &color) { setColor(StyleColorId::ToolbarBg, color); }
QColor StyleManager::getSubToolbarBgColor() { return color(StyleColorId::SubToolbarBg); }
void StyleManager::setSubToolbarBgColor(const QColor &color) { setColor(StyleColorId::SubToolbarBg, color); }
QColor StyleManager::getRecordControlBgColor() { return color(StyleColorId::RecordControlBg); }
void StyleManager::setRecordControlBgColor(const QColor &color) { setColor(StyleColorId::RecordControlBg, color); }
QColor StyleManager::getToolbarBtnColor() { return color(StyleColorId::ToolbarBtn); }
void StyleManager::setToolbarBtnColor(const QColor &color) { setColor(StyleColorId::ToolbarBtn, color); }
QColor StyleManager::getToolbarTextColor() { return color(StyleColorId::ToolbarText); }
void StyleManager::setToolbarTextColor(const QColor &color) { setColor(StyleColorId::ToolbarText, color); }
QColor StyleManager::getToolbarButtonHoverColor() { return color(StyleColorId::ToolbarButtonHover); }
void StyleManager::setToolbarButtonHoverColor(const QColor &color) { setColor(StyleColorId::ToolbarButtonHover, color); }
QColor StyleManager::getToolbarButtonDisabledColor() { return color(StyleColorId::ToolbarButtonDisabled); }
void StyleManager::setToolbarButtonDisabledColor(const QColor &color) { setColor(StyleColorId::ToolbarButtonDisabled, color); }
QColor StyleManager::getToolbarButtonCheckedColor() { return color(StyleColorId::ToolbarButtonChecked); }
void StyleManager::setToolbarButtonCheckedColor(const QColor &color) { setColor(StyleColorId::ToolbarButtonChecked, color); }
QColor StyleManager::getCloseButtonBgColor() { return color(StyleColorId::CloseButtonBg); }
void StyleManager::setCloseButtonBgColor(const QColor &color) { setColor(StyleColorId::CloseButtonBg, color); }
QColor StyleManager::getCloseButtonHoverColor() { return color(StyleColorId::CloseButtonHover); }
void StyleManager::setCloseButtonHoverColor(const QColor &color) { setColor(StyleColorId::CloseButtonHover, color); }
QColor StyleManager::getSettingButtonBgColor() { return color(StyleColorId::SettingButtonBg); }
void StyleManager::setSettingButtonBgColor(const QColor &color) { setColor(StyleColorId::SettingButtonBg, color); }
QColor StyleManager::getSettingButtonTextColor() { return color(StyleColorId::SettingButtonText); }
void StyleManager::setSettingButtonTextColor(const QColor &color) { setColor(StyleColorId::SettingButtonText, color); }
QColor StyleManager::getTabWidgetBgColor() { return color(StyleColorId::TabWidgetBg); }
void StyleManager::setTabWidgetBgColor(const QColor &color) { setColor(StyleColorId::TabWidgetBg, color); }
QColor StyleManager::getTabButtonBgColor() { return color(StyleColorId::TabButtonBg); }
void StyleManager::setTabButtonBgColor(const QColor &color) { setColor(StyleColorId::TabButtonBg, color); }
QColor StyleManager::getTabButtonTextColor() { return color(StyleColorId::TabButtonText); }
void StyleManager::setTabButtonTextColor(const QColor &color) { setColor(StyleColorId::TabButtonText, color); }
QColor StyleManager::getTabButtonSelectedBgColor() { return color(StyleColorId::TabButtonSelectedBg); }
void StyleManager::setTabButtonSelectedBgColor(const QColor &color) { setColor(StyleColorId::TabButtonSelectedBg, color); }
QColor StyleManager::getTabButtonSelectedTextColor() { return color(StyleColorId::TabButtonSelectedText); }
void StyleManager::setTabButtonSelectedTextColor(const QColor &color) { setColor(StyleColorId::TabButtonSelectedText, color); }
QColor StyleManager::getHandleCircleColor() { return color(StyleColorId::HandleCircle); }
void StyleManager::setHandleCircleColor(const QColor &color) { setColor(StyleColorId::HandleCircle, color); }
QColor StyleManager::getHandleCloseColor() { return color(StyleColorId::HandleClose); }
void StyleManager::setHandleCloseColor(const QColor &color) { setColor(StyleColorId::HandleClose, color); }

// ============ 颜色配置元数据表（单一数据源） ============
// 此表是颜色配置的唯一权威：ConfigManager 播种默认值（ensureDefaultValues /
// createDefaultConfig）、SettingsWindow 构建设置 UI、initFromConfig 从持久化恢复，
// 三方均遍历 colorSettingTable()。与 ShortcutTypes.h 的 kShortcutConfigs 同一"数据驱动"惯用法。
// 注意：表行顺序必须与 StyleColorId 枚举顺序一致（SettingsWindow 用表位置作按钮数组索引）。
const std::array<StyleManager::StyleColorSetting, StyleManager::kStyleColorCount>&
StyleManager::colorSettingTable() {
    static const std::array<StyleColorSetting, kStyleColorCount> table = {{
        // Border 组
        { StyleColorId::CaptureBorder, StyleColorCategory::Border, StyleColorSignal::None,
          "captureBorderColor", "style.captureBorderColor", "Capture Border:",
          &getCaptureBorderColor, &setCaptureBorderColor, DEFAULT_CAPTURE_BORDER_COLOR },
        { StyleColorId::RecordBorder, StyleColorCategory::Border, StyleColorSignal::None,
          "recordBorderColor", "style.recordBorderColor", "Record Border:",
          &getRecordBorderColor, &setRecordBorderColor, DEFAULT_RECORD_BORDER_COLOR },

        // Toolbar 组
        { StyleColorId::ToolbarBg, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarBgColor", "style.toolbarBgColor", "Toolbar Background:",
          &getToolbarBgColor, &setToolbarBgColor, DEFAULT_TOOLBAR_BG_COLOR },
        { StyleColorId::SubToolbarBg, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "subToolbarBgColor", "style.subToolbarBgColor", "Sub Toolbar Background:",
          &getSubToolbarBgColor, &setSubToolbarBgColor, DEFAULT_SUB_TOOLBAR_BG_COLOR },
        { StyleColorId::RecordControlBg, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "recordControlBgColor", "style.recordControlBgColor", "Record Control Background:",
          &getRecordControlBgColor, &setRecordControlBgColor, DEFAULT_RECORD_CONTROL_BG_COLOR },
        { StyleColorId::ToolbarBtn, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarBtnColor", "style.toolbarBtnColor", "Toolbar Button:",
          &getToolbarBtnColor, &setToolbarBtnColor, DEFAULT_TOOLBAR_BTN_COLOR },
        { StyleColorId::ToolbarText, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarTextColor", "style.toolbarTextColor", "Toolbar Text:",
          &getToolbarTextColor, &setToolbarTextColor, DEFAULT_TOOLBAR_TEXT_COLOR },
        { StyleColorId::ToolbarButtonHover, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarButtonHoverColor", "style.toolbarButtonHoverColor", "Button Hover:",
          &getToolbarButtonHoverColor, &setToolbarButtonHoverColor, DEFAULT_TOOLBAR_BUTTON_HOVER_COLOR },
        { StyleColorId::ToolbarButtonDisabled, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarButtonDisabledColor", "style.toolbarButtonDisabledColor", "Button Disabled:",
          &getToolbarButtonDisabledColor, &setToolbarButtonDisabledColor, DEFAULT_TOOLBAR_BUTTON_DISABLED_COLOR },
        { StyleColorId::ToolbarButtonChecked, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "toolbarButtonCheckedColor", "style.toolbarButtonCheckedColor", "Button Checked:",
          &getToolbarButtonCheckedColor, &setToolbarButtonCheckedColor, DEFAULT_TOOLBAR_BUTTON_CHECKED_COLOR },
        { StyleColorId::CloseButtonBg, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "closeButtonBgColor", "style.closeButtonBgColor", "Cancel Button:",
          &getCloseButtonBgColor, &setCloseButtonBgColor, DEFAULT_CLOSE_BUTTON_BG_COLOR },
        { StyleColorId::CloseButtonHover, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "closeButtonHoverColor", "style.closeButtonHoverColor", "Cancel Button Hover:",
          &getCloseButtonHoverColor, &setCloseButtonHoverColor, DEFAULT_CLOSE_BUTTON_HOVER_COLOR },

        // SettingButton 组（无 UI 行，仅 save/load/initFromConfig）
        { StyleColorId::SettingButtonBg, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "settingButtonBgColor", "style.settingButtonBgColor", "",
          &getSettingButtonBgColor, &setSettingButtonBgColor, DEFAULT_SETTING_BUTTON_BG_COLOR },
        { StyleColorId::SettingButtonText, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "settingButtonTextColor", "style.settingButtonTextColor", "",
          &getSettingButtonTextColor, &setSettingButtonTextColor, DEFAULT_SETTING_BUTTON_TEXT_COLOR },

        // Tab 组
        { StyleColorId::TabWidgetBg, StyleColorCategory::TabWidgetBg, StyleColorSignal::TabWidgetBg,
          "tabWidgetBgColor", "style.tabWidgetBgColor", "Tab Background:",
          &getTabWidgetBgColor, &setTabWidgetBgColor, DEFAULT_TAB_WIDGET_BG_COLOR },
        { StyleColorId::TabButtonBg, StyleColorCategory::TabButton, StyleColorSignal::None,
          "tabButtonBgColor", "style.tabButtonBgColor", "Tab Button Background:",
          &getTabButtonBgColor, &setTabButtonBgColor, DEFAULT_TAB_BUTTON_BG_COLOR },
        { StyleColorId::TabButtonText, StyleColorCategory::TabButton, StyleColorSignal::None,
          "tabButtonTextColor", "style.tabButtonTextColor", "Tab Button Text:",
          &getTabButtonTextColor, &setTabButtonTextColor, DEFAULT_TAB_BUTTON_TEXT_COLOR },
        { StyleColorId::TabButtonSelectedBg, StyleColorCategory::TabButton, StyleColorSignal::None,
          "tabButtonSelectedBgColor", "style.tabButtonSelectedBgColor", "Tab Button Selected Background:",
          &getTabButtonSelectedBgColor, &setTabButtonSelectedBgColor, DEFAULT_TAB_BUTTON_SELECTED_BG_COLOR },
        { StyleColorId::TabButtonSelectedText, StyleColorCategory::TabButton, StyleColorSignal::None,
          "tabButtonSelectedTextColor", "style.tabButtonSelectedTextColor", "Tab Button Selected Text:",
          &getTabButtonSelectedTextColor, &setTabButtonSelectedTextColor, DEFAULT_TAB_BUTTON_SELECTED_TEXT_COLOR },

        // 角手柄组（无 UI 行，仅 save/load/initFromConfig）
        { StyleColorId::HandleCircle, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "handleCircleColor", "style.handleCircleColor", "",
          &getHandleCircleColor, &setHandleCircleColor, DEFAULT_HANDLE_CIRCLE_COLOR },
        { StyleColorId::HandleClose, StyleColorCategory::Toolbar, StyleColorSignal::None,
          "handleCloseColor", "style.handleCloseColor", "",
          &getHandleCloseColor, &setHandleCloseColor, DEFAULT_HANDLE_CLOSE_COLOR },
    }};
    return table;
}

/**
 * @brief 主题色板（按 StyleColorId 枚举顺序返回 21 项预设颜色）
 * @param dark true 返回深色色板，false 返回浅色色板（即各项 DEFAULT_*）
 * @return 21 项颜色的数组
 * @note 浅色直接取 colorSettingTable() 的默认色；深色为 HUD 风格预设
 *       （参考滚动截图提示条配色）。截屏/录屏边框、角手柄、关闭按钮等
 *       屏幕叠加物/红色语义不随主题变化，保持默认色。
 * @author chiangyang
 */
static std::array<QColor, StyleManager::kStyleColorCount> themePalette(bool dark) {
    std::array<QColor, StyleManager::kStyleColorCount> palette{};
    const auto& table = StyleManager::colorSettingTable();
    for (int i = 0; i < StyleManager::kStyleColorCount; ++i) {
        palette[static_cast<size_t>(i)] = table[static_cast<size_t>(i)].defaultColor;
    }
    if (!dark) {
        return palette;
    }
    using Id = StyleManager::StyleColorId;
    // HUD 底板与按钮填充（rgba(22,22,26,248) 底板 + 半透明白填充）
    palette[static_cast<size_t>(Id::ToolbarBg)]             = QColor(22, 22, 26, 248);
    palette[static_cast<size_t>(Id::SubToolbarBg)]          = QColor(22, 22, 26, 248);
    palette[static_cast<size_t>(Id::RecordControlBg)]       = QColor(22, 22, 26, 248);
    palette[static_cast<size_t>(Id::ToolbarBtn)]            = QColor(255, 255, 255, 48);
    palette[static_cast<size_t>(Id::ToolbarText)]           = QColor("#f2f2f2");
    palette[static_cast<size_t>(Id::ToolbarButtonHover)]    = QColor(255, 255, 255, 78);
    palette[static_cast<size_t>(Id::ToolbarButtonDisabled)] = QColor(255, 255, 255, 20);
    // ToolbarButtonChecked 保留 #0078d7（深色下蓝色高亮清晰）
    palette[static_cast<size_t>(Id::SettingButtonBg)]       = QColor(255, 255, 255, 48);
    palette[static_cast<size_t>(Id::SettingButtonText)]     = QColor("#f2f2f2");
    palette[static_cast<size_t>(Id::TabWidgetBg)]           = QColor("#1a1a1e");
    palette[static_cast<size_t>(Id::TabButtonBg)]           = QColor(255, 255, 255, 30);
    palette[static_cast<size_t>(Id::TabButtonText)]         = QColor("#f2f2f2");
    palette[static_cast<size_t>(Id::TabButtonSelectedBg)]   = QColor("#23232a");
    palette[static_cast<size_t>(Id::TabButtonSelectedText)] = QColor("#ffffff");
    // 屏幕叠加物/红色语义不随主题：CaptureBorder/RecordBorder/HandleCircle/
    // HandleClose/CloseButtonBg/CloseButtonHover 保持默认色
    return palette;
}

/**
 * @brief 获取当前主题标识
 * @author chiangyang
 */
QString StyleManager::theme() {
    return s_theme;
}

/**
 * @brief 设置当前主题标识（仅更新标量，不写配置、不铺色板）
 * @author chiangyang
 */
void StyleManager::setTheme(const QString &theme) {
    s_theme = (theme == QLatin1String("dark")) ? QStringLiteral("dark") : DEFAULT_THEME;
}

/**
 * @brief 获取当前主题模式枚举
 * @author chiangyang
 */
StyleManager::ThemeMode StyleManager::themeMode() {
    return (s_theme == QLatin1String("dark")) ? ThemeMode::Dark : ThemeMode::Light;
}

/**
 * @brief 应用主题：批量覆盖颜色表并持久化全部颜色键与主题键
 * @author chiangyang
 */
void StyleManager::applyTheme(const QString &theme, ConfigManager* cm) {
    const bool dark = (theme == QLatin1String("dark"));
    s_theme = dark ? QStringLiteral("dark") : DEFAULT_THEME;

    const auto palette = themePalette(dark);
    const auto& table = colorSettingTable();
    for (int i = 0; i < kStyleColorCount; ++i) {
        const auto& s = table[static_cast<size_t>(i)];
        const QColor& c = palette[static_cast<size_t>(i)];
        s.setter(c);
        if (cm) {
            // HexArgb 保留 alpha（QColor("#AARRGGBB") 读取端兼容）
            cm->setValue(QString("style/%1").arg(s.settingsKey), c.name(QColor::HexArgb));
        }
    }
    if (cm) {
        cm->setValue("style/theme", s_theme);
    }
    LOG_INFO(QString("Theme applied: %1").arg(s_theme));
}

/**
 * @brief 工具栏 SVG 图标染色色
 * @author chiangyang
 */
QColor StyleManager::toolbarIconTint() {
    return (themeMode() == ThemeMode::Dark) ? QColor("#f2f2f2") : QColor();
}

void StyleManager::applyDarkTitleBar(WId windowId) {
#ifdef Q_OS_WIN
    if (!windowId) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    const BOOL dark = (themeMode() == ThemeMode::Dark) ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE：Windows 11/10 20H1+ 官方属性值为 20，
    // Windows 10 1809~1909 的旧属性值为 19，两个都尝试以兼容旧系统
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
#elif defined(Q_OS_MACOS)
    if (!windowId) {
        return;
    }
    // QWidget::winId() 在 macOS 上是 NSView*，取其所属 NSWindow
    id nsView = reinterpret_cast<id>(windowId);
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) {
        return;
    }
    const bool dark = (themeMode() == ThemeMode::Dark);
    const char *appearanceName = dark ? "NSAppearanceNameDarkAqua" : "NSAppearanceNameAqua";
    // NSString *name = [NSString stringWithUTF8String:appearanceName]
    id name = ((id (*)(id, SEL, const char *))objc_msgSend)(
        (id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), appearanceName);
    // NSAppearance *appearance = [NSAppearance appearanceNamed:name]
    id appearance = ((id (*)(id, SEL, id))objc_msgSend)(
        (id)objc_getClass("NSAppearance"), sel_registerName("appearanceNamed:"), name);
    if (!appearance) {
        return;
    }
    // [nsWindow setAppearance:appearance]，窗口外观（含标题栏）随主题切换
    ((void (*)(id, SEL, id))objc_msgSend)(nsWindow, sel_registerName("setAppearance:"), appearance);
#else
    Q_UNUSED(windowId);
#endif
}

/**
 * @brief 从配置初始化样式（依赖注入）
 * @param cm ConfigManager 指针；nullptr 时重置为默认值
 *
 * 应用启动时在 ConfigManager::setInstance 之后、任何窗口创建之前调用一次。
 * 遍历颜色表恢复持久化颜色，显式恢复标注工具默认值与工具栏按钮样式，
 * 使样式恢复与窗口构造顺序解耦（不再依赖 SettingsWindow 先于 SnipScreen）。
 * @author chiangyang
 */
void StyleManager::initFromConfig(ConfigManager* cm) {
    if (!cm) {
        resetToDefaults();
        return;
    }

    // 遍历颜色表恢复全部持久化颜色（缺键时回落默认色）
    for (const auto& s : colorSettingTable()) {
        s.setter(QColor(cm->value(
            QString("style/%1").arg(s.settingsKey), s.defaultColor.name(QColor::HexArgb)).toString()));
    }

    // 标注工具默认值（带边界 clamp，与 ConfigManager 原 getDefault* 一致）
    setDefaultPenWidth(qBound(1, cm->value("style/defaultPenWidth", DEFAULT_PEN_WIDTH).toInt(), 20));
    setDefaultFontSize(qBound(8, cm->value("style/defaultFontSize", DEFAULT_FONT_SIZE).toInt(), 48));
    setDefaultEraserWidth(qBound(1, cm->value("style/defaultEraserWidth", DEFAULT_ERASER_WIDTH).toInt(), 20));
    setDefaultMosaicSize(qBound(1, cm->value("style/defaultMosaicSize", DEFAULT_MOSAIC_SIZE).toInt(), 20));

    // 工具栏按钮样式
    setToolbarButtonStyle(cm->value("style/toolbarButtonStyle", DEFAULT_TOOLBAR_BUTTON_STYLE).toString());

    // 主题标量（颜色键已被上方循环持久化覆盖，天然恢复，无需按主题重铺色板）
    setTheme(cm->value("style/theme", DEFAULT_THEME).toString());
}

namespace {

/**
 * @brief 不绘制勾选指示器的条目代理（配合 QSS :selected 高亮表达选中）
 *
 * 同时修正弹窗锚定：macOS 菜单式弹窗会把弹窗锚在 combo 顶部（正好盖住
 * 本体），本代理监听弹窗显示事件，将其强制定位到 combo 正下方。
 * @author chiangyang
 */
class HighlightOnlyPopupDelegate final : public QStyledItemDelegate {
public:
    explicit HighlightOnlyPopupDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        // 去掉 ✓ 勾选指示器（macOS 菜单式弹窗会给当前项设置勾选列，挤压文字宽度）
        option->features &= ~QStyleOptionViewItem::HasCheckIndicator;
    }

    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            if (auto *combo = qobject_cast<QComboBox *>(parent())) {
                if (combo->view() && combo->view()->window() == watched) {
                    QPointer<QComboBox> comboGuard(combo);
                    QPointer<QWidget> popupGuard(combo->view()->window());
                    // Qt 完成自身定位后再修正（单发延迟到下一轮事件循环）
                    QTimer::singleShot(0, this, [comboGuard, popupGuard]() {
                        if (!comboGuard || !popupGuard) return;

                        // ① 弹窗宽度自适应最长条目文本，且不窄于 combo 本体
                        //   （弹窗宽度跟随内部视图，视图默认 sizeHint 在不同缩放/
                        //     字体度量下可能窄于文字，导致条目中间省略）
                        int maxItemWidth = 0;
                        const QFontMetrics fm(comboGuard->font());
                        for (int i = 0; i < comboGuard->count(); ++i) {
                            maxItemWidth = qMax(maxItemWidth,
                                fm.horizontalAdvance(comboGuard->itemText(i)));
                        }
                        // 对齐基准用 combo 的实际宽度（布局可能把它拉伸得
                        // 比最小宽度更宽，弹窗必须与之等宽）
                        const int minWidth = qMax(
                            maxItemWidth + qRound(fm.horizontalAdvance('M') * 2.0),
                            comboGuard->width());
                        if (QAbstractItemView *view = comboGuard->view()) {
                            view->setMinimumWidth(minWidth);
                            // 仅按需加宽容器（宽度 = 视图最小宽 + 容器边框），
                            // 高度保留 Qt 的计算结果——对容器 adjustSize 会把
                            // 视图高度破坏成 0
                            const int chrome = qMax(0, popupGuard->width() - view->width());
                            const int neededWidth = minWidth + chrome;
                            if (popupGuard->width() < neededWidth) {
                                popupGuard->resize(neededWidth, popupGuard->height());
                            }
                        }

                        // ② 锚定到 combo 正下方
                        const QPoint below = comboGuard->mapToGlobal(QPoint(0, comboGuard->height()));
                        if (popupGuard->pos() != below) {
                            popupGuard->move(below);
                        }
                    });
                }
            }
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }
};

} // namespace

/**
 * @brief 下拉框弹窗统一为"高亮选中"风格
 * @author chiangyang
 */
void StyleManager::useHighlightOnlyPopup(QComboBox *combo) {
    if (!combo) return;
    auto *delegate = new HighlightOnlyPopupDelegate(combo);
    combo->setItemDelegate(delegate);
    // 监听弹窗窗口的显示事件（delegate 自身做过滤器）
    if (QWidget *popupWin = combo->view()->window()) {
        popupWin->installEventFilter(delegate);
    }
}

/**
 * @brief 工具栏圆角半径（对齐 QSS border-radius: 0.3em 的视觉意图）
 * @author chiangyang
 */
int StyleManager::toolbarCornerRadius(const QWidget *widget) {
    return qRound(0.3 * widget->fontMetrics().horizontalAdvance(QLatin1Char('M')));
}

/**
 * @brief 加载SVG图标
 * @param iconPath SVG图标路径
 * @param size 图标大小
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath, const QSize &size) {
    return loadSvgIcon(iconPath, size, QColor());
}

/**
 * @brief 加载SVG图标
 * @param iconPath SVG图标路径
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath) {
    return loadSvgIcon(iconPath, QSize(24, 24), QColor());
}

/**
 * @brief 加载SVG图标并染色
 * @param iconPath SVG图标路径
 * @param size 图标大小
 * @param tint 染色颜色；无效色（QColor()）时不染色
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath, const QSize &size, const QColor &tint) {
    QSvgRenderer svgRenderer(iconPath);
    if (svgRenderer.isValid()) {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        svgRenderer.render(&painter);
        // 染色：SourceIn 用 tint 整体重着色，保留图标 alpha 形状
        if (tint.isValid()) {
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), tint);
        }
        painter.end();
        QIcon icon(pixmap);
        if (!icon.isNull()) {
            LOG_INFO(QString("Loaded SVG icon: %1").arg(iconPath));
            return icon;
        } else {
            LOG_WARNING(QString("Failed to create icon from SVG: %1").arg(iconPath));
        }
    } else {
        LOG_WARNING(QString("Invalid SVG renderer for: %1").arg(iconPath));
    }
    // 如果加载失败，返回一个默认的箭头图标
    return QApplication::style()->standardIcon(QStyle::SP_ArrowRight);
}

/**
 * @brief 加载SVG图标并染色（默认 24x24）
 * @param iconPath SVG图标路径
 * @param tint 染色颜色；无效色（QColor()）时不染色
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath, const QColor &tint) {
    return loadSvgIcon(iconPath, QSize(24, 24), tint);
}

/**
 * @brief 加载应用图标
 * @return 加载的应用图标
 * @author chiangyang
 */
QIcon StyleManager::loadAppIcon() {
    // 尝试加载SVG图标
    QIcon svgIcon = loadSvgIcon(":/icons/app.svg", QSize(32, 32));
    if (!svgIcon.isNull()) {
        LOG_INFO("Using SVG icon");
        return svgIcon;
    }
    
    // 如果SVG加载失败，尝试加载PNG图标
    QIcon pngIcon(":/icons/app.png");
    if (!pngIcon.isNull()) {
        LOG_INFO("Using PNG icon");
        return pngIcon;
    }
    
    // 如果PNG加载失败，尝试使用系统图标
    QIcon systemIcon = QIcon::fromTheme("camera-photo");
    if (!systemIcon.isNull()) {
        LOG_INFO("Using system icon");
        return systemIcon;
    }
    
    // 如果所有方法都失败，使用蓝色占位图标
    LOG_WARNING("System icon is null, using blue placeholder icon");
    QPixmap pix(16, 16);
    pix.fill(Qt::blue);
    return QIcon(pix);
}

/**
 * @brief 加载全局样式表（按当前主题填充占位符）
 * @author chiangyang
 */
QString StyleManager::loadGlobalStyleSheet() {
    QFile qss(":/stylesheets/app.qss");
    if (!qss.open(QFile::ReadOnly)) {
        LOG_WARNING("Failed to open global stylesheet resource");
        return QString();
    }
    QString sheet = QString::fromUtf8(qss.readAll());

    // 占位符按主题填值（浅色=原硬编码值；深色=HUD 配套色）
    const bool dark = (themeMode() == ThemeMode::Dark);
    const QHash<QString, QString> tokens = dark
        ? QHash<QString, QString>{
            { "@textColor",         "#f2f2f2" },
            { "@groupBoxBorder",    "rgba(255,255,255,55)" },
            { "@inputBorder",       "rgba(255,255,255,80)" },
            { "@inputBorderFocus",  "rgba(255,255,255,160)" },
            { "@scrollBg",          "#2a2a2e" },
            { "@scrollHandle",      "#5a5a60" },
            { "@scrollHandleHover", "#7a7a80" },
            { "@sliderGroove",      "#3a3a3e" },
            { "@sliderHandle",      "#c0c0c4" },
            { "@colorButtonBorder", "rgba(255,255,255,55)" },
            { "@viewBg",            "#1a1a1e" },
            { "@separatorColor",    "rgba(255,255,255,60)" },
            { "@checkBoxBorder",    "rgba(255,255,255,120)" },
            { "@checkBoxBg",        "#2a2a2e" },
            { "@checkBoxBorderHover", "rgba(255,255,255,200)" },
            { "@checkBoxChecked",   "#0078d7" },
            { "@checkBoxDisabledBorder", "rgba(255,255,255,40)" },
            { "@checkBoxDisabledBg", "rgba(255,255,255,12)" },
        }
        : QHash<QString, QString>{
            { "@textColor",         "#000" },
            { "@groupBoxBorder",    "#E0E0E0" },
            { "@inputBorder",       "#848484" },
            { "@inputBorderFocus",  "#2a2a2a" },
            { "@scrollBg",          "#F0F0F0" },
            { "@scrollHandle",      "#C0C0C0" },
            { "@scrollHandleHover", "#A0A0A0" },
            { "@sliderGroove",      "#b0b0b0" },
            { "@sliderHandle",      "#555" },
            { "@colorButtonBorder", "#ccc" },
            { "@viewBg",            "#ffffff" },
            { "@separatorColor",    "rgba(0,0,0,40)" },
            { "@checkBoxBorder",    "#848484" },
            { "@checkBoxBg",        "#ffffff" },
            { "@checkBoxBorderHover", "#2a2a2a" },
            { "@checkBoxChecked",   "#0078d7" },
            { "@checkBoxDisabledBorder", "#c0c0c0" },
            { "@checkBoxDisabledBg", "#f0f0f0" },
        };
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        sheet.replace(it.key(), it.value());
    }
    return sheet;
}

/**
 * @brief 重新应用全局样式表
 *
 * 全局 qss（app.qss）只在 setStyleSheet() 调用时解析一次，DPI 变化后
 * pt（字体）和 em（控件尺寸）不会自动更新。此方法重新加载并应用
 * 全局 qss，让 pt 按新 logicalDotsPerInch 重新换算像素、em 基于新
 * 字体重新计算，确保 DPI 变化后所有窗口的字体和控件尺寸正确更新。
 * 在 SettingsWindow 和 HistoryWindow 的 DPI 变化处理中均会调用。
 * @return 是否成功重新应用
 * @author chiangyang
 */
bool StyleManager::reapplyGlobalStyleSheet() {
    const QString sheet = loadGlobalStyleSheet();
    if (sheet.isEmpty()) {
        LOG_WARNING("Failed to reload global stylesheet on DPI change");
        return false;
    }
    qApp->setStyleSheet(sheet);
    LOG_INFO("Global stylesheet reapplied on DPI change");
    return true;
}

// ============ 样式串构造与存取实现（自 StyleManager.h 内联体迁入，改样式只重编译本文件） ============

QString StyleManager::getToolbarBackgroundStyle() {
        return QString("background-color: %1; border-radius: 0.3em;")
            .arg(qssColor(color(StyleColorId::ToolbarBg)));
}

QString StyleManager::getSubToolbarStyle() {
        return QString("background-color: %1; border-radius: 0.3em;")
            .arg(qssColor(color(StyleColorId::SubToolbarBg)));
}

QString StyleManager::getToolButtonStyle() {
        return QString(
                    "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }"
                    "QPushButton:hover { background-color: %3; transform: scale(1.05); }"
                    "QPushButton::icon { color: %4; }"
                ).arg(qssColor(color(StyleColorId::ToolbarText)))
                .arg(qssColor(color(StyleColorId::ToolbarBtn)))
                .arg(qssColor(color(StyleColorId::ToolbarButtonHover)))
                .arg(qssColor(color(StyleColorId::ToolbarBtn)));
}

QString StyleManager::getActionButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }"
            "QPushButton:checked { background-color: %3; }"
            "QPushButton:hover { background-color: %4; transform: scale(1.05); }"
            "QPushButton:disabled { color: %1; background-color: %5; }"
            "QPushButton::icon { color: %1; }"
        ).arg(qssColor(color(StyleColorId::ToolbarText)))
         .arg(qssColor(color(StyleColorId::ToolbarBtn)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonChecked)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonHover)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonDisabled)));
}

QString StyleManager::getCloseButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em; transition: all 0.2s ease; }")
            .arg(qssColor(color(StyleColorId::ToolbarText)))
            .arg(qssColor(color(StyleColorId::CloseButtonBg))) +
            QString(
            "QPushButton:hover { background-color: %1; transform: scale(1.05); }")
            .arg(qssColor(color(StyleColorId::CloseButtonHover))) +
            QString("QPushButton::icon { color: %1; }")
            .arg(qssColor(color(StyleColorId::ToolbarText)));
}

void StyleManager::applyToolButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getToolButtonStyle());
        }
}

void StyleManager::applyActionButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getActionButtonStyle());
        }
}

void StyleManager::applyCloseButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getCloseButtonStyle());
        }
}

void StyleManager::applyNormalButtonStyle(QPushButton *button) {
        if (button) {
            button->setStyleSheet(getActionButtonStyle());
        }
}

QString StyleManager::getButtonCheckedStyle() {
        return QString("color: %1; background-color: %2; padding: 0.24em; border: none; border-radius: 0.24em;")
            .arg(qssColor(color(StyleColorId::ToolbarText)))
            .arg(qssColor(color(StyleColorId::ToolbarButtonChecked)));
}

QString StyleManager::getComboBoxStyle() {
        // 深色主题下弹出列表必须用实色底，半透明的 ToolbarBtn 会让下拉框透出下层内容
        const bool dark = (themeMode() == ThemeMode::Dark);
        const QString popupBg = dark ? QStringLiteral("#2a2a2e") : qssColor(color(StyleColorId::ToolbarBtn));
        const QString border = dark ? QStringLiteral("rgba(255,255,255,80)") : QStringLiteral("#333");
        const QString hover = dark ? QStringLiteral("rgba(255,255,255,48)") : QStringLiteral("#444");
        return QString(
            "QComboBox { color: %1; background-color: %2; border: 1px solid %3; padding: 0.14em 0.19em; border-radius: 0.24em; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 0.24em solid transparent; border-right: 0.24em solid transparent; border-top: 0.29em solid %1; }"
            "QComboBox QAbstractItemView { background-color: %5; color: %1; border: 1px solid %3; }"
            "QComboBox QAbstractItemView::item { padding: 0.24em; }"
            "QComboBox QAbstractItemView::item:hover { background-color: %6; }"
            "QComboBox QAbstractItemView::item:selected { background-color: %4; color: %1; }"
        ).arg(qssColor(color(StyleColorId::ToolbarText)))
         .arg(qssColor(color(StyleColorId::ToolbarBtn)))
         .arg(border)
         .arg(qssColor(color(StyleColorId::ToolbarButtonChecked)))
         .arg(popupBg)
         .arg(hover);
}

QString StyleManager::getCheckBoxStyle() {
        return QString(
            "QCheckBox { color: %1; spacing: 4px; }"
            "QCheckBox::indicator { width: 0.67em; height: 0.67em; }"
        ).arg(qssColor(color(StyleColorId::ToolbarText)));
}

QString StyleManager::getSettingsCheckBoxStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        return QString(
            "QCheckBox { color: %1; }"
            "QCheckBox::indicator { width: 1.1em; height: 1.1em; }"
        ).arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000"));
}

QString StyleManager::getColorButtonStyle(const QString &color, const bool isSelected) {
        const bool dark = (themeMode() == ThemeMode::Dark);
        if (isSelected) {
            return QString("background-color: %1; border: 2px solid %2;")
                .arg(color)
                .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000"));
        }
        return QString("background-color: %1; border: 1px solid %2;")
            .arg(color)
            .arg(dark ? QStringLiteral("rgba(255,255,255,80)") : QStringLiteral("#ccc"));
}

QString StyleManager::getOverlayTextEditStyle(const QColor &color, int fontSize) {
        return QString("QTextEdit { background-color: rgba(255, 255, 255, 200); "
                       "border: 1px dashed %1; color: %1; font-size: %2px; }")
            .arg(qssColor(color))
            .arg(fontSize);
}

QString StyleManager::getSnipInfoLabelStyle() {
        return "background-color: rgba(0,0,0,150); color: white; "
               "padding: 0.14em 0.38em; border-radius: 0.19em; font-size: 10pt;";
}

QString StyleManager::getRecordTimerLabelStyle() {
        return "background-color: rgba(0,0,0,150); color: #ff4444; "
               "font-size: 10pt; font-weight: bold; padding: 0.1em 0.38em; border-radius: 0.14em;";
}

QString StyleManager::getPinHintLabelStyle() {
        return "background-color: rgba(0, 0, 0, 180); color: white; padding: 0.38em 0.57em; border-radius: 0.19em; font-size: 10pt;";
}

QString StyleManager::getOcrLoadingLabelStyle() {
        return "background-color: rgba(0,0,0,180); color: white; padding: 0.57em 0.95em; border-radius: 0.29em; font-size: 10pt;";
}

QString StyleManager::getOcrResultTextStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        return QString(
            "QTextEdit {"
            "    background-color: %1;"
            "    color: %2;"
            "    border: 1px solid %3;"
            "    border-radius: 0.3em;"
            "    padding: 0.5em;"
            "    font-size: 14pt;"
            "}"
        ).arg(dark ? QStringLiteral("#1a1a1e") : QStringLiteral("#ffffff"))
         .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#333333"))
         .arg(dark ? QStringLiteral("rgba(255,255,255,55)") : QStringLiteral("#cccccc"));
}

QString StyleManager::getOcrResultTitleStyle() {
        return "font-size: 10pt; font-weight: bold;";
}

QString StyleManager::getAppNameLabelStyle() {
        return "font-size: 9pt; font-weight: bold;";
}

QString StyleManager::getMenuStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        const QString bg = dark ? QStringLiteral("#2a2a2e") : QStringLiteral("#ffffff");
        const QString text = dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000000");
        const QString border = dark ? QStringLiteral("rgba(255,255,255,55)") : QStringLiteral("#cccccc");
        const QString hover = dark ? QStringLiteral("rgba(255,255,255,48)") : QStringLiteral("#e0e0e0");
        return QString(
            "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 0.29em; padding: 0.29em 0; font-size: 10pt; }"
            "QMenu::item { padding: 0.48em 0.71em; margin: 0 0.29em; border-radius: 0.19em; }"
            "QMenu::item:selected { background-color: %4; }"
            "QMenu::item:hover { background-color: %4; }"
            "QMenu::separator { height: 0.1em; background-color: %3; margin: 0.3em 0.5em; }"
        ).arg(bg).arg(text).arg(border).arg(hover);
}

QString StyleManager::getWindowStyle() {
        return QString("background-color: %1; border-radius: 0.3em; color: %2;")
            .arg(qssColor(color(StyleColorId::RecordControlBg)))
            .arg(qssColor(color(StyleColorId::ToolbarText)));
}

QString StyleManager::getPathEditStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        return QString(
            "QLineEdit {"
            "    border: 1px solid %1;"
            "    border-bottom: 1px solid %2;"
            "    background-color: %3;"
            "    color: %4;"
            "}"
        ).arg(dark ? QStringLiteral("rgba(255,255,255,55)") : QStringLiteral("#CCCCCC"))
         .arg(dark ? QStringLiteral("rgba(255,255,255,160)") : QStringLiteral("#848484"))
         .arg(dark ? QStringLiteral("#2a2a2e") : QStringLiteral("#fff"))
         .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000"));
}

QString StyleManager::getKeySequenceEditStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        const QString border = dark ? QStringLiteral("rgba(255,255,255,55)") : QStringLiteral("#CCCCCC");
        const QString bg = dark ? QStringLiteral("#2a2a2e") : QStringLiteral("#fff");
        const QString text = dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000");
        return QString(
            "QKeySequenceEdit QLineEdit {"
            "    border: 1px solid %1;"
            "    border-bottom: 1px solid %2;"
            "    background-color: %3;"
            "    color: %4;"
            "}"

            "QKeySequenceEdit QLineEdit:focus {"
            "    border: 1px solid %1;"
            "    border-bottom: 2px solid #1E90FF; /* 蓝色下边框（深浅色通用） */"
            "}"
        ).arg(border)
         .arg(dark ? QStringLiteral("rgba(255,255,255,160)") : QStringLiteral("#848484"))
         .arg(bg)
         .arg(text);
}

QString StyleManager::getGroupBoxStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        return QString(
            "QGroupBox { background-color: %1; color: %2; }"
        ).arg(qssColor(color(StyleColorId::TabWidgetBg)))
         .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000"));
}

QString StyleManager::getSettingsButtonStyle() {
        return QString(
            "QPushButton { color: %1; background-color: %2; border: none; }"
            "QPushButton:hover { background-color: %4; }"
            "QPushButton:disabled { color: %1; background-color: %3; }"
        ).arg(qssColor(color(StyleColorId::SettingButtonText)))
         .arg(qssColor(color(StyleColorId::SettingButtonBg)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonDisabled)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonHover)));
}

QString StyleManager::getSettingsComboBoxStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        const QString bg = dark ? QStringLiteral("#2a2a2e") : QStringLiteral("#fff");
        const QString text = dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000");
        const QString border = dark ? QStringLiteral("rgba(255,255,255,80)") : QStringLiteral("#848484");
        const QString hover = dark ? QStringLiteral("rgba(255,255,255,48)") : QStringLiteral("#d0d0d0");
        // QSS image: 引用的 SVG 无法动态染色，深色主题换用白色箭头资源
        const QString upArrow = dark ? QStringLiteral(":/icons/spinner-up-dark.svg")
                                     : QStringLiteral(":/icons/spinner-up.svg");
        const QString downArrow = dark ? QStringLiteral(":/icons/spinner-down-dark.svg")
                                       : QStringLiteral(":/icons/spinner-down.svg");
        return QString(
            "QComboBox { background-color: %1; color: %2; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox::down-arrow { image: none; border-left: 0.24em solid transparent; border-right: 0.24em solid transparent; border-top: 0.29em solid %2; }"
            "QComboBox QAbstractItemView { background-color: %1; color: %2; border: 1px solid %3; outline: none; }"
            "QComboBox QAbstractItemView::item { padding: 0.24em; }"
            "QComboBox QAbstractItemView::item:hover { background-color: %4; }"
            "QComboBox QAbstractItemView::item:selected { background-color: %4; color: %2; }"
            "QAbstractSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 0.24em; padding: 0.14em 0.19em; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 1.2em; border: none; background: transparent; }"
            "QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; }"
            "QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; }"
            "QAbstractSpinBox::up-arrow { image: url(%5); width: 0.5em; height: 0.5em; }"
            "QAbstractSpinBox::down-arrow { image: url(%6); width: 0.5em; height: 0.5em; }"
        ).arg(bg).arg(text).arg(border).arg(hover).arg(upArrow).arg(downArrow);
}

QString StyleManager::getMessageBoxStyle() {
        const bool dark = (themeMode() == ThemeMode::Dark);
        return QString(
            "QMessageBox { background-color: %1; }"
            "QMessageBox QLabel { color: %6; }"
            "QMessageBox QPushButton { color: %2; background-color: %3; border: none; }"
            "QMessageBox QPushButton:hover { background-color: %4; }"
            "QMessageBox QPushButton:pressed { background-color: %4; }"
            "QMessageBox QPushButton:disabled { color: %2; background-color: %5; }"
        ).arg(qssColor(color(StyleColorId::TabWidgetBg)))
         .arg(qssColor(color(StyleColorId::SettingButtonText)))
         .arg(qssColor(color(StyleColorId::SettingButtonBg)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonHover)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonDisabled)))
         .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#333333"));
}

QString StyleManager::getProgressBarStyle() {
        return QString(
            "QProgressBar { border: none; background-color: %1; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 3px; }"
        ).arg(qssColor(color(StyleColorId::TabWidgetBg)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonHover)));
}

QString StyleManager::getTabWidgetStyle() {
        return QString(
            "QTabWidget::pane { background-color: %1; }"
            "QTabBar::tab { background-color: %2; color: %3; }"
            "QTabBar::tab:selected { background-color: %4; color: %5; }"
            "QTabBar::tab:hover { background-color: %6; }"
        ).arg(qssColor(color(StyleColorId::TabWidgetBg)))
         .arg(qssColor(color(StyleColorId::TabButtonBg)))
         .arg(qssColor(color(StyleColorId::TabButtonText)))
         .arg(qssColor(color(StyleColorId::TabButtonSelectedBg)))
         .arg(qssColor(color(StyleColorId::TabButtonSelectedText)))
         .arg(qssColor(color(StyleColorId::ToolbarButtonHover)));
}

QString StyleManager::getToolbarButtonStyle() {
        return s_toolbarButtonStyle;
}

void StyleManager::setToolbarButtonStyle(const QString &style) {
        s_toolbarButtonStyle = style;
}

int StyleManager::getDefaultPenWidth() {
        return s_defaultPenWidth;
}

void StyleManager::setDefaultPenWidth(int width) {
        s_defaultPenWidth = width;
}

int StyleManager::getDefaultFontSize() {
        return s_defaultFontSize;
}

void StyleManager::setDefaultFontSize(int size) {
        s_defaultFontSize = size;
}

int StyleManager::getDefaultEraserWidth() {
        return s_defaultEraserWidth;
}

void StyleManager::setDefaultEraserWidth(int width) {
        s_defaultEraserWidth = width;
}

int StyleManager::getDefaultMosaicSize() {
        return s_defaultMosaicSize;
}

void StyleManager::setDefaultMosaicSize(int size) {
        s_defaultMosaicSize = size;
}

void StyleManager::resetToDefaults() {
        // 颜色：遍历颜色表逐项恢复默认色
        for (const auto& s : colorSettingTable()) {
            setColor(s.id, s.defaultColor);
        }

        // 标注工具默认值
        s_defaultPenWidth = DEFAULT_PEN_WIDTH;
        s_defaultFontSize = DEFAULT_FONT_SIZE;
        s_defaultEraserWidth = DEFAULT_ERASER_WIDTH;
        s_defaultMosaicSize = DEFAULT_MOSAIC_SIZE;

        s_toolbarButtonStyle = DEFAULT_TOOLBAR_BUTTON_STYLE;
        s_theme = DEFAULT_THEME;
}

