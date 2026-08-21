#ifndef TRAYMENUBUILDER_H
#define TRAYMENUBUILDER_H

#include <QObject>
#include <QHash>
#include <QKeySequence>

#include "ShortcutTypes.h"

class QMenu;
class QAction;
class SnipScreen;
class HistoryWindow;

/**
 * @brief 托盘菜单构建器（工厂方法模式 + 数据驱动）
 *
 * 从 ShortcutConfigItem 数据表逐项创建菜单项，
 * 自动拼接"菜单文本 + (快捷键)"，并绑定对应 action 触发逻辑。
 *
 * 提供 refresh() 增量刷新快捷键显示值，
 * 提供 retranslate() 语言切换后重新翻译文案。
 *
 * 菜单项通过 QHash<ShortcutType, QAction*> 集中管理，
 * 代替原先 main.cpp 中 9 个独立 QAction 变量的硬编码创建与重复文本拼接。
 *
 * @author chiangyang
 */
class TrayMenuBuilder : public QObject {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param parent 父对象（通常是 ShortcutManager，保证生命周期一致）
     * @author chiangyang
     */
    explicit TrayMenuBuilder(QObject* parent = nullptr);

    /**
     * @brief 析构函数，清空 m_actions 引用表。QAction 所有权归 QMenu，由 Qt 父子关系释放
     * @author chiangyang
     */
    ~TrayMenuBuilder() override;

    /**
     * @brief 构建托盘菜单（工厂方法 + 数据驱动）
     * @param menu 目标托盘菜单（必须已创建，调用方负责添加设置/退出等非快捷键菜单项）
     * @param snipScreen 截图主界面（绑定 action）
     * @param historyWindow 历史记录窗口（绑定 action）
     * @param settingsWindow 设置窗口（绑定设置 action，可传 nullptr 后通过 setSettingsWindow 补充）
     *
     * 菜单项顺序与原 main.cpp 保持一致：
     *   [历史记录] → 分隔线 → [截图/录屏/贴图/全屏/活动窗口/录屏暂停/停止/TogglePins] → 分隔线
     * 第一项与最后一项后都插入分隔线，方便外部追加"设置""退出"等静态菜单项。
     * @note 使用位置: ShortcutManager::buildTrayMenu() 内部调用
     * @author chiangyang
     */
    void build(QMenu* menu, SnipScreen* snipScreen,
               HistoryWindow* historyWindow, QWidget* settingsWindow);

    /**
     * @brief 设置 SettingsWindow 指针（若 build 时为 nullptr，可在此处补充）
     * @param settingsWindow 设置窗口
     * @note 保留此接口以便分阶段构建场景，当前实现主要作为扩展点
     * @author chiangyang
     */
    void setSettingsWindow(QWidget* settingsWindow);

public slots:
    /**
     * @brief 刷新菜单项中的快捷键显示值（重新拼接"菜单项文本 + (快捷键)"）
     *
     * 用户在 SettingsWindow 修改快捷键后，通过 ShortcutManager::shortcutChanged
     * 信号自动触发此函数，实现托盘菜单与最新配置的实时同步。
     * @note 使用位置: ShortcutManager::shortcutChanged 信号、手动 refreshTrayMenu()
     * @author chiangyang
     */
    void refresh();

    /**
     * @brief 重新翻译菜单项文本（语言切换后调用，所有菜单项重新走 tm->get()）
     *
     * 当前实现等价于 refresh()（buildActionText 内部已走 tm->get()），
     * 保留独立接口以便未来若有"菜单 title/特殊非 action 文本"时扩展。
     * @note 使用位置: TranslationManager::languageChanged 信号触发时调用
     * @author chiangyang
     */
    void retranslate();

    /**
     * @brief ShortcutManager 的枚举信号适配入口（重载版，内部转发到无参 refresh）
     * @param type 未使用：refresh 内部全量重算所有菜单项显示值
     * @param sequence 未使用
     *
     * ShortcutManager::shortcutChanged(ShortcutType, QKeySequence) 信号签名
     * 与此重载完全匹配，可直接 connect 到这里，实现观察者模式。
     * @author chiangyang
     */
    void refresh(ShortcutType type, const QKeySequence& sequence);

private:
    /**
     * @brief 构造单个菜单项的显示文本
     * @param cfg 快捷键配置项
     * @return 格式为 "翻译后的菜单项文本 + 空格 + (当前快捷键字符串)"
     *
     * 翻译文本优先取 tm->get(trayTextKey, trayFallback)，
     * 快捷键值取配置文件中的当前值或默认值。
     * @note 被 build() 和 refresh() 两个公开方法共用，是数据驱动的核心格式化函数
     * @author chiangyang
     */
    QString buildActionText(const ShortcutConfigItem& cfg) const;

    QMenu* m_menu;                                           ///< 目标托盘菜单（弱引用）
    SnipScreen* m_snipScreen;                                ///< 截图主界面（弱引用）
    HistoryWindow* m_historyWindow;                          ///< 历史记录窗口（弱引用）
    QWidget* m_settingsWindow;                               ///< 设置窗口（弱引用）

    QHash<ShortcutType, QAction*> m_actions;                 ///< 类型到 QAction 映射
};

#endif // TRAYMENUBUILDER_H
