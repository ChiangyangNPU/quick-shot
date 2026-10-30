#include "PetTheme.h"
#include "../core/StyleManager.h"

/**
 * @brief 当前是否深色主题
 * @author chiangyang
 */
bool PetTheme::isDark() {
    return StyleManager::themeMode() == StyleManager::ThemeMode::Dark;
}

/**
 * @brief 主文字颜色
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::textColor(bool dark) {
    return dark ? QColor(232, 234, 237) : QColor(32, 33, 36);
}

/**
 * @brief 次要文字颜色（统计、日期、完成态、空状态）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::mutedColor(bool dark) {
    return dark ? QColor(142, 144, 149) : QColor(128, 132, 139);
}

/**
 * @brief 输入框/下拉等控件的填充底色（以选项卡背景为基准微调一档）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::fieldColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(142) : base.darker(105);
}

/**
 * @brief 分隔线颜色
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::dividerColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(150) : base.darker(111);
}

/**
 * @brief 行悬停底色（极浅，仅用于反馈）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::hoverColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(126) : base.darker(103);
}

/**
 * @brief 主强调色（沿用应用配置的蓝色，与设置菜单的按钮同色）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::accentColor(bool dark) {
    const QColor base = StyleManager::getToolbarButtonCheckedColor();
    return dark ? base.lighter(118) : base;
}

/**
 * @brief 强调色上的文字颜色（蓝色底统一用白字）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::accentTextColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(255, 255, 255);
}

/**
 * @brief 逾期提示色
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::overdueColor(bool dark) {
    return dark ? QColor(255, 123, 114) : QColor(200, 68, 64);
}

/**
 * @brief 今天到期提示色
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::todayColor(bool dark) {
    return dark ? QColor(236, 178, 84) : QColor(178, 116, 18);
}

/**
 * @brief 下拉箭头图标（深色主题换用白色箭头资源，SVG 无法动态染色）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString PetTheme::downArrowIcon(bool dark) {
    return dark ? QStringLiteral(":/icons/spinner-down-dark.svg")
                : QStringLiteral(":/icons/spinner-down.svg");
}

// ============ 晾衣绳专用色板 ============

/**
 * @brief 麻绳颜色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::ropeColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(214, 178, 128);
}

/**
 * @brief 木衣夹颜色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::pinColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(120, 84, 52);
}

/**
 * @brief 卡纸底色（浅纸色，深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::cardColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(250, 250, 250, 235);
}

/**
 * @brief 卡纸描边色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::cardBorderColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(255, 255, 255, 60);
}

/**
 * @brief 文本卡摘要文字色（浅纸上深字，深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::cardTextColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(60, 60, 66);
}

/**
 * @brief 卡片柔和投影色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::cardShadowColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(0, 0, 0, 20);
}

/**
 * @brief 最近预览/点击卡片的高亮描边基色（跟随应用强调色）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::cardHighlightColor(bool dark) {
    return accentColor(dark);
}

/**
 * @brief 空历史提示的深色圆角托底色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::tipPanelColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(30, 30, 34, 170);
}

/**
 * @brief 空历史提示文字色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::tipTextColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(255, 255, 255, 220);
}

/**
 * @brief 关闭按钮常态底色（深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::closeIdleColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(30, 30, 34, 130);
}

/**
 * @brief 关闭按钮悬停底色（警示红，深浅主题一致）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor PetTheme::closeHoverColor(bool dark) {
    Q_UNUSED(dark);
    return QColor(232, 17, 35, 200);
}
