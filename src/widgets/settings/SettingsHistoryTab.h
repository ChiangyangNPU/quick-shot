#ifndef SETTINGSHISTORYTAB_H
#define SETTINGSHISTORYTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

/**
 * @brief 设置窗口「历史记录」选项卡
 *
 * 包含记录开关、存储设置（保留天数 / 最大记录数）、数据管理（清理 / 清空）
 * 与统计信息。从 SettingsWindow 拆分而来：选项卡根容器（historyTabBg）、
 * 滚动区背景调色板、样式刷新与下拉框宽度自适应全部内聚于此。
 * @author chiangyang
 */
class SettingsHistoryTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsHistoryTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案（含保留天数下拉框重建）
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（DPI / 样式重载时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

    /**
     * @brief 应用选项卡背景色（TabWidgetBg 颜色变更时由宿主调用）
     * @param color 新的背景色
     * @author chiangyang
     */
    void applyTabBackground(const QColor &color);

    /**
     * @brief 重新计算下拉框固定宽度（DPI 变化时由宿主调用）
     * @author chiangyang
     */
    void updateComboWidths();

    /**
     * @brief 选项卡滚动区域访问器（宿主 eventFilter 处理滚轮滚动用）
     * @author chiangyang
     */
    QScrollArea *scrollArea() const { return historyScrollArea; }

private:
    void setupUi();
    void updateHistoryStats();

    QScrollArea *historyScrollArea;         ///< 滚动区域
    QWidget *m_historyScrollContent;        ///< 滚动区域内的内容容器
    QGroupBox *historyRecordGroup;          ///< 记录开关组
    QGroupBox *historyStorageGroup;         ///< 存储设置组
    QGroupBox *historyDataGroup;            ///< 数据管理组
    QGroupBox *historyStatsGroup;           ///< 统计信息组
    QCheckBox *m_screenshotHistoryCheck;    ///< 记录截图历史复选框
    QCheckBox *m_clipboardHistoryCheck;     ///< 记录剪贴板历史复选框
    QLabel *lblRetentionDays;               ///< 保留天数标签
    QComboBox *cmbRetentionDays;            ///< 保留天数下拉框
    QLabel *lblMaxItems;                    ///< 最大记录数标签
    QComboBox *cmbMaxItems;                 ///< 最大记录数下拉框
    QLabel *lblHistoryStats;                ///< 历史统计信息标签
    QPushButton *btnCleanHistory;           ///< 清理历史按钮
    QPushButton *btnClearHistory;           ///< 清空历史按钮
};

#endif // SETTINGSHISTORYTAB_H
