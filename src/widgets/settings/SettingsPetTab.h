#ifndef SETTINGSPETTAB_H
#define SETTINGSPETTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
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
 * 内容分为四个分组框：基本（宠物开关）、宠物样式（经典样式 + 十二生肖
 * 平铺图标按钮）、自定义宠物（缩略图 + 添加/删除）、行为（晾衣绳打开方式、
 * 重置位置、功能说明）。开关与样式由 DesktopPetManager 持久化并即时生效；
 * 内容超高时由外层 QScrollArea 滚动（窗口高度由 SettingsWindow 封顶）。
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
    QGroupBox *m_basicGroup;            ///< 基本分组（宠物开关）
    QCheckBox *m_enablePetCheck;        ///< 打开桌面宠物复选框
    QGroupBox *m_styleGroupBox;         ///< 宠物样式分组（经典样式 + 十二生肖）
    QLabel *m_classicLabel;             ///< 经典样式分组标签（第一行）
    QLabel *m_zodiacLabel;              ///< 十二生肖分组标签（第二行起，FlowLayout 自动换行）
    QList<QToolButton*> m_styleButtons; ///< 外观样式图标按钮（下标对应 PetStyle 枚举值）
    QButtonGroup *m_styleGroup;         ///< 样式按钮互斥组（id = 样式下标；自定义项从 1000 起）
    QGroupBox *m_behaviorGroup;         ///< 行为分组（晾衣绳方式、待办提醒、番茄钟、重置位置、功能说明）
    QPushButton *m_btnResetPos;         ///< 重置宠物位置按钮
    QLabel *m_triggerLabel;             ///< 晾衣绳打开方式标签
    QComboBox *m_triggerCombo;          ///< 晾衣绳打开方式下拉框（上边界/点击宠物/两者）
    QCheckBox *m_todoReminderCheck;     ///< 待办提醒复选框（逾期/今天到期时宠物头顶红点）
    QCheckBox *m_pomodoroCheck;         ///< 番茄钟开关复选框
    QLabel *m_pomodoroWorkLabel;        ///< 番茄钟"工作"标签
    QSpinBox *m_pomodoroWorkSpin;       ///< 番茄钟工作时长（分钟）
    QLabel *m_pomodoroBreakLabel;       ///< 番茄钟"休息"标签
    QSpinBox *m_pomodoroBreakSpin;      ///< 番茄钟休息时长（分钟）
    QCheckBox *m_idleSleepCheck;        ///< 待机打盹复选框（无互动时宠物呼吸浮动）
    QLabel *m_idleSleepAfterLabel;      ///< 待机打盹"无互动"标签
    QSpinBox *m_idleSleepSpin;          ///< 无互动进入打盹的分钟数
    QCheckBox *m_sysMonitorCheck;       ///< 系统状态复选框（过载冒汗/低电量电池角标）
    QLabel *m_descLabel;                ///< 功能说明标签

    QGroupBox *m_customGroup = nullptr;     ///< 自定义宠物分组
    FlowLayout *m_customFlow = nullptr;     ///< 自定义宠物缩略图流式布局（自动换行）
    QPushButton *m_btnAddCustom = nullptr;  ///< 添加自定义宠物按钮
    QPushButton *m_btnRemoveCustom = nullptr; ///< 删除当前自定义宠物按钮
    QLabel *m_customHint = nullptr;         ///< 自定义宠物使用提示
    QVector<QToolButton*> m_customButtons;  ///< 自定义宠物缩略图按钮
};

#endif // SETTINGSPETTAB_H
