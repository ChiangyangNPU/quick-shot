#include "StyleManager.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QStyle>
#include <QSvgRenderer>

#include "Logger.h"

// .cpp 级循环依赖：ConfigManager.cpp 会 include StyleManager.h（用 DEFAULT_* 播种），
// 此文件 include ConfigManager.h（initFromConfig 用 value() 读取持久化）。
// 头文件间仅前向声明 class ConfigManager，见 StyleManager.h 顶部注释。
#include "ConfigManager.h"

// 静态成员变量初始化
QColor StyleManager::s_recordBorderColor = QColor("#4CAF50");         ///< 录屏框当前颜色
QColor StyleManager::s_captureBorderColor = QColor("#4da6ff");       ///< 截屏框当前颜色
QColor StyleManager::s_toolbarBgColor = QColor("#cfcfcf");               ///< 工具栏当前背景颜色
QColor StyleManager::s_recordControlBgColor = QColor("#cfcfcf");        ///< 录屏控制栏当前背景颜色
QColor StyleManager::s_toolbarBtnColor = QColor("#e0e0e0");               ///< 工具栏按钮当前颜色
QColor StyleManager::s_toolbarTextColor = QColor("#000");              ///< 工具栏按钮文字当前颜色
QColor StyleManager::s_toolbarButtonHoverColor = QColor("#fff");       ///< 工具栏按钮悬停当前颜色
QColor StyleManager::s_toolbarButtonDisabledColor = QColor("#d1d1d1");    ///< 工具栏按钮禁用当前颜色
QColor StyleManager::s_subToolbarBgColor = QColor("#cfcfcf");            ///< 子工具栏当前背景颜色
QColor StyleManager::s_settingButtonBgColor = QColor("#e0e0e0");       ///< 设置窗口按钮当前背景颜色
QColor StyleManager::s_settingButtonTextColor = QColor("#000000");     ///< 设置窗口按钮文字当前颜色
QColor StyleManager::s_toolbarButtonCheckedColor = QColor("#0078d7");   ///< 工具栏按钮选中当前颜色
QColor StyleManager::s_closeButtonBgColor = QColor("#cc0000");         ///< 关闭按钮当前背景颜色
QColor StyleManager::s_closeButtonHoverColor = QColor("#ff3333");      ///< 关闭按钮当前悬停颜色
QColor StyleManager::s_tabWidgetBgColor = QColor("#ffffff");           ///< 选项卡背景当前颜色
QColor StyleManager::s_tabButtonBgColor = QColor("#e0e0e0");           ///< 选项卡按钮背景当前颜色
QColor StyleManager::s_tabButtonTextColor = QColor("#000000");         ///< 选项卡按钮文字当前颜色
QColor StyleManager::s_tabButtonSelectedBgColor = QColor("#ffffff");    ///< 选项卡按钮选中背景当前颜色
QColor StyleManager::s_tabButtonSelectedTextColor = QColor("#000000");  ///< 选项卡按钮选中文字当前颜色
QColor StyleManager::s_handleCircleColor = QColor("#2563EB");            ///< 角手柄圆形当前颜色
QColor StyleManager::s_handleCloseColor = QColor("#DC2626");             ///< 角手柄关闭按钮当前颜色
QString StyleManager::s_toolbarButtonStyle = "text";                    ///< 工具栏按钮当前样式（文字模式）
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
            QString("style/%1").arg(s.settingsKey), s.defaultColor.name()).toString()));
    }

    // 标注工具默认值（带边界 clamp，与 ConfigManager 原 getDefault* 一致）
    setDefaultPenWidth(qBound(1, cm->value("style/defaultPenWidth", DEFAULT_PEN_WIDTH).toInt(), 20));
    setDefaultFontSize(qBound(8, cm->value("style/defaultFontSize", DEFAULT_FONT_SIZE).toInt(), 48));
    setDefaultEraserWidth(qBound(1, cm->value("style/defaultEraserWidth", DEFAULT_ERASER_WIDTH).toInt(), 20));
    setDefaultMosaicSize(qBound(1, cm->value("style/defaultMosaicSize", DEFAULT_MOSAIC_SIZE).toInt(), 20));

    // 工具栏按钮样式
    setToolbarButtonStyle(cm->value("style/toolbarButtonStyle", DEFAULT_TOOLBAR_BUTTON_STYLE).toString());
}

/**
 * @brief 加载SVG图标
 * @param iconPath SVG图标路径
 * @param size 图标大小
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath, const QSize &size) {
    QSvgRenderer svgRenderer(iconPath);
    if (svgRenderer.isValid()) {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        svgRenderer.render(&painter);
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
 * @brief 加载SVG图标
 * @param iconPath SVG图标路径
 * @return 加载的图标
 * @author chiangyang
 */
QIcon StyleManager::loadSvgIcon(const QString &iconPath) {
    QSvgRenderer svgRenderer(iconPath);
    if (svgRenderer.isValid()) {
        QPixmap pixmap(QSize(24, 24));
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        svgRenderer.render(&painter);
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
    QFile qss(":/stylesheets/app.qss");
    if (!qss.open(QFile::ReadOnly)) {
        LOG_WARNING("Failed to reload global stylesheet on DPI change");
        return false;
    }
    qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    LOG_INFO("Global stylesheet reapplied on DPI change");
    return true;
}
