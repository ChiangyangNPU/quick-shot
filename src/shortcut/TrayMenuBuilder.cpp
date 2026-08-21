#include "TrayMenuBuilder.h"

#include <QMenu>
#include <QAction>
#include <QWidget>

#include "ShortcutTypes.h"
#include "../capture/SnipScreen.h"
#include "../widgets/HistoryWindow.h"
#include "../widgets/PinWindow.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

/**
 * @brief 构造函数
 * @param parent 父对象（通常是 ShortcutManager，保证生命周期一致）
 * @author chiangyang
 */
TrayMenuBuilder::TrayMenuBuilder(QObject* parent)
    : QObject(parent)
    , m_menu(nullptr)
    , m_snipScreen(nullptr)
    , m_historyWindow(nullptr)
    , m_settingsWindow(nullptr) {
    LOG_INFO("[TrayMenuBuilder] Created");
}

/**
 * @brief 析构函数
 *
 * QAction 的所有权归属 QMenu（由 menu->addAction 时指定父对象），
 * 这里只清空本地 Hash 表的引用，避免二次释放。
 * @author chiangyang
 */
TrayMenuBuilder::~TrayMenuBuilder() {
    m_actions.clear(); // QAction 归 QMenu 所有，由 Qt 父子关系释放
    LOG_INFO("[TrayMenuBuilder] Destroyed");
}

/**
 * @brief 构造单个菜单项的显示文本
 * @param cfg 快捷键配置项
 * @return "翻译后的菜单项文本 + 空格 + 括号内的当前快捷键"
 *
 * 示例：返回 "Capture (Alt+Q)"、"Pin Clipboard (Alt+P)" 等格式。
 * 翻译文本优先取 tm->get(trayTextKey, trayFallback)，
 * 快捷键值取配置文件中的当前值或默认值。
 * @note 被 build() 和 refresh() 两个公开方法共用，是数据驱动的核心格式化函数
 * @author chiangyang
 */
QString TrayMenuBuilder::buildActionText(const ShortcutConfigItem& cfg) const {
    TranslationManager* tm = TranslationManager::instance();
    QString text = tm->get(cfg.trayTextKey, cfg.trayFallback);
    QKeySequence seq = getShortcutSequence(cfg);
    return QString("%1 (%2)").arg(text).arg(seq.toString());
}

/**
 * @brief 设置 SettingsWindow 指针（当 build 时未传入时后续补充）
 * @param settingsWindow 设置窗口对象指针
 * @note 保留此接口以便分阶段构建场景，当前实现主要作为扩展点
 * @author chiangyang
 */
void TrayMenuBuilder::setSettingsWindow(QWidget* settingsWindow) {
    m_settingsWindow = settingsWindow;
}

/**
 * @brief 构建托盘菜单（工厂方法）
 * @param menu 目标托盘菜单（必须已创建，由 main.cpp 管理生命周期）
 * @param snipScreen 截图主界面（绑定 action）
 * @param historyWindow 历史记录窗口（绑定 action）
 * @param settingsWindow 设置窗口（可传 nullptr，后续通过 setSettingsWindow 补充）
 *
 * 菜单项顺序严格与原 main.cpp 保持一致：
 *   [历史记录] → 分隔线 → [截图/录屏/贴图/全屏/活动窗口/录屏暂停/停止/TogglePins] → 分隔线
 * 第一项和最后一项后都插入分隔线，方便外部追加"设置"、"退出"等静态菜单项。
 * 构建完成后菜单项与类型映射保存在 m_actions Hash 表，供后续 refresh()/retranslate() 增量更新。
 * @note 使用位置: ShortcutManager::buildTrayMenu() 内部调用
 * @author chiangyang
 */
void TrayMenuBuilder::build(QMenu* menu, SnipScreen* snipScreen,
                            HistoryWindow* historyWindow, QWidget* settingsWindow) {
    if (!menu) return;
    m_menu = menu;
    m_snipScreen = snipScreen;
    m_historyWindow = historyWindow;
    m_settingsWindow = settingsWindow;
    m_actions.clear();

    TranslationManager* tm = TranslationManager::instance();

    // 与原 main.cpp 顺序保持一致：
    // 先历史记录 → 分隔线 → 截图/录屏/贴图/全屏/活动窗口/录屏暂停/停止/TogglePins → 分隔线（外部追加设置/退出）

    // 1. 历史记录
    {
        const ShortcutConfigItem* cfg = getShortcutConfig(ShortcutType::History);
        if (cfg) {
            QAction* act = menu->addAction(buildActionText(*cfg));
            m_actions.insert(ShortcutType::History, act);
            QObject::connect(act, &QAction::triggered, [this, historyWindow]() {
                if (historyWindow) {
                    historyWindow->show();
                    historyWindow->raise();
                    historyWindow->activateWindow();
                }
            });
        }
    }

    menu->addSeparator();

    // 2. 按数据表顺序逐个创建菜单项（跳过 History，已单独处理）
    const ShortcutType orderedTypes[] = {
        ShortcutType::Snip,
        ShortcutType::Record,
        ShortcutType::Pin,
        ShortcutType::Fullscreen,
        ShortcutType::ActiveWindow,
        ShortcutType::RecordPause,
        ShortcutType::RecordStop,
        ShortcutType::TogglePins,
    };

    for (ShortcutType t : orderedTypes) {
        const ShortcutConfigItem* cfg = getShortcutConfig(t);
        if (!cfg || !cfg->trayTextKey) continue;

        QAction* act = menu->addAction(buildActionText(*cfg));
        m_actions.insert(t, act);

        switch (t) {
            case ShortcutType::Snip:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->start();
                });
                break;
            case ShortcutType::Record:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->startRecording();
                });
                break;
            case ShortcutType::Pin:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->pinClipboard();
                });
                break;
            case ShortcutType::Fullscreen:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->grabFullscreen();
                });
                break;
            case ShortcutType::ActiveWindow:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->grabActiveWindow();
                });
                break;
            case ShortcutType::RecordPause:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->togglePauseRecording();
                });
                break;
            case ShortcutType::RecordStop:
                QObject::connect(act, &QAction::triggered, [this]() {
                    if (m_snipScreen) m_snipScreen->stopRecording();
                });
                break;
            case ShortcutType::TogglePins:
                QObject::connect(act, &QAction::triggered, []() {
                    PinWindow::toggleAll();
                });
                break;
            default:
                break;
        }
    }

    menu->addSeparator();

    LOG_INFO(QString("[TrayMenuBuilder] Build done, %1 shortcut actions created")
                 .arg(m_actions.size()));
}

/**
 * @brief 刷新所有菜单项的显示值（重新拼接"菜单项文本 + (快捷键)"）
 *
 * 遍历 m_actions 表，重新调用 buildActionText 为每一项赋值。
 * 用户在 SettingsWindow 修改快捷键后，通过 ShortcutManager::shortcutChanged
 * 信号自动触发此函数，实现托盘菜单与最新配置的实时同步。
 * 无菜单项时直接返回避免空转。
 * @note 使用位置: ShortcutManager::shortcutChanged 信号触发、手动 refreshTrayMenu() 调用
 * @author chiangyang
 */
void TrayMenuBuilder::refresh() {
    if (m_actions.isEmpty()) return;
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        ShortcutType t = it.key();
        QAction* act = it.value();
        if (!act) continue;
        const ShortcutConfigItem* cfg = getShortcutConfig(t);
        if (!cfg) continue;
        act->setText(buildActionText(*cfg));
    }
    LOG_INFO(QString("[TrayMenuBuilder] Refreshed %1 menu action texts")
                 .arg(m_actions.size()));
}

/**
 * @brief ShortcutManager 枚举信号适配入口（重载版）
 * @param type 未使用：refresh 内部全量重算所有菜单项显示值
 * @param sequence 未使用
 *
 * ShortcutManager::shortcutChanged(ShortcutType, QKeySequence) 信号签名
 * 与此重载完全匹配，可直接 connect 到此函数，实现观察者模式。
 * 内部简单委托到无参 refresh()，保持一处实现两处入口。
 * @author chiangyang
 */
void TrayMenuBuilder::refresh(ShortcutType /*type*/, const QKeySequence& /*sequence*/) {
    refresh();
}

/**
 * @brief 重新翻译菜单项（语言切换后调用）
 *
 * 当前实现上 retranslate 与 refresh 等价：buildActionText 内部已经走
 * tm->get() 获取最新翻译，所以直接复用 refresh 逻辑。
 * 保留独立接口是为了在未来若有"菜单 title/特殊非 action 文本"时扩展。
 * @note 使用位置: TranslationManager::languageChanged 信号触发时调用
 * @author chiangyang
 */
void TrayMenuBuilder::retranslate() {
    if (!m_menu) return;
    // 简单实现：和 refresh 相同（buildActionText 内部已走 tm->get()），
    // 唯一差异是 menu 的 title / separator 不影响（此处均无）
    refresh();
    LOG_INFO("[TrayMenuBuilder] Retranslate done");
}
