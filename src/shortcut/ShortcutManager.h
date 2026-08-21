#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <optional>
#include <functional>

#include "ShortcutTypes.h"
#include "ShortcutRegistry.h"

class QMenu;
class TrayMenuBuilder;
class SnipScreen;
class HistoryWindow;

/**
 * @brief 快捷键管理器（单例 + 外观模式）
 *
 * 快捷键子系统的全局唯一入口，对外提供简洁统一 API，
 * 内部协调 ShortcutRegistry（注册表）、TrayMenuBuilder（托盘菜单构建器）
 * 和 ShortcutConfigItem（配置数据表）三个子组件。
 *
 * 典型用法（main.cpp）：
 * @code
 *   ShortcutManager::instance()->initialize(snipScreen, historyWindow);
 *   ShortcutManager::instance()->registerAll();
 *   ShortcutManager::instance()->buildTrayMenu(trayMenu, snipScreen,
 *                                               historyWindow, settingsWindow);
 * @endcode
 *
 * 设置界面（SettingsWindow）更新快捷键：
 * @code
 *   ShortcutManager::instance()->updateFromUiString("snip", "Alt+T");
 * @endcode
 *
 * @author chiangyang
 */
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    /**
     * @brief 获取单例实例（懒汉式单例，Qt GUI 主线程单线程场景足够）
     * @return 单例指针
     * @note 首次调用才构造实例，避免静态初始化顺序 fiasco 问题
     * @author chiangyang
     */
    static ShortcutManager* instance();

    /**
     * @brief 构造函数
     * @param parent 父对象
     *
     * 初始化 ShortcutRegistry（以 this 为父对象），向 Qt 元对象系统注册
     * ShortcutType 枚举用于信号槽跨线程传递。TrayMenuBuilder 首次 buildTrayMenu()
     * 时再构造，减少启动开销。
     * @author chiangyang
     */
    explicit ShortcutManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数，调用 unregisterAll() 批量注销热键并清理单例指针
     * @author chiangyang
     */
    ~ShortcutManager() override;

    /**
     * @brief 注入外部依赖（SnipScreen、HistoryWindow）
     * @param snipScreen 截图主界面（用于触发截图/录屏）
     * @param historyWindow 历史记录窗口（用于显示历史记录）
     *
     * 必须在 registerAll() / buildTrayMenu() 之前调用。
     * 外部对象生命周期由 main.cpp 管理，本类仅持有弱引用（原始指针）。
     * @note 使用位置: main.cpp 初始化阶段、buildTrayMenu() 内部再次覆盖保持一致
     * @author chiangyang
     */
    void initialize(SnipScreen* snipScreen, HistoryWindow* historyWindow);

    /**
     * @brief 按数据表顺序批量注册所有全局热键（模板方法固定骨架）
     * @return 注册成功数量（应等于 kShortcutConfigCount=9）
     *
     * 固定顺序（严格按照数据表用户认知顺序）：
     * 截图→录屏→历史→贴图→全屏→活动窗口→录屏暂停→录屏停止→TogglePins
     * 顺序遵循用户认知，日志输出直观。
     * @note 使用位置: main.cpp 启动阶段，需先调用 initialize() 注入依赖
     * @author chiangyang
     */
    int registerAll();

    /**
     * @brief 批量注销所有已注册热键
     * @note 直接委托 ShortcutRegistry::unregisterAll()
     * @author chiangyang
     */
    void unregisterAll();

    /**
     * @brief 注册单个快捷键（调用方传入回调，用于数据表之外的扩展场景）
     * @param type 快捷键类型
     * @param callback 触发回调
     * @return 是否注册成功
     * @note 一般场景优先使用 registerAll() 批量注册，此接口仅用于扩展或热替换
     * @author chiangyang
     */
    bool registerShortcut(ShortcutType type, std::function<void()> callback);

    /**
     * @brief 构建托盘菜单（数据驱动，内部委托 TrayMenuBuilder，工厂方法模式）
     * @param menu 目标托盘菜单（必须已创建，生命周期由 main.cpp 管理）
     * @param snipScreen 截图主界面（用于绑定 action 触发逻辑）
     * @param historyWindow 历史记录窗口（用于绑定 action 触发逻辑）
     * @param settingsWindow 设置窗口（可传入 nullptr 后通过 setSettingsWindow 补充）
     *
     * 首次调用时创建 TrayMenuBuilder 实例并自动 connect shortcutChanged 信号
     * 到 TrayMenuBuilder::refresh 槽，实现观察者模式自动刷新。
     * @note 使用位置: main.cpp 创建托盘菜单阶段
     * @author chiangyang
     */
    void buildTrayMenu(QMenu* menu, SnipScreen* snipScreen,
                       HistoryWindow* historyWindow, QWidget* settingsWindow);

    /**
     * @brief 刷新托盘菜单上显示的快捷键值
     * @note 一般无需手动调用，shortcutChanged 信号已自动驱动；配置被外部修改时可强制刷新
     * @author chiangyang
     */
    void refreshTrayMenu();

    /**
     * @brief 重新翻译托盘菜单文本（语言切换后调用）
     * @note 使用位置: main.cpp 监听 TranslationManager::languageChanged 时调用
     * @author chiangyang
     */
    void retranslateTrayMenu();

    /**
     * @brief 更新某个快捷键（SettingsWindow 通知入口，枚举版）
     * @param type 快捷键类型
     * @param sequence 新的快捷键序列
     * @return 是否更新成功
     *
     * 成功后发射 shortcutChanged(type, sequence) 信号，
     * 驱动所有观察者（TrayMenuBuilder、日志、未来扩展）增量刷新。
     * @author chiangyang
     */
    bool update(ShortcutType type, const QKeySequence& sequence);

    /**
     * @brief SettingsWindow 字符串入口（兼容 SettingsWindow 旧信号参数的适配器）
     * @param typeStr 类型字符串（如 "snip"、"record"）
     * @param sequence 新的快捷键序列
     * @return 是否更新成功
     *
     * 在迁移期间 SettingsWindow 仍发出 (QString, QKeySequence) 信号，
     * 此适配器转换为枚举版 update，无需逐个修改 SettingsWindow 的
     * 9 处 emit 代码，实现平滑迁移。
     * @note 使用位置: SettingsWindow::shortcutChanged 信号连接
     * @author chiangyang
     */
    bool updateFromUiString(const QString& typeStr, const QKeySequence& sequence);

    /**
     * @brief 重置某个快捷键到数据表中的默认值
     * @param type 快捷键类型
     * @return 重置后的实际快捷键序列；若重置失败则返回当前仍有效的序列
     * @author chiangyang
     */
    QKeySequence reset(ShortcutType type);

    /**
     * @brief 获取某个快捷键当前使用的序列
     * @param type 快捷键类型
     * @return 当前有效序列；未知类型返回空 QKeySequence
     *
     * 序列来源优先级：QSettings 用户配置 → ShortcutConfigItem.defaultValue →
     * History 类型额外回退到旧配置键 history/shortcut。
     * @author chiangyang
     */
    QKeySequence getSequence(ShortcutType type) const;

    /**
     * @brief 获取全部配置项的引用列表（SettingsWindow 选项卡 UI 构建使用）
     * @return 配置项指针列表（浅拷贝，顺序严格与数据表定义一致）
     * @note 返回值不拥有数据，生命周期等同于进程
     * @author chiangyang
     */
    QList<const ShortcutConfigItem*> getAllConfigs() const;

    /**
     * @brief 类型字符串转枚举（对外工具方法，包装 ShortcutTypes.h 同名自由函数）
     * @param typeStr 类型字符串，如 "snip"
     * @return 枚举值；解析失败返回 std::nullopt
     * @note 作为 ShortcutManager 的静态成员，方便调用方统一使用单例命名空间
     * @author chiangyang
     */
    static std::optional<ShortcutType> typeFromString(const QString& typeStr);

signals:
    /**
     * @brief 快捷键变更信号（枚举参数）
     * @param type 类型
     * @param sequence 新的键位序列
     *
     * TrayMenuBuilder 和其他观察者监听此信号，
     * 替代原先 SettingsWindow 直接连接 9 个 GlobalShortcut 的长串 lambda。
     * @author chiangyang
     */
    void shortcutChanged(ShortcutType type, const QKeySequence& sequence);

private:
    static ShortcutManager* s_instance;             ///< 单例实例

    ShortcutRegistry m_registry;                    ///< 全局热键注册表（组合）
    TrayMenuBuilder* m_trayBuilder;                 ///< 托盘菜单构建器（组合，使用 QObject 父子生命周期）

    SnipScreen* m_snipScreen;                       ///< 截图主界面弱引用
    HistoryWindow* m_historyWindow;                 ///< 历史记录窗口弱引用
    QWidget* m_settingsWindow;                      ///< 设置窗口弱引用
};

#endif // SHORTCUTMANAGER_H
