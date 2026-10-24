#ifndef SETTINGSPETTAB_H
#define SETTINGSPETTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>

/**
 * @brief 设置窗口「桌面宠物」选项卡
 *
 * 包含桌面宠物开关、平铺图标式的外观样式选择、重置位置按钮与功能说明。
 * 开关与样式由 DesktopPetManager 持久化（pet/enabled、pet/style）并即时生效。
 * @author chiangyang
 */
class SettingsPetTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPetTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（主题 / DPI 变化时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

    /**
     * @brief 应用选项卡背景色（TabWidgetBg 颜色变更时由宿主调用）
     * @param color 新的背景色
     * @author chiangyang
     */
    void applyTabBackground(const QColor &color);

private:
    void setupUi();

    QGroupBox *m_petGroup;              ///< 桌面宠物分组
    QCheckBox *m_enablePetCheck;        ///< 打开桌面宠物复选框
    QLabel *m_styleLabel;               ///< 外观样式标签
    QList<QToolButton*> m_styleButtons; ///< 外观样式图标按钮（下标对应 PetStyle 枚举值）
    QButtonGroup *m_styleGroup;         ///< 样式按钮互斥组（id = 样式下标）
    QPushButton *m_btnResetPos;         ///< 重置宠物位置按钮
    QLabel *m_descLabel;                ///< 功能说明标签
};

#endif // SETTINGSPETTAB_H
