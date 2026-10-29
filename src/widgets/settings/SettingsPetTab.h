#ifndef SETTINGSPETTAB_H
#define SETTINGSPETTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QVector>

class FlowLayout;

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

    /**
     * @brief 选项卡滚动区（供 SettingsWindow 高度动画期间抑制滚动条闪现）
     * @return 滚动区指针（懒加载前为 nullptr）
     * @author chiangyang
     */
    QScrollArea *scrollArea() const { return petScrollArea; }

private:
    void setupUi();

    /**
     * @brief 重建自定义宠物缩略图按钮（列表变更后调用）
     *
     * 自定义按钮与内置样式共用 m_styleGroup，id 从 kCustomIdBase 起偏移，
     * 保证两组选择互斥。
     * @author chiangyang
     */
    void refreshCustomList();

    /**
     * @brief 按当前选择同步按钮选中态与删除按钮可用性
     * @author chiangyang
     */
    void syncSelection();

    /**
     * @brief 选择本地图片/GIF 并导入为自定义宠物
     * @author chiangyang
     */
    void addCustomPetFromFile();

    /**
     * @brief 删除当前选中的自定义宠物（二次确认）
     * @author chiangyang
     */
    void removeSelectedCustomPet();

    QScrollArea *petScrollArea = nullptr;   ///< 滚动区域（16 种样式按钮较高）
    QWidget *m_petScrollContent = nullptr;  ///< 滚动内容容器（palette 背景）
    QGroupBox *m_petGroup;              ///< 桌面宠物分组
    QCheckBox *m_enablePetCheck;        ///< 打开桌面宠物复选框
    QLabel *m_styleLabel;               ///< 外观样式标签
    QLabel *m_classicLabel;             ///< 经典样式分组标签（第一行）
    QLabel *m_zodiacLabel;              ///< 十二生肖分组标签（第二行起，FlowLayout 自动换行）
    QList<QToolButton*> m_styleButtons; ///< 外观样式图标按钮（下标对应 PetStyle 枚举值）
    QButtonGroup *m_styleGroup;         ///< 样式按钮互斥组（id = 样式下标；自定义项从 1000 起）
    QPushButton *m_btnResetPos;         ///< 重置宠物位置按钮
    QLabel *m_triggerLabel;             ///< 晾衣绳打开方式标签
    QComboBox *m_triggerCombo;          ///< 晾衣绳打开方式下拉框（上边界/点击宠物/两者）
    QLabel *m_descLabel;                ///< 功能说明标签

    QGroupBox *m_customGroup = nullptr;     ///< 自定义宠物分组
    FlowLayout *m_customFlow = nullptr;     ///< 自定义宠物缩略图流式布局（自动换行）
    QPushButton *m_btnAddCustom = nullptr;  ///< 添加自定义宠物按钮
    QPushButton *m_btnRemoveCustom = nullptr; ///< 删除当前自定义宠物按钮
    QLabel *m_customHint = nullptr;         ///< 自定义宠物使用提示
    QVector<QToolButton*> m_customButtons;  ///< 自定义宠物缩略图按钮
};

#endif // SETTINGSPETTAB_H
