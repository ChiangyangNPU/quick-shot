#ifndef SETTINGSTAB_H
#define SETTINGSTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <array>
#include "../../core/StyleManager.h"

/**
 * @brief 设置窗口「样式」选项卡
 *
 * 颜色设置（数据驱动表）、工具栏按钮样式、标注工具默认值。
 * 从 SettingsWindow 拆分而来：颜色行创建、取色器、样式保存/加载、
 * 重置与滚动容器全部内聚于此。跨选项卡联动（tabWidget QSS、其他
 * 选项卡背景、全部分组样式）经 tabWidgetStyleChanged / tabWidgetBgChanged
 * 信号由宿主执行；default*Changed 与 tabWidgetBgColorChanged 等对外
 * 信号经宿主同名信号转发保持外部订阅不变。
 * @author chiangyang
 */
class SettingsStyleTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsStyleTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（DPI / 样式重载时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

    /**
     * @brief 加载样式到控件（StyleManager 已由启动逻辑恢复，此处同步 UI）
     * @author chiangyang
     */
    void loadStyleSettings();

public slots:
    /// 工具栏画笔粗细变更（SnipScreen 工具栏联动，经宿主转发）
    void onToolPenWidthChanged(int width);
    /// 工具栏字号变更（经宿主转发）
    void onToolFontSizeChanged(int size);
    /// 工具栏橡皮擦粗细变更（经宿主转发）
    void onToolEraserWidthChanged(int width);
    /// 工具栏马赛克大小变更（经宿主转发）
    void onToolMosaicSizeChanged(int size);

signals:
    void tabWidgetStyleChanged();
    void tabWidgetBgChanged(const QColor &color);
    void tabWidgetBgColorChanged();
    void toolbarButtonStyleChanged(const QString &style);
    void defaultPenWidthChanged(int width);
    void defaultFontSizeChanged(int size);
    void defaultEraserWidthChanged(int width);
    void defaultMosaicSizeChanged(int size);

private slots:
    void onResetStyle();
    void onToolbarButtonStyleChanged(int index);

private:
    void setupUi();
    QColor showColorDialog(const QColor &currentColor);
    QHBoxLayout *createColorRow(QWidget *parent, const QString &labelText,
                                const QColor &color, QPushButton *&outBtn, QLabel *&outLabel);
    void updateColorButton(QPushButton *btn, const QColor &color);
    void applyColorChange(StyleManager::StyleColorId id);
    void applyColorPostUpdate(StyleManager::StyleColorCategory category, const QColor &newColor);
    void saveStyleSettings();

    QGroupBox *borderGroup;         ///< 边框颜色设置组
    QGroupBox *toolbarGroup;        ///< 工具栏样式设置组
    QGroupBox *tabGroup;            ///< 选项卡样式设置组
    QWidget *styleTabContainer;     ///< 样式选项卡容器（本 Tab 的根内容）
    QWidget *scrollContainer;       ///< 滚动区域容器
    QScrollArea *styleScrollArea;   ///< 样式选项卡滚动区域
    QPushButton *btnResetStyle;     ///< 重置样式按钮
    QLabel *lblToolbarButtonStyle;  ///< 工具栏按钮样式标签
    QComboBox *cmbToolbarButtonStyle; ///< 工具栏按钮样式下拉框
    QGroupBox *m_annotationDefaultsGroup; ///< 标注工具默认值设置组
    QLabel *lblDefaultPenWidth;          ///< 画笔默认粗细标签
    QSpinBox *spnDefaultPenWidth;        ///< 画笔默认粗细 SpinBox
    QLabel *lblDefaultFontSize;          ///< 文本默认字号标签
    QSpinBox *spnDefaultFontSize;        ///< 文本默认字号 SpinBox
    QLabel *lblDefaultEraserWidth;       ///< 橡皮擦默认粗细标签
    QSpinBox *spnDefaultEraserWidth;     ///< 橡皮擦默认粗细 SpinBox
    QLabel *lblDefaultMosaicSize;        ///< 马赛克默认大小标签
    QSpinBox *spnDefaultMosaicSize;      ///< 马赛克默认大小 SpinBox
    std::array<QPushButton*, StyleManager::kStyleColorCount> m_colorButtons{};
    std::array<QLabel*, StyleManager::kStyleColorCount> m_colorLabels{};
};

#endif // SETTINGSTAB_H
