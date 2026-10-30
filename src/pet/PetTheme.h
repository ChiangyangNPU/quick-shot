#ifndef PETTHEME_H
#define PETTHEME_H

#include <QColor>
#include <QString>

/**
 * @brief 桌面宠物模块的主题色板
 *
 * 为宠物相关窗口（待办气泡等）的自绘与手写 QSS 统一提供派生颜色与图标路径。
 * 颜色以 StyleManager 的主题模式和用户配色为基准计算，随主题切换即时生效，
 * 避免各窗口各自维护一套灰阶与强调色。
 * @author chiangyang
 */
class PetTheme {
public:
    /**
     * @brief 当前是否深色主题
     * @return 深色主题返回 true
     * @author chiangyang
     */
    static bool isDark();

    /**
     * @brief 主文字颜色
     * @param dark 是否深色主题
     * @return 文字颜色
     * @author chiangyang
     */
    static QColor textColor(bool dark);

    /**
     * @brief 次要文字颜色（统计、日期、完成态、空状态）
     * @param dark 是否深色主题
     * @return 次要文字颜色
     * @author chiangyang
     */
    static QColor mutedColor(bool dark);

    /**
     * @brief 输入框/下拉等控件的填充底色（以选项卡背景为基准微调一档）
     * @param dark 是否深色主题
     * @return 填充底色
     * @author chiangyang
     */
    static QColor fieldColor(bool dark);

    /**
     * @brief 分隔线颜色
     * @param dark 是否深色主题
     * @return 分隔线颜色
     * @author chiangyang
     */
    static QColor dividerColor(bool dark);

    /**
     * @brief 行悬停底色（极浅，仅用于反馈）
     * @param dark 是否深色主题
     * @return 悬停底色
     * @author chiangyang
     */
    static QColor hoverColor(bool dark);

    /**
     * @brief 主强调色（沿用应用配置的蓝色，与设置菜单的按钮同色）
     * @param dark 是否深色主题（深色下取一档更亮保证对比度）
     * @return 强调色
     * @author chiangyang
     */
    static QColor accentColor(bool dark);

    /**
     * @brief 强调色上的文字颜色（蓝色底统一用白字）
     * @param dark 是否深色主题
     * @return 强调色上的文字颜色
     * @author chiangyang
     */
    static QColor accentTextColor(bool dark);

    /**
     * @brief 逾期提示色
     * @param dark 是否深色主题
     * @return 逾期提示色
     * @author chiangyang
     */
    static QColor overdueColor(bool dark);

    /**
     * @brief 今天到期提示色
     * @param dark 是否深色主题
     * @return 今天到期提示色
     * @author chiangyang
     */
    static QColor todayColor(bool dark);

    /**
     * @brief 下拉箭头图标（深色主题换用白色箭头资源，SVG 无法动态染色）
     * @param dark 是否深色主题
     * @return 图标资源路径
     * @author chiangyang
     */
    static QString downArrowIcon(bool dark);

    // ============ 晾衣绳专用色板 ============
    // 纸卡/麻绳/木夹是物理隐喻：深浅主题下保持同一实物色（浅色纸卡在深色
    // 桌面上同样成立），深浅分支仅保留在强调/HUD 类颜色上。

    /**
     * @brief 麻绳颜色（深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor ropeColor(bool dark);

    /**
     * @brief 木衣夹颜色（深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor pinColor(bool dark);

    /**
     * @brief 卡纸底色（浅纸色，深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor cardColor(bool dark);

    /**
     * @brief 卡纸描边色（深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor cardBorderColor(bool dark);

    /**
     * @brief 文本卡摘要文字色（浅纸上深字，深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor cardTextColor(bool dark);

    /**
     * @brief 卡片柔和投影色（深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor cardShadowColor(bool dark);

    /**
     * @brief 最近预览/点击卡片的高亮描边基色（跟随应用强调色；
     *        呼吸亮度由绘制端按相位调 alpha）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor cardHighlightColor(bool dark);

    /**
     * @brief 空历史提示的深色圆角托底色（悬浮于任意桌面背景，深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor tipPanelColor(bool dark);

    /**
     * @brief 空历史提示文字色（深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor tipTextColor(bool dark);

    /**
     * @brief 关闭按钮常态底色（深色圆片，深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor closeIdleColor(bool dark);

    /**
     * @brief 关闭按钮悬停底色（警示红，深浅主题一致）
     * @param dark 是否深色主题
     * @author chiangyang
     */
    static QColor closeHoverColor(bool dark);
};

#endif // PETTHEME_H
