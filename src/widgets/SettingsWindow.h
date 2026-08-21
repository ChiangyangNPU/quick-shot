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
#include "../update/UpdateManager.h"

class ConfigManager;
class QGridLayout;

/**
 * @brief 单行省略号标签
 *
 * 文字超过可用宽度时右侧以省略号截断（配合 setWordWrap(false) 保持单行），
 * 并同步设置 tooltip，鼠标悬停可查看完整文字。
 * 用于固定宽度窗口下避免提示文字被直接裁掉且无迹可寻。
 * 注：Qt6 的 QLabel 没有 setTextElideMode，需在 paintEvent 中用 QFontMetrics 手动省略。
 * @author chiangyang
 */
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {}

    // QLabel::setText 非虚函数，此处以同名覆盖（hiding），
    // 调用方通过 ElidedLabel* 访问即可命中本实现
    void setText(const QString &text) {
        m_fullText = text;
        QLabel::setText(text);
        setToolTip(text);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        // 以当前实际宽度省略，超出部分显示为 "…"，悬停 tooltip 查看全文
        const QString elided = fontMetrics().elidedText(m_fullText, Qt::ElideRight, width());
        painter.drawText(rect(), alignment(), elided);
    }

private:
    QString m_fullText; ///< 完整文字（绘制时按宽度省略）
};

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
     * @brief 语言改变槽函数
     * @param index 语言下拉框索引
     * @author chiangyang
     */
    void onLanguageChanged(int index);

    /**
     * @brief 自动启动改变槽函数
     * @param state 复选框状态
     * @author chiangyang
     */
    void onAutoStartChanged(Qt::CheckState state);

    /**
     * @brief 日志打印改变槽函数
     * @param state 复选框状态
     * @author chiangyang
     */
    void onLogPrintChanged(Qt::CheckState state);

    /**
     * @brief OCR 语言改变槽函数
     * @param index 下拉框索引
     * @author chiangyang
     */
    void onOcrLanguageChanged(int index);

    /**
     * @brief GPU 加速改变槽函数
     * @param state 复选框状态
     * @author chiangyang
     */
    void onGpuAccelChanged(Qt::CheckState state);

    /**
     * @brief 打开日志文件槽函数
     * @author chiangyang
     */
    void onOpenLogFile();

    /**
     * @brief 清除日志文件槽函数
     * @author chiangyang
     */
    void onClearLogFile();

    /**
     * @brief 选择截图保存目录
     * @author chiangyang
     */
    void onChooseCaptureSaveDir();

    /**
     * @brief 选择录制保存目录
     * @author chiangyang
     */
    void onChooseRecordSaveDir();

    /**
     * @brief 恢复截屏保存目录为默认
     * @author chiangyang
     */
    void onResetCaptureSaveDir();

    /**
     * @brief 恢复录屏保存目录为默认
     * @author chiangyang
     */
    void onResetRecordSaveDir();

    /**
     * @brief 打开配置文件所在文件夹
     * @author chiangyang
     */
    void onOpenConfigFileLocation();

    /**
     * @brief 更改配置文件
     * @author chiangyang
     */
    void onChangeConfigFile();

    /**
     * @brief 配置文件更改完成槽函数
     * @param success 是否成功
     * @param message 消息
     * @author chiangyang
     */
    void onConfigChanged(bool success, const QString &message);

    /**
     * @brief 配置文件路径更改槽函数
     * @param newPath 新路径
     * @author chiangyang
     */
    void onConfigPathChanged(const QString &newPath);

    /**
     * @brief 检查更新槽函数
     *
     * 手动触发检查服务器上是否有新版本可用
     * @author chiangyang
     */
    void onCheckForUpdate();

    /**
     * @brief 下载更新槽函数
     *
     * 下载远程更新包到本地临时目录
     * @author chiangyang
     */
    void onDownloadUpdate();

    /**
     * @brief 安装更新槽函数
     *
     * 应用已下载的更新包并重启应用
     * @author chiangyang
     */
    void onInstallUpdate();

    /**
     * @brief 取消更新槽函数
     *
     * 取消当前正在进行的检查、下载或安装操作
     * @author chiangyang
     */
    void onCancelUpdate();

    /**
     * @brief 更新检查完成槽函数
     *
     * 服务器请求完成后触发，传递版本信息与错误信息
     * @param success 检查是否成功
     * @param versionInfo 远程版本信息
     * @param errorInfo 错误信息（仅在失败时有效）
     * @author chiangyang
     */
    void onUpdateCheckFinished(bool success, const UpdateManager::VersionInfo &versionInfo, const UpdateManager::ErrorInfo &errorInfo);

    /**
     * @brief 更新下载进度槽函数
     *
     * 下载过程中定期触发，报告当前进度
     * @param bytesReceived 已接收字节数
     * @param bytesTotal 总字节数
     * @param percent 下载百分比（0-100）
     * @author chiangyang
     */
    void onUpdateDownloadProgress(qint64 bytesReceived, qint64 bytesTotal, int percent);

    /**
     * @brief 更新下载完成槽函数
     *
     * 下载完成（成功或失败）后触发
     * @param success 下载是否成功
     * @param filePath 下载文件的本地路径
     * @param errorInfo 错误信息（仅在失败时有效）
     * @author chiangyang
     */
    void onUpdateDownloadFinished(bool success, const QString &filePath, const UpdateManager::ErrorInfo &errorInfo);

    /**
     * @brief 更新安装完成槽函数
     *
     * UpdateManager 安装流程结束（成功或失败）后触发，用于同步UI显示
     * @param success 安装是否成功
     * @param message 结果描述
     * @author chiangyang
     */
    void onUpdateInstallFinished(bool success, const QString &message);

    /**
     * @brief 重置更新相关UI
     * @author chiangyang
     */
    void resetUpdateUI();

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
     * @brief 设置更新状态文字并缓存翻译键，供语言切换时重新渲染
     * @param key 翻译键（空表示文字不可重建，如安装结果消息，将原样保留 fallback）
     * @param fallback 翻译缺失时的兜底文字
     * @param args 格式化参数（替换 %1、%2...）
     * @author chiangyang
     */
    void setUpdateStatusText(const QString &key, const QString &fallback, const QStringList &args = QStringList());

    /**
     * @brief 按缓存的翻译键重新渲染更新状态文字（语言切换时调用）
     * @author chiangyang
     */
    void renderUpdateStatusText();

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
     * @brief 快捷键行数据
     *
     * 描述一行可配置快捷键的全部信息与控件指针，供 addShortcutRow 创建、
     * retranslateUi 更新文案、信号槽读写配置使用。
     * @author chiangyang
     */
    struct ShortcutRow {
        QString action;       ///< 动作标识（如 "pin"），用于 shortcutChanged 信号
        QString configKey;    ///< QSettings 键（如 "shortcut_pin"）
        QString defaultKey;   ///< 默认快捷键（如 "Alt+P"）
        QString trKey;        ///< 翻译键（如 "shortcut.pin.label"）
        QString fallback;     ///< 翻译缺失时的回退文案（如 "Pin Clipboard"）
        QKeySequenceEdit *edit = nullptr;   ///< 快捷键输入框
        QLabel *label = nullptr;            ///< 行标签
        QPushButton *btnOk = nullptr;       ///< 确定按钮
        QPushButton *btnCancel = nullptr;   ///< 取消按钮
        QPushButton *btnReset = nullptr;    ///< 恢复默认按钮
    };

    /**
     * @brief 固定快捷键行数据
     *
     * 描述一行不可配置（固定约定）的快捷键信息，供 addFixedShortcutRow 创建、
     * retranslateUi 更新功能名文案使用。键位文本 keys 硬编码无需翻译。
     * @author chiangyang
     */
    struct FixedShortcutRow {
        QString trKey;      ///< 功能名翻译键（如 "shortcut.fixed.save"）
        QString fallback;   ///< 翻译缺失时的回退文案（如 "Save to File"）
        QString keys;       ///< 键位文本（硬编码，如 "Ctrl+S" / "1" / "Ctrl+Y / Ctrl+Shift+Z"）
        QLabel *label = nullptr;        ///< 功能名标签
        QLineEdit *edit = nullptr;      ///< 只读禁用的键位展示框
    };

    /**
     * @brief 数据驱动地添加一行快捷键配置
     *
     * 创建 Label + QKeySequenceEdit + 确定/取消/恢复 三按钮，套用与现有 snip/record 行
     * 一致的样式与固定高度，连接 OK/Cancel/Reset 信号槽读写配置并发射 shortcutChanged。
     * 创建的行信息存入 m_extraShortcutRows，供 retranslateUi 更新文案。
     * @param layout 快捷键选项卡的网格布局
     * @param row 该行在网格中的行号
     * @param action 动作标识
     * @param configKey QSettings 键
     * @param defaultKey 默认快捷键
     * @param trKey 翻译键
     * @param fallback 翻译回退文案
     * @author chiangyang
     */
    void addShortcutRow(QGridLayout *layout, int row, const QString &action,
                        const QString &configKey, const QString &defaultKey,
                        const QString &trKey, const QString &fallback);

    /**
     * @brief 数据驱动地添加一行固定快捷键展示
     *
     * 创建 Label（功能名）+ QLineEdit（只读禁用，展示键位文本），套用与可配置行
     * 一致的输入框样式与固定高度。键位输入框置灰（setEnabled(false)）明确传达不可编辑。
     * 创建的行信息存入 m_fixedShortcutRows，供 retranslateUi 更新功能名文案。
     * @param layout 所属分组的网格布局
     * @param row 该行在网格中的行号
     * @param trKey 功能名翻译键
     * @param fallback 翻译回退文案
     * @param keys 键位文本（硬编码，如 "Ctrl+S"）
     * @author chiangyang
     */
    void addFixedShortcutRow(QGridLayout *layout, int row, const QString &trKey,
                             const QString &fallback, const QString &keys);

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
     * 在 applyColorChange() 改色和 onResetStyle() 重置时统一调用，
     * 覆盖通用/翻译/样式/历史记录 4 个选项卡的全部 QGroupBox，
     * 避免每个 GroupBox 在每个改色路径下都重复写 if 判断。
     * @author chiangyang
     */
    void updateAllGroupBoxStyles();

    /**
     * @brief 刷新历史记录选项卡滚动区的 QPalette 背景色
     * @param color 新的背景色（通常是 StyleManager::getTabWidgetBgColor()）
     *
     * 历史记录选项卡用 QPalette 设背景（避免 setStyleSheet 禁用子控件原生渲染），
     * 每次改颜色时需重新设置 m_historyScrollContent 和 historyScrollArea->viewport() 的调色板。
     * @author chiangyang
     */
    void updateHistoryTabPalette(const QColor &color);

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
     * @brief 更新翻译选项卡两个下拉框宽度
     *
     * 取翻译引擎下拉框与目标语言下拉框中所有条目文本最宽者，
     * 统一设置二者的最小宽度，保证两框宽度一致（谁长选谁）。
     * 在 setupTranslateTab 与 retranslateUi 中调用，确保语言切换后宽度自适应。
     * @author chiangyang
     */
    void updateTranslateComboWidth();

    /**
     * @brief 加载设置
     * @author chiangyang
     */
    void loadSettings();

    QTabWidget *tabWidget;          ///< 选项卡控件
    QScrollArea *styleScrollArea;   ///< 样式选项卡滚动区域
    QScrollArea *historyScrollArea; ///< 历史记录选项卡滚动区域
    QWidget *m_historyScrollContent; ///< 历史记录选项卡滚动区域内的内容容器
    QWidget *m_generalTab;          ///< 通用选项卡根容器
    QWidget *m_translateTab;        ///< 翻译选项卡根容器
    QWidget *m_historyTab;          ///< 历史记录选项卡根容器

    // General
    QComboBox *langCombo;           ///< 语言下拉框
    QCheckBox *autoStartCheck;      ///< 自动启动复选框
    QCheckBox *logPrintCheck;       ///< 日志打印复选框
    QLabel *lblLanguage;            ///< 语言标签
    QLabel *lblAutoStart;           ///< 自动启动标签
    QPushButton *btnOpenLogFile;    ///< 打开日志文件按钮
    QPushButton *btnClearLogFile;   ///< 清除日志文件按钮
    QLabel *lblOcrLanguage;         ///< OCR 语言标签
    QComboBox *cmbOcrLanguage;      ///< OCR 语言下拉框
    QCheckBox *gpuAccelCheck;       ///< GPU 加速复选框
    QLabel *lblCaptureSaveDir;      ///< 截屏保存路径标签
    QLineEdit *editCaptureSaveDir;  ///< 截屏保存路径
    QPushButton *btnCaptureChooseDir; ///< 选择截屏保存路径按钮
    QPushButton *btnCaptureResetDir; ///< 恢复截屏默认路径按钮
    QLabel *lblRecordSaveDir;       ///< 录屏保存路径标签
    QLineEdit *editRecordSaveDir;   ///< 录屏保存路径
    QPushButton *btnRecordChooseDir; ///< 选择录屏保存路径按钮
    QPushButton *btnRecordResetDir; ///< 恢复录屏默认路径按钮
    QGroupBox *saveGroup;           ///< 通用选项卡保存路径设置组
    QGroupBox *configGroup;         ///< 通用选项卡配置文件管理组

    // Config File Management
    QLabel *lblConfigTitle;       ///< 配置文件标题标签
    QLineEdit *lblConfigFileName; ///< 配置文件路径文本框
    QPushButton *btnOpenConfigDir; ///< 打开配置文件所在文件夹按钮
    QPushButton *btnChangeConfig;   ///< 更改配置文件按钮
    ConfigManager *m_configManager; ///< 配置文件管理器

    // Shortcuts
    QList<ShortcutRow> m_extraShortcutRows; ///< 可配置全局热键行（截图/录屏/历史/贴图/全屏/活动窗口/录屏暂停/录屏停止/隐藏贴图）
    QList<FixedShortcutRow> m_fixedShortcutRows; ///< 固定快捷键展示行（截图标注/PinWindow，只读禁用）
    QGroupBox *m_shortcutGroupGlobal = nullptr;     ///< 快捷键选项卡-全局热键分组（可配置）
    QGroupBox *m_shortcutGroupTools = nullptr;      ///< 快捷键选项卡-标注工具分组（1-8 切换工具）
    QGroupBox *m_shortcutGroupAnnotation = nullptr; ///< 快捷键选项卡-标注操作分组（复制/撤销/保存/约束等）
    QGroupBox *m_shortcutGroupPinWindow = nullptr;  ///< 快捷键选项卡-PinWindow 快捷键分组（固定）
    QScrollArea *m_shortcutsScrollArea = nullptr;   ///< 快捷键选项卡滚动区域

    // History
    QGroupBox *historyRecordGroup;      ///< 历史记录设置组
    QGroupBox *historyStorageGroup;     ///< 历史存储设置组
    QGroupBox *historyDataGroup;        ///< 历史数据管理组
    QGroupBox *historyStatsGroup;       ///< 历史统计信息组
    QCheckBox *m_screenshotHistoryCheck;  ///< 记录截图历史复选框
    QCheckBox *m_clipboardHistoryCheck;   ///< 记录剪贴板历史复选框
    QLabel *lblRetentionDays;               ///< 保留天数标签
    QComboBox *cmbRetentionDays;            ///< 保留天数下拉框
    QLabel *lblMaxItems;                    ///< 最大记录数标签
    QComboBox *cmbMaxItems;                 ///< 最大记录数下拉框
    QLabel *lblHistoryStats;                ///< 历史统计信息标签
    QPushButton *btnCleanHistory;           ///< 清理历史按钮
    QPushButton *btnClearHistory;           ///< 清空历史按钮

    // About
    QLabel *appNameLabel;           ///< 应用名称标签
    QLabel *versionLabel;           ///< 版本标签
    QLabel *copyrightLabel;         ///< 版权标签
    QLabel *qtLinkLabel;            ///< Qt链接标签
    QLabel *githubLabel;            ///< GitHub仓库链接标签
    QLabel *giteeLabel;             ///< Gitee仓库链接标签
    QLabel *emailLabel;             ///< 联系邮箱标签
    QGroupBox *m_updateGroup;       ///< 更新分组
    UpdateManager *m_updateManager;        ///< 更新管理器
    QPushButton *btnCheckUpdate;            ///< 检查更新按钮
    ElidedLabel *lblUpdateStatus;           ///< 更新状态标签
    QProgressBar *m_updateProgressBar;      ///< 更新进度条
    QPushButton *btnDownloadUpdate;         ///< 下载更新按钮
    QPushButton *btnInstallUpdate;          ///< 安装更新按钮
    QPushButton *btnCancelUpdate;           ///< 取消更新按钮
    QLabel *lblUpdateChannel;               ///< 更新渠道标签
    QString m_updateStatusKey;      ///< 更新状态文字翻译键（空表示不可重建）
    QString m_updateStatusFallback; ///< 更新状态文字兜底（翻译缺失/不可重建时原样使用）
    QStringList m_updateStatusArgs; ///< 更新状态文字格式化参数

    // Translate
    QGroupBox *m_translateEngineGroup;  ///< 翻译引擎组
    QGroupBox *m_translateOptionsGroup; ///< 翻译选项组（目标语言+开关）
    QComboBox *m_translateEngineCombo;  ///< 翻译引擎下拉框
    QComboBox *m_translateLangCombo;    ///< 翻译语言下拉框
    QLineEdit *m_mymemoryEmailEdit;     ///< MyMemory 邮箱输入框
    QLineEdit *m_baiduAppIdEdit;        ///< 百度 AppID 输入框
    QLineEdit *m_baiduKeyEdit;          ///< 百度密钥输入框
    QLineEdit *m_deeplKeyEdit;          ///< DeepL Key 输入框
    QLineEdit *m_libreUrlEdit;          ///< LibreTranslate 地址输入框
    QCheckBox *m_translateEnabledCheck; ///< 启用翻译复选框
    QCheckBox *m_translatePrivacyCheck; ///< 隐私提示复选框
    QWidget *m_mymemoryConfigWidget;    ///< MyMemory 配置容器
    QWidget *m_baiduConfigWidget;       ///< 百度配置容器
    QWidget *m_deeplConfigWidget;       ///< DeepL 配置容器
    QWidget *m_libreConfigWidget;       ///< LibreTranslate 配置容器
    QLabel *lblTranslateEngine;         ///< 翻译引擎标签
    QLabel *lblTranslateLang;           ///< 翻译语言标签
    QLabel *lblMymemoryEmail;           ///< MyMemory 邮箱标签
    QLabel *lblMymemoryWebsite;         ///< MyMemory 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblMymemoryNetworkHint; ///< MyMemory 网络访问说明标签
    QLabel *lblBaiduAppId;              ///< 百度 AppID 标签
    QLabel *lblBaiduKey;                ///< 百度密钥标签
    QLabel *lblBaiduWebsite;            ///< 百度翻译官网链接标签（点击跳转浏览器）
    ElidedLabel *lblBaiduNetworkHint;   ///< 百度翻译网络访问说明标签
    QLabel *lblDeeplKey;                ///< DeepL Key 标签
    QLabel *lblDeeplWebsite;             ///< DeepL 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblDeeplNetworkHint;   ///< DeepL 网络访问说明标签
    QLabel *lblLibreUrl;                ///< LibreTranslate 地址标签
    QLabel *lblLibreWebsite;            ///< LibreTranslate 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblLibreNetworkHint;   ///< LibreTranslate 网络访问说明标签

    // Style
    QGroupBox *borderGroup;         ///< 边框颜色设置组
    QGroupBox *toolbarGroup;        ///< 工具栏样式设置组
    QGroupBox *tabGroup;            ///< 选项卡样式设置组
    QWidget *styleTabContainer;     ///< 样式选项卡容器
    QWidget *scrollContainer;       ///< 滚动区域容器
    QPropertyAnimation *heightAnimation; ///< 高度动画
    int m_animatedHeight; ///< 动画高度属性
    QPushButton *btnResetStyle;     ///< 重置样式按钮

    // Toolbar Button Style
    QLabel *lblToolbarButtonStyle;  ///< 工具栏按钮样式标签
    QComboBox *cmbToolbarButtonStyle; ///< 工具栏按钮样式下拉框

    // Annotation Defaults
    QGroupBox *m_annotationDefaultsGroup; ///< 标注工具默认值设置组
    QLabel *lblDefaultPenWidth;          ///< 画笔默认粗细标签
    QSpinBox *spnDefaultPenWidth;        ///< 画笔默认粗细 SpinBox
    QLabel *lblDefaultFontSize;          ///< 文本默认字号标签
    QSpinBox *spnDefaultFontSize;        ///< 文本默认字号 SpinBox
    QLabel *lblDefaultEraserWidth;       ///< 橡皮擦默认粗细标签
    QSpinBox *spnDefaultEraserWidth;     ///< 橡皮擦默认粗细 SpinBox
    QLabel *lblDefaultMosaicSize;        ///< 马赛克默认大小标签
    QSpinBox *spnDefaultMosaicSize;      ///< 马赛克默认大小 SpinBox

private slots:
    /**
     * @brief 重置样式槽函数
     * @author chiangyang
     */
    void onResetStyle();

    /**
     * @brief 工具栏按钮样式改变槽函数
     * @param index 下拉框索引
     * @author chiangyang
     */
    void onToolbarButtonStyleChanged(int index);

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
     * @brief 翻译引擎切换槽函数，显隐对应引擎配置行
     * @param index 引擎下拉框索引
     * @author chiangyang
     */
    void onTranslateEngineChanged(int index);

    /**
     * @brief 翻译设置改变槽函数（保存配置并重载翻译引擎）
     * @author chiangyang
     */
    void onTranslateSettingChanged();

    /**
     * @brief 带动画的高度设置
     * @param height 目标高度
     * @author chiangyang
     */
    void setFixedHeightWithAnimation(int height);



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
     * @brief 更新历史统计信息
     * @author chiangyang
     */
    void updateHistoryStats();

    /**
     * @brief 显示颜色选择器
     * @param currentColor 当前颜色
     * @return 选择的颜色
     * @author chiangyang
     */
    QColor showColorDialog(const QColor &currentColor);

    /**
     * @brief 创建一行颜色选择控件（标签 + 弹性间距 + 色块按钮）
     *
     * 统一封装样式选项卡中重复的颜色行创建逻辑：创建标签和色块按钮，
     * 按钮使用 #settingColorButton 对象名（尺寸/边框/圆角由全局 qss 管理），
     * 并通过 outLabel/outBtn 输出指针供调用方保存为成员变量、连接信号。
     *
     * @param parent 父控件（通常为所属 QGroupBox）
     * @param labelText 标签文本（已翻译）
     * @param color 初始颜色
     * @param outBtn 输出：创建的色块按钮指针（供调用方保存为成员变量）
     * @param outLabel 输出：创建的标签指针（供 retranslateUi 更新文本）
     * @return 包含标签和按钮的水平布局（已含 addStretch）
     * @author chiangyang
     */
    QHBoxLayout *createColorRow(QWidget *parent, const QString &labelText,
                                 const QColor &color, QPushButton *&outBtn, QLabel *&outLabel);

    /**
     * @brief 更新颜色按钮的显示颜色
     *
     * 仅设置 background-color，尺寸/边框/圆角仍由全局 qss 的 #settingColorButton 管理，
     * 保持 DPI 自适应缩放。
     *
     * @param btn 颜色按钮
     * @param color 新颜色
     * @author chiangyang
     */
    void updateColorButton(QPushButton *btn, const QColor &color);

    /**
     * @brief 保存样式设置
     * @author chiangyang
     */
    void saveStyleSettings();

    /**
     * @brief 加载样式设置
     * @author chiangyang
     */
    void loadStyleSettings();

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
     * @brief 重新计算通用选项卡中控件的固定宽度
     *
     * 基于当前字体（随 DPI 缩放）重新计算标签固定宽度与下拉框最小宽度，
     * 确保 DPI 变化后控件宽度能容纳放大后的文本，避免文字被截断。
     * 在 setupGeneralTab 构造时与 DPI 变化时均会调用。
     * @author chiangyang
     */
    void updateGeneralControlWidths();

    /**
     * @brief 重新计算历史记录选项卡下拉框的固定宽度
     *
     * 取保留天数与最大记录数两个下拉框的最大内容宽度作为统一宽度。
     * 在 setupHistoryTab 构造时、语言切换时与 DPI 变化时均会调用，
     * 消除原先 setupHistoryTab 与 retranslateUi 中的重复代码。
     * @author chiangyang
     */
    void updateHistoryComboWidth();

    /**
     * @brief 重新计算所有需要固定宽度的控件
     *
     * DPI 变化时调用，依次更新通用选项卡、历史记录选项卡、翻译选项卡
     * 中控件的固定/最小宽度，确保文字不被截断。
     * @author chiangyang
     */
    void updateControlWidths();

    // ---- 颜色配置（数据驱动，元数据表归 StyleManager 所有） ----
    // 颜色配置元数据表（settingsKey/translationKey/defaultText/defaultColor）定义在
    // StyleManager::colorSettingTable()（core 层单一数据源），SettingsWindow 只消费它，
    // 不再持有第二份颜色表（原先 SettingsWindow::colorSettingTable 的默认色与
    // StyleManager::DEFAULT_* 不一致，是历史 bug 来源）。

    /**
     * @brief 通用颜色变更处理
     *
     * 统一替代原先 17 个 onXxxColorChanged 槽函数的重复逻辑：
     * 1. 弹出颜色对话框
     * 2. 若用户选择了新颜色：更新 StyleManager、刷新按钮显示、保存设置
     * 3. 执行分类对应的样式联动
     * 4. 发射信号（若有：仅 tabWidgetBgColorChanged 有订阅者）
     * @param id 颜色 ID（StyleManager::StyleColorId，兼作按钮数组索引）
     * @author chiangyang
     */
    void applyColorChange(StyleManager::StyleColorId id);

    /**
     * @brief 颜色变更后的样式联动
     *
     * 不同分类的颜色变更会触发不同的样式刷新：
     * - Border/Toolbar：无后处理
     * - TabButton：仅刷新 tabWidget 样式表
     * - TabWidgetBg：刷新 tabWidget + 容器 + GroupBox + 三个 tab 的背景 + 历史 tab Palette
     *
     * @param category 颜色分类（StyleManager::StyleColorCategory）
     * @param newColor 新颜色（仅 TabWidgetBg 类用到）
     * @author chiangyang
     */
    void applyColorPostUpdate(StyleManager::StyleColorCategory category, const QColor& newColor);

    std::array<QPushButton*, StyleManager::kStyleColorCount> m_colorButtons{};  ///< 颜色按钮数组（按 StyleColorId 索引）
    std::array<QLabel*, StyleManager::kStyleColorCount> m_colorLabels{};        ///< 颜色标签数组（按 StyleColorId 索引）

    // 拖动相关
    bool m_isDragging = false;   ///< 是否正在拖动窗口
    QPoint m_dragStartPos;       ///< 拖动起始位置（全局坐标）
    QPoint m_widgetStartPos;     ///< 窗口起始位置（全局坐标）

    int m_dpiScaledWidth = 500;          ///< 基于屏幕逻辑 DPI 缩放后的窗口宽度
};

#endif // SETTINGSWINDOW_H
