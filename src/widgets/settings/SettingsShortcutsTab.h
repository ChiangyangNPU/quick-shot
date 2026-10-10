#ifndef SETTINGSSHORTCUTSTAB_H
#define SETTINGSSHORTCUTSTAB_H

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QGridLayout>
#include "../../core/ConfigManager.h"
#include "../../shortcut/ShortcutTypes.h"

/**
 * @brief 设置窗口「快捷键」选项卡
 *
 * 四个分组（全局热键可配置 / 标注工具 / 标注操作 / PinWindow）。
 * 可配置行由 kShortcutConfigs 数据表自动生成，OK/Reset 后经 shortcutChanged
 * 信号通知宿主（宿主转发给 ShortcutManager 更新热键）。
 * 从 SettingsWindow 拆分而来：行生成、重翻译、样式刷新、滚轮滚动事件
 * 过滤全部内聚于此。
 * @author chiangyang
 */
class SettingsShortcutsTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsShortcutsTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案（含全部快捷键行）
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

signals:
    /**
     * @brief 快捷键改变信号（宿主转发给 ShortcutManager 更新热键）
     * @param action 动作标识（如 "pin"）
     * @param keySequence 新键序
     * @author chiangyang
     */
    void shortcutChanged(const QString &action, const QKeySequence &keySequence);

protected:
    /**
     * @brief 事件过滤器：快捷键选项卡内的滚轮滚动
     *
     * 焦点在输入类控件（下拉框 / 文本框 / 键序编辑框）时不滚动，
     * 避免与控件自身交互冲突；其余滚轮滚动当前滚动区。
     * @param obj 被监视的对象
     * @param event 事件
     * @return 是否处理了事件
     * @author chiangyang
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUi();

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

    void addShortcutRow(QGridLayout *layout, int row, const QString &action,
                        const QString &configKey, const QString &defaultKey,
                        const QString &trKey, const QString &fallback);
    void addFixedShortcutRow(QGridLayout *layout, int row, const QString &trKey,
                             const QString &fallback, const QString &keys);

    QList<ShortcutRow> m_extraShortcutRows; ///< 可配置全局热键行
    QList<FixedShortcutRow> m_fixedShortcutRows; ///< 固定快捷键展示行
    QGroupBox *m_shortcutGroupGlobal = nullptr;     ///< 全局热键分组（可配置）
    QGroupBox *m_shortcutGroupTools = nullptr;      ///< 标注工具分组（1-8 切换工具）
    QGroupBox *m_shortcutGroupAnnotation = nullptr; ///< 标注操作分组（固定）
    QGroupBox *m_shortcutGroupPinWindow = nullptr;  ///< PinWindow 快捷键分组（固定）
    QScrollArea *m_shortcutsScrollArea = nullptr;   ///< 选项卡滚动区域
};

#endif // SETTINGSSHORTCUTSTAB_H
