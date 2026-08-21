#include "ShortcutManager.h"

#include <QKeySequence>
#include <QMenu>
#include <QSettings>

#include "TrayMenuBuilder.h"
#include "ShortcutTypes.h"
#include "ShortcutRegistry.h"
#include "../capture/SnipScreen.h"
#include "../widgets/HistoryWindow.h"
#include "../widgets/PinWindow.h"
#include "../core/ConfigManager.h"
#include "../log/Logger.h"

ShortcutManager* ShortcutManager::s_instance = nullptr;

/**
 * @brief 获取单例实例（懒汉式线程不安全单例，Qt GUI 主线程单线程场景足够）
 * @return 单例指针
 * @note 首次调用时才构造实例，避免静态初始化顺序问题（fiasco）
 * @author chiangyang
 */
ShortcutManager* ShortcutManager::instance() {
    if (!s_instance) {
        s_instance = new ShortcutManager();
    }
    return s_instance;
}

/**
 * @brief 构造函数
 * @param parent 父对象
 *
 * 初始化顺序：
 * 1. ShortcutRegistry（热键注册表，以 this 为父对象）
 * 2. TrayMenuBuilder 暂设为 nullptr，在 buildTrayMenu 首次调用时创建
 * 3. SnipScreen / HistoryWindow / SettingsWindow 三个弱引用暂设为 nullptr
 * 4. 向 Qt 元对象系统注册 ShortcutType 枚举，用于信号槽跨线程传递
 * @author chiangyang
 */
ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent)
    , m_registry(this)
    , m_trayBuilder(nullptr)
    , m_snipScreen(nullptr)
    , m_historyWindow(nullptr)
    , m_settingsWindow(nullptr) {
    qRegisterMetaType<ShortcutType>("ShortcutType");
    LOG_INFO("[ShortcutManager] Created (singleton)");
}

/**
 * @brief 析构函数
 *
 * 清理顺序：
 * 1. 调用 unregisterAll() 批量注销所有热键
 * 2. TrayMenuBuilder 是 ShortcutManager 的 QObject 子对象，
 *    由 Qt 父子关系自动释放，此处仅将指针置空避免悬空引用
 * 3. 清除单例指针，避免下次 instance() 返回野指针
 * @author chiangyang
 */
ShortcutManager::~ShortcutManager() {
    unregisterAll();
    m_trayBuilder = nullptr; // TrayMenuBuilder 由 QObject 父子关系自动释放
    if (s_instance == this) {
        s_instance = nullptr;
    }
    LOG_INFO("[ShortcutManager] Destroyed");
}

/**
 * @brief 注入外部依赖（SnipScreen、HistoryWindow）
 * @param snipScreen 截图主界面（弱引用，生命周期由 main.cpp 管理）
 * @param historyWindow 历史记录窗口（弱引用）
 *
 * 必须在 registerAll() / buildTrayMenu() 之前调用。
 * 设计上不使用智能指针以避免改变现有所有权模型；
 * 若对象被外部释放，所有回调在执行时都会做空指针检查。
 * @note 使用位置: main.cpp 初始化阶段、buildTrayMenu() 内部再次覆盖以保证一致
 * @author chiangyang
 */
void ShortcutManager::initialize(SnipScreen* snipScreen, HistoryWindow* historyWindow) {
    m_snipScreen = snipScreen;
    m_historyWindow = historyWindow;
    LOG_INFO("[ShortcutManager] Dependencies injected (snipScreen, historyWindow)");
}

/**
 * @brief 按数据表顺序批量注册所有全局热键（模板方法固定骨架）
 * @return 注册成功的数量，应等于 kShortcutConfigCount（9）
 *
 * 固定注册顺序（严格按照数据表用户认知顺序）：
 * Snip → Record → History → Pin → Fullscreen → ActiveWindow →
 * RecordPause → RecordStop → TogglePins
 * 每个回调内部都做空指针判断，保证弱引用对象释放后程序不崩溃。
 * @note 使用位置: main.cpp 启动阶段，需先调用 initialize() 注入依赖
 * @author chiangyang
 */
int ShortcutManager::registerAll() {
    if (!m_snipScreen) {
        LOG_ERROR("[ShortcutManager] registerAll called before initialize()");
        return 0;
    }
    int registered = 0;

    // 1. 截图
    if (m_registry.registerShortcut(ShortcutType::Snip, [this]() {
        if (m_snipScreen) m_snipScreen->start();
    })) registered++;

    // 2. 录屏
    if (m_registry.registerShortcut(ShortcutType::Record, [this]() {
        if (m_snipScreen) m_snipScreen->startRecording();
    })) registered++;

    // 3. 历史记录
    if (m_registry.registerShortcut(ShortcutType::History, [this]() {
        if (m_historyWindow) {
            m_historyWindow->show();
            m_historyWindow->raise();
            m_historyWindow->activateWindow();
        }
    })) registered++;

    // 4. 贴图剪贴板（Alt+P 历史截图翻页）
    if (m_registry.registerShortcut(ShortcutType::Pin, [this]() {
        if (m_snipScreen) m_snipScreen->pinClipboard();
    })) registered++;

    // 5. 全屏截图
    if (m_registry.registerShortcut(ShortcutType::Fullscreen, [this]() {
        if (m_snipScreen) m_snipScreen->grabFullscreen();
    })) registered++;

    // 6. 活动窗口截图
    if (m_registry.registerShortcut(ShortcutType::ActiveWindow, [this]() {
        if (m_snipScreen) m_snipScreen->grabActiveWindow();
    })) registered++;

    // 7. 录屏暂停/恢复
    if (m_registry.registerShortcut(ShortcutType::RecordPause, [this]() {
        if (m_snipScreen) m_snipScreen->togglePauseRecording();
    })) registered++;

    // 8. 录屏停止
    if (m_registry.registerShortcut(ShortcutType::RecordStop, [this]() {
        if (m_snipScreen) m_snipScreen->stopRecording();
    })) registered++;

    // 9. 隐藏/显示所有贴图
    if (m_registry.registerShortcut(ShortcutType::TogglePins, []() {
        PinWindow::toggleAll();
    })) registered++;

    LOG_INFO(QString("[ShortcutManager] registerAll: %1/%2 registered")
                 .arg(registered).arg(kShortcutConfigCount));
    return registered;
}

/**
 * @brief 批量注销所有已注册热键
 *
 * 直接委托 ShortcutRegistry::unregisterAll() 完成，保持外观模式的精简。
 * @note 使用位置: 析构函数、程序退出时
 * @author chiangyang
 */
void ShortcutManager::unregisterAll() {
    m_registry.unregisterAll();
}

/**
 * @brief 注册单个快捷键（调用方自定义回调，用于数据表之外的场景）
 * @param type 快捷键类型
 * @param callback 触发回调（通过右值引用转移所有权，避免多余拷贝）
 * @return 是否注册成功
 * @note 一般场景优先使用 registerAll() 批量注册，此接口仅用于扩展或热替换
 * @author chiangyang
 */
bool ShortcutManager::registerShortcut(ShortcutType type, std::function<void()> callback) {
    return m_registry.registerShortcut(type, std::move(callback));
}

/**
 * @brief 构建托盘菜单（内部委托 TrayMenuBuilder，工厂方法 + 数据驱动）
 * @param menu 目标托盘菜单（必须已创建，调用方负责其生命周期）
 * @param snipScreen 截图主界面（绑定 action 触发逻辑）
 * @param historyWindow 历史记录窗口（绑定 action 触发逻辑）
 * @param settingsWindow 设置窗口（可传 nullptr，后续通过 setSettingsWindow 补充）
 *
 * 首次调用时创建 TrayMenuBuilder 实例（以 this 为父对象保证生命周期），
 * 并将 shortcutChanged 信号自动连接到 TrayMenuBuilder::refresh 槽，
 * 实现观察者模式的自动刷新。
 * @note 使用位置: main.cpp 创建托盘菜单阶段
 * @author chiangyang
 */
void ShortcutManager::buildTrayMenu(QMenu* menu, SnipScreen* snipScreen,
                                    HistoryWindow* historyWindow, QWidget* settingsWindow) {
    m_snipScreen = snipScreen;
    m_historyWindow = historyWindow;
    m_settingsWindow = settingsWindow;

    if (!m_trayBuilder) {
        m_trayBuilder = new TrayMenuBuilder(this);
        // TrayMenuBuilder::refresh 有重载（无参版 + 带 ShortcutType/QKeySequence 版），
        // 需通过 qOverload 明确指定匹配 shortcutChanged(ShortcutType, QKeySequence) 信号的槽
        QObject::connect(this, &ShortcutManager::shortcutChanged,
                         m_trayBuilder, qOverload<ShortcutType, const QKeySequence&>(&TrayMenuBuilder::refresh));
    }
    m_trayBuilder->build(menu, snipScreen, historyWindow, settingsWindow);
    LOG_INFO("[ShortcutManager] Tray menu built");
}

/**
 * @brief 手动刷新托盘菜单快捷键显示值
 *
 * 若 TrayMenuBuilder 尚未创建则直接返回。
 * 一般无需手动调用，shortcutChanged 信号已自动驱动刷新。
 * @note 使用场景：配置文件被外部修改后强制刷新菜单显示
 * @author chiangyang
 */
void ShortcutManager::refreshTrayMenu() {
    if (m_trayBuilder) {
        m_trayBuilder->refresh();
    }
}

/**
 * @brief 重新翻译托盘菜单文本（语言切换后调用）
 *
 * 若 TrayMenuBuilder 尚未创建则直接返回。
 * @note 使用位置: main.cpp 监听 TranslationManager::languageChanged 时调用
 * @author chiangyang
 */
void ShortcutManager::retranslateTrayMenu() {
    if (m_trayBuilder) {
        m_trayBuilder->retranslate();
    }
}

/**
 * @brief 更新某个快捷键（枚举参数版）
 * @param type 快捷键类型
 * @param sequence 新的快捷键序列
 * @return 是否更新成功
 *
 * 更新成功后发射 shortcutChanged(type, sequence) 信号，
 * 驱动所有观察者（TrayMenuBuilder、日志、未来的扩展）增量刷新。
 * @note 此为内部接口，外部一般通过 SettingsWindow 间接调用
 * @author chiangyang
 */
bool ShortcutManager::update(ShortcutType type, const QKeySequence& sequence) {
    bool ok = m_registry.update(type, sequence);
    if (ok) {
        emit shortcutChanged(type, sequence);
    }
    LOG_INFO(QString("[ShortcutManager] update type=%1 seq=%2 ok=%3")
                 .arg(shortcutTypeToString(type)).arg(sequence.toString()).arg(ok));
    return ok;
}

/**
 * @brief SettingsWindow 字符串入口（兼容 SettingsWindow 旧信号参数）
 * @param typeStr 类型字符串（如 "snip"、"record"、"history"）
 * @param sequence 新的快捷键序列
 * @return 是否更新成功
 *
 * 在迁移期间 SettingsWindow 仍发出 (QString, QKeySequence) 信号，
 * 通过此适配器转换为枚举版 update，无需逐个修改 SettingsWindow 的
 * 9 处 emit 代码，实现平滑迁移。
 * @note 使用位置: SettingsWindow::shortcutChanged 信号连接到此处
 * @author chiangyang
 */
bool ShortcutManager::updateFromUiString(const QString& typeStr, const QKeySequence& sequence) {
    auto typeOpt = shortcutTypeFromString(typeStr);
    if (!typeOpt.has_value()) {
        LOG_WARNING(QString("[ShortcutManager] updateFromUiString unknown typeStr: %1").arg(typeStr));
        return false;
    }
    return update(*typeOpt, sequence);
}

/**
 * @brief 重置某个快捷键到数据表中的默认值
 * @param type 快捷键类型
 * @return 重置后的实际快捷键序列；若重置失败则返回当前仍有效的序列
 *
 * 读取 ShortcutConfigItem 的 defaultValue 构造 QKeySequence，
 * 通过 update() 写入配置并刷新注册表。失败（如默认值与系统冲突）
 * 时保持原值返回，调用方可据此提示用户。
 * @author chiangyang
 */
QKeySequence ShortcutManager::reset(ShortcutType type) {
    const ShortcutConfigItem* cfg = getShortcutConfig(type);
    if (!cfg) return {};
    QKeySequence def(cfg->defaultValue);
    bool ok = update(type, def);
    return ok ? def : getSequence(type);
}

/**
 * @brief 获取某个快捷键当前使用的序列
 * @param type 快捷键类型
 * @return 当前有效序列，失败或未知类型返回空 QKeySequence
 *
 * 序列来源：优先读取 QSettings 中用户配置的值；
 * 不存在则回退到 ShortcutConfigItem.defaultValue；
 * History 类型额外回退到旧配置键 history/shortcut。
 * @author chiangyang
 */
QKeySequence ShortcutManager::getSequence(ShortcutType type) const {
    const ShortcutConfigItem* cfg = getShortcutConfig(type);
    if (!cfg) return {};
    return getShortcutSequence(*cfg);
}

/**
 * @brief 获取全部配置项的引用列表（SettingsWindow 选项卡 UI 构建使用）
 * @return 配置项指针列表（浅拷贝，顺序严格与数据表定义一致）
 *
 * SettingsWindow 构建"全局快捷键"选项卡时通过此方法一次性获取
 * 所有配置，替代原先分散的 9 组 value() 读取代码。
 * @note 返回值不拥有数据，生命周期等同于进程。
 * @author chiangyang
 */
QList<const ShortcutConfigItem*> ShortcutManager::getAllConfigs() const {
    return ::getAllShortcutConfigs();
}

/**
 * @brief 类型字符串转枚举（对外工具方法，包装 ShortcutTypes.h 同名自由函数）
 * @param typeStr 类型字符串
 * @return 枚举值；解析失败返回 std::nullopt
 * @note 作为 ShortcutManager 的静态成员，方便调用方统一使用单例命名空间
 * @author chiangyang
 */
std::optional<ShortcutType> ShortcutManager::typeFromString(const QString& typeStr) {
    return ::shortcutTypeFromString(typeStr);
}
