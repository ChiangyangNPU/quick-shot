#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QWidget>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QStringList>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QLineEdit>
#include <QGroupBox>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QList>
#include <array>
#include "TranslationManager.h"
#include "StyleManager.h"
#include "ElidedLabel.h"
#include "settings/SettingsAboutTab.h"
#include "settings/SettingsHistoryTab.h"
#include "settings/SettingsTranslateTab.h"
#include "settings/SettingsShortcutsTab.h"
#include "settings/SettingsGeneralTab.h"
#include "settings/SettingsStyleTab.h"

class QGridLayout;

/**
 * @brief 设置窗口类
 *
 * 用于管理应用程序的设置，包括语言选择、自动启动和快捷键配置
 * @author chiangyang
 */
class SettingsWindow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int animatedHeight READ animatedHeight WRITE setAnimatedHeight)

public:
    /**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
    explicit SettingsWindow(QWidget *parent = nullptr);

    /**
     * @brief 重新翻译UI
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 主题切换时刷新设置窗口全部样式
     *
     * 刷新 tabWidget QSS、各选项卡根背景与全部分组样式，
     * 并循环调用各选项卡的 refreshStyles()（含输入控件配色）。
     * @author chiangyang
     */
    void refreshAllStyles();

    /**
     * @brief 刷新窗口自身背景色与系统标题栏主题
     *
     * 窗口底色取 TabWidgetBg 色板项（浅色近白/深色 #1a1a1e），
     * 用 objectName 选择器只染窗口自身、避免级联到子控件；
     * 标题栏经 StyleManager::applyDarkTitleBar 跟随浅色/深色。
     * 构造时与主题切换（refreshAllStyles）时调用。
     * @author chiangyang
     */
    void updateWindowBackground();

    /**
     * @brief 事件过滤器
     * @param obj 被监视的对象
     * @param event 事件
     * @return 是否处理了事件
     * @author chiangyang
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    /**
     * @brief 鼠标按下事件，用于开始拖动窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件，用于拖动窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件，结束拖动
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 窗口显示事件
     *
     * 窗口显示时重新绑定当前屏幕的 DPI 变化信号，覆盖窗口在不同屏幕间
     * 移动后旧信号失效的情况，确保缩放比例调整时窗口宽度能随之变化。
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

    /**
     * @brief 获取动画高度（Q_PROPERTY的READ方法）
     * @return 当前动画高度
     * @author chiangyang
     */
    int animatedHeight() const { return m_animatedHeight; }

    /**
     * @brief 设置动画高度（Q_PROPERTY的WRITE方法）
     * @param height 新的高度值
     * @author chiangyang
     */
    void setAnimatedHeight(int height);

signals:
    /**
     * @brief 语言改变信号
     * @param langCode 语言代码
     * @author chiangyang
     */
    void languageChanged(const QString &langCode);

    /**
     * @brief 快捷键改变信号
     * @param action 操作名称
     * @param keySequence 快捷键序列
     * @author chiangyang
     */
    void shortcutChanged(const QString &action, const QKeySequence &keySequence);

    /**
     * @brief 选项卡背景颜色改变信号
     *
     * 通知 HistoryWindow 等外部窗口更新背景色。
     * @author chiangyang
     */
    void tabWidgetBgColorChanged();

    /**
     * @brief 工具栏按钮样式改变信号
     * @param style 按钮样式（text或icon）
     * @author chiangyang
     */
    void toolbarButtonStyleChanged(const QString &style);

    /**
     * @brief 主题改变信号
     * @param theme 主题标识（"light"或"dark"）
     * @author chiangyang
     */
    void themeChanged(const QString &theme);

    /**
     * @brief 画笔默认粗细改变信号
     * @param width 画笔默认粗细值
     * @author chiangyang
     */
    void defaultPenWidthChanged(int width);

    /**
     * @brief 文本默认字号改变信号
     * @param size 文本默认字号
     * @author chiangyang
     */
    void defaultFontSizeChanged(int size);

    /**
     * @brief 橡皮擦默认粗细改变信号
     * @param width 橡皮擦默认粗细值
     * @author chiangyang
     */
    void defaultEraserWidthChanged(int width);

    /**
     * @brief 马赛克默认大小改变信号
     * @param size 马赛克默认大小值
     * @author chiangyang
     */
    void defaultMosaicSizeChanged(int size);

public slots:
    /**
     * @brief 工具栏画笔粗细变更槽函数
     *
     * 当用户在工具栏滑块上调节画笔粗细时调用，
     * 仅更新 SpinBox 显示以保持界面同步，不改变全局默认值。
     * @param width 新的画笔粗细值
     * @author chiangyang
     */
    void onToolPenWidthChanged(int width);

    /**
     * @brief 工具栏字号变更槽函数
     *
     * 当用户在工具栏滑块上调节字号时调用，
     * 仅更新 SpinBox 显示以保持界面同步，不改变全局默认值。
     * @param size 新的字号值
     * @author chiangyang
     */
    void onToolFontSizeChanged(int size);

    /**
     * @brief 工具栏橡皮擦粗细变更槽函数
     *
     * 当用户在工具栏滑块上调节橡皮擦粗细时调用，
     * 仅更新 SpinBox 显示以保持界面同步，不改变全局默认值。
     * @param width 新的橡皮擦粗细值
     * @author chiangyang
     */
    void onToolEraserWidthChanged(int width);

    /**
     * @brief 工具栏马赛克大小变更槽函数
     *
     * 当用户在工具栏滑块上调节马赛克大小时调用，
     * 仅更新 SpinBox 显示以保持界面同步，不改变全局默认值。
     * @param size 新的马赛克大小值
     * @author chiangyang
     */
    void onToolMosaicSizeChanged(int size);

private slots:
    /**
     * @brief 按当前选项卡内容重新自适应窗口高度
     *
     * 与 onTabChanged 的高度适配逻辑一致：先让页面缩放回 sizeHint
     * 自然高度，再把窗口高度动画到页面所需高度（含 tab 栏补偿）。
     * 更新组状态/渠道文字动态变化后调用，避免内容增长但窗口高度不变
     * 导致组内控件被压缩。切换选项卡能修复同款问题正是因为它触发了本逻辑。
     * @author chiangyang
     */
    void fitWindowHeight();

private:
    /**
     * @brief 设置UI
     * @author chiangyang
     */
    void setupUi();

    /**
     * @brief 设置通用选项卡
     * @author chiangyang
     */
    void setupGeneralTab();

    /**
     * @brief 设置快捷键选项卡
     * @author chiangyang
     */
    void setupShortcutsTab();

    /**
     * @brief 设置关于选项卡
     * @author chiangyang
     */
    void setupAboutTab();

    /**
     * @brief 设置翻译选项卡
     * @author chiangyang
     */
    void setupTranslateTab();

    /**
     * @brief 刷新所有 QGroupBox 的背景样式（绑定到 StyleManager 的 s_tabWidgetBgColor）
     *
     * 各选项卡改色/重置路径统一调用，
     * 覆盖通用/翻译/样式/快捷键/历史记录选项卡的全部 QGroupBox，
     * 避免每个 GroupBox 在每个改色路径下都重复写 if 判断。
     * @author chiangyang
     */
    void updateAllGroupBoxStyles();

    /**
     * @brief 刷新常规/翻译/历史记录三个选项卡根容器的背景色
     * @param color 新的背景色（通常是 StyleManager::getTabWidgetBgColor()）
     *
     * 这三个选项卡的根 QWidget 用 #objectName 选择器设 setStyleSheet 背景色，
     * 只匹配自身不影响子控件原生渲染（QCheckBox 等保持原生主题）。
     * QTabWidget::pane 的 QSS 背景色在 Windows 上不一定透过到 tab 页面，
     * 因此需显式设背景色。
     * @author chiangyang
     */
    void updateAllTabsPalette(const QColor &color);

    /**
     * @brief 加载设置
     * @author chiangyang
     */
    void loadSettings();

    QTabWidget *tabWidget;          ///< 选项卡控件
    SettingsGeneralTab *m_generalTabWidget; ///< 通用选项卡
    SettingsStyleTab *m_styleTabWidget;         ///< 样式选项卡


    // Shortcuts

    // History（选项卡内容内聚在 SettingsHistoryTab）
    SettingsHistoryTab *m_historyTabWidget; ///< 历史记录选项卡
    SettingsShortcutsTab *m_shortcutsTabWidget; ///< 快捷键选项卡

    // About（选项卡内容内聚在 SettingsAboutTab：应用信息 + 软件更新）
    SettingsAboutTab *m_aboutTab;   ///< 关于选项卡

    // Translate
    // Translate（选项卡内容内聚在 SettingsTranslateTab）
    SettingsTranslateTab *m_translateTabWidget; ///< 翻译选项卡

    QPropertyAnimation *heightAnimation; ///< 高度动画
    int m_animatedHeight; ///< 动画高度属性

private slots:

    /**
     * @brief 选项卡切换槽函数，用于自适应调整窗口高度
     * @param index 选项卡索引
     * @author chiangyang
     */
    void onTabChanged(int index);

    /**
     * @brief 屏幕 DPI 变化槽函数
     *
     * 系统缩放比例调整时触发，重新计算窗口宽度并适配当前选项卡高度。
     * @author chiangyang
     */
    void onDpiChanged();

    /**
     * @brief 带动画的高度设置
     * @param height 目标高度
     * @author chiangyang
     */



private:
    /**
     * @brief 为所有子控件安装事件过滤器
     * @author chiangyang
     */
    void installEventFilterOnChildren();

    /**
     * @brief 设置样式选项卡
     * @author chiangyang
     */
    void setupStyleTab();

    /**
     * @brief 设置历史记录选项卡
     * @author chiangyang
     */
    void setupHistoryTab();

    /**
     * @brief 根据当前屏幕逻辑 DPI 重新计算并应用窗口宽度
     *
     * 系统缩放比例变化时调用。基于当前屏幕的 logicalDotsPerInch 重新计算
     * 窗口宽度，更新最小/最大宽度约束与固定尺寸，并重新触发选项卡高度适配，
     * 确保窗口宽度和高度均随缩放比例正确变化。
     * @author chiangyang
     */
    void updateDpiScaledWidth();

    /**
     * @brief 根据当前屏幕逻辑 DPI 计算窗口宽度
     *
     * 96 DPI（100% 缩放）为基准，对应 500 像素；按比例放大并限制在
     * 合理范围（500~1000）。注意：qss 用 em 单位管控件尺寸，但管不了
     * 窗口整体宽度，这里仍需手动按 DPI 算。
     * @return 基于 DPI 缩放后的窗口宽度
     * @author chiangyang
     */
    int calculateDpiScaledWidth() const;

    /**
     * @brief 重新计算所有需要固定宽度的控件
     *
     * DPI 变化时调用，依次更新通用选项卡、历史记录选项卡、翻译选项卡
     * 中控件的固定/最小宽度，确保文字不被截断。
     * @author chiangyang
     */
    void updateControlWidths();

    /**
     * @brief 按当前选项卡内容调整窗口高度（可选动画）
     *
     * fitWindowHeight() 的实际实现。槽 fitWindowHeight() 保持无参签名以兼容
     * contentHeightChanged() 这类无参信号连接，内部固定走动画；
     * 首次显示与初始化校准等不该有动画的场景直接调用本方法并传 false。
     * @param animated 是否使用高度动画（false 时立即设置目标高度）
     * @note 目标高度与当前高度相同时直接返回，避免启动无意义的动画。
     * @author chiangyang
     */
    void applyTabHeight(bool animated);

    // ---- 颜色配置（数据驱动，元数据表归 StyleManager 所有） ----
    // 颜色配置元数据表（settingsKey/translationKey/defaultText/defaultColor）定义在
    // StyleManager::colorSettingTable()（core 层单一数据源），SettingsWindow 只消费它，
    // 不再持有第二份颜色表（原先 SettingsWindow::colorSettingTable 的默认色与
    // StyleManager::DEFAULT_* 不一致，是历史 bug 来源）。

    // 拖动相关
    bool m_isDragging = false;   ///< 是否正在拖动窗口
    bool m_firstShow = true;     ///< 是否尚未完成首次显示（用于首次显示时同步定高）
    QPoint m_dragStartPos;       ///< 拖动起始位置（全局坐标）
    QPoint m_widgetStartPos;     ///< 窗口起始位置（全局坐标）

    int m_dpiScaledWidth = 500;          ///< 基于屏幕逻辑 DPI 缩放后的窗口宽度
};

#endif // SETTINGSWINDOW_H
