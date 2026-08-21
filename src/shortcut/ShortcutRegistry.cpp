#include "ShortcutRegistry.h"
#include "../core/ConfigManager.h"

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
ShortcutRegistry::ShortcutRegistry(QObject* parent)
    : QObject(parent) {
    LOG_INFO("[ShortcutRegistry] Created");
}

/**
 * @brief 析构函数
 *
 * 调用 unregisterAll() 批量注销所有已注册的全局热键，
 * 确保 GlobalShortcut 实例被正确释放，避免热键泄露。
 * @author chiangyang
 */
ShortcutRegistry::~ShortcutRegistry() {
    unregisterAll();
    LOG_INFO("[ShortcutRegistry] Destroyed");
}

/**
 * @brief 注册一个全局快捷键并绑定回调
 * @param type 快捷键类型（ShortcutType 枚举）
 * @param callback 快捷键触发时调用的回调函数
 * @return 是否注册成功
 *
 * 若该类型已注册过，则先注销旧的再注册新的，保证幂等性。
 * 注册流程：从 ShortcutConfigItem 数据表读取类型配置 → 从配置文件读取
 * 实际键位序列 → 创建 GlobalShortcut 实例并注册 → 存入两张 Hash 表。
 * @note 使用位置: ShortcutManager::registerAll() 遍历 9 种类型批量调用
 * @author chiangyang
 */
bool ShortcutRegistry::registerShortcut(ShortcutType type, std::function<void()> callback) {
    // 如已注册，先注销旧的
    if (m_shortcuts.contains(type)) {
        unregisterShortcut(type);
    }

    const ShortcutConfigItem* cfg = getShortcutConfig(type);
    if (!cfg) {
        LOG_ERROR(QString("[ShortcutRegistry] Unknown shortcut type: %1")
                      .arg(static_cast<int>(type)));
        return false;
    }

    QKeySequence seq = getShortcutSequence(*cfg);
    if (seq.isEmpty()) {
        LOG_ERROR(QString("[ShortcutRegistry] Empty sequence for type: %1")
                      .arg(shortcutTypeToString(type)));
        return false;
    }

    GlobalShortcut* gs = new GlobalShortcut(this);
    if (!gs->registerShortcut(seq.toString(), callback)) {
        LOG_ERROR(QString("[ShortcutRegistry] Failed to register: type=%1 seq=%2")
                      .arg(shortcutTypeToString(type)).arg(seq.toString()));
        delete gs;
        return false;
    }

    m_shortcuts.insert(type, gs);
    m_callbacks.insert(type, callback);
    LOG_INFO(QString("[ShortcutRegistry] Registered: type=%1 seq=%2")
                 .arg(shortcutTypeToString(type)).arg(seq.toString()));
    return true;
}

/**
 * @brief 更新某个快捷键的键位（保留原有回调）
 * @param type 快捷键类型
 * @param sequence 新的快捷键序列（QKeySequence）
 * @return 是否更新成功
 *
 * 更新逻辑：
 * 1. 若类型未注册，直接返回失败（避免写入配置后无法应用）
 * 2. 将新的序列写回 QSettings 配置文件并 sync 持久化
 * 3. 调用 GlobalShortcut::updateShortcut() 重新注册 OS 级热键
 * @note 不负责发射信号，信号由上层 ShortcutManager::update() 统一发射，
 *       保持 Registry 与观察者解耦
 * @author chiangyang
 */
bool ShortcutRegistry::update(ShortcutType type, const QKeySequence& sequence) {
    auto it = m_shortcuts.find(type);
    if (it == m_shortcuts.end()) {
        LOG_WARNING(QString("[ShortcutRegistry] update on unregistered type: %1")
                        .arg(shortcutTypeToString(type)));
        return false;
    }

    GlobalShortcut* gs = it.value();
    std::function<void()> cb = m_callbacks.value(type);

    // 使用新序列重新注册
    // 为保持回调与实例的一致性，先通过临时序列注册，成功后替换
    // updateShortcut 内部会调用 registerShortcut(cb 复用 m_callback 存的)
    if (!cb) {
        LOG_ERROR(QString("[ShortcutRegistry] update missing callback: %1")
                      .arg(shortcutTypeToString(type)));
        return false;
    }

    // 先写回配置
    const ShortcutConfigItem* cfg = getShortcutConfig(type);
    if (cfg) {
        QSettings* settings = ConfigManager::instance()->getSettings();
        settings->setValue(cfg->configKey, sequence.toString());
        // 向后兼容：历史记录快捷键同时写旧键 history/shortcut，
        // 确保用户降级回旧版本时配置不丢失
        if (type == ShortcutType::History) {
            settings->setValue("history/shortcut", sequence.toString());
        }
        settings->sync();
    }

    bool ok = gs->updateShortcut(sequence.toString());
    LOG_INFO(QString("[ShortcutRegistry] update %1: seq=%2 %3")
                 .arg(shortcutTypeToString(type))
                 .arg(sequence.toString())
                 .arg(ok ? "success" : "failed"));
    return ok;
}

/**
 * @brief 注销某个指定类型的快捷键
 * @param type 快捷键类型
 *
 * 若类型不存在则直接返回（幂等）。
 * 操作顺序：调用 GlobalShortcut::unregisterShortcut() 释放 OS 级热键 →
 * delete 实例 → 从两张 Hash 表移除条目。
 * @note 使用位置: registerShortcut() 重注册时、析构函数、unregisterAll()
 * @author chiangyang
 */
void ShortcutRegistry::unregisterShortcut(ShortcutType type) {
    auto it = m_shortcuts.find(type);
    if (it == m_shortcuts.end()) return;

    GlobalShortcut* gs = it.value();
    gs->unregisterShortcut();
    delete gs;
    m_shortcuts.erase(it);
    m_callbacks.remove(type);
    LOG_INFO(QString("[ShortcutRegistry] Unregistered: type=%1")
                 .arg(shortcutTypeToString(type)));
}

/**
 * @brief 批量注销所有已注册快捷键
 *
 * 遍历 m_shortcuts 表逐个注销并释放实例，最后清空两张 Hash 表。
 * 空表时直接返回，避免无意义操作和日志噪音。
 * @note 使用位置: 析构函数、ShortcutManager 程序退出前
 * @author chiangyang
 */
void ShortcutRegistry::unregisterAll() {
    if (m_shortcuts.isEmpty()) return;
    for (GlobalShortcut* gs : m_shortcuts) {
        gs->unregisterShortcut();
        delete gs;
    }
    int count = m_shortcuts.size();
    m_shortcuts.clear();
    m_callbacks.clear();
    LOG_INFO(QString("[ShortcutRegistry] Unregistered all (%1 items)").arg(count));
}

/**
 * @brief 获取指定类型的 GlobalShortcut 实例
 * @param type 快捷键类型
 * @return GlobalShortcut 实例指针；未注册时返回 nullptr
 * @note 仅用于调试或需要底层操作的场景，外部调用方应优先使用
 *       ShortcutManager 提供的高层 API
 * @author chiangyang
 */
GlobalShortcut* ShortcutRegistry::get(ShortcutType type) const {
    return m_shortcuts.value(type, nullptr);
}

/**
 * @brief 查询指定类型是否已注册
 * @param type 快捷键类型
 * @return 是否已注册（Hash 表中存在该类型条目）
 * @note 用于判断某个快捷键当前是否处于激活状态
 * @author chiangyang
 */
bool ShortcutRegistry::isRegistered(ShortcutType type) const {
    return m_shortcuts.contains(type);
}

/**
 * @brief 获取某类型的触发回调
 * @param type 快捷键类型
 * @return 回调函数（std::function<void()>）；不存在则返回空 function
 * @note 主要用于 update() 等内部场景校验回调是否已设置，
 *       外部一般不需要直接触发回调
 * @author chiangyang
 */
std::function<void()> ShortcutRegistry::getCallback(ShortcutType type) const {
    return m_callbacks.value(type);
}
