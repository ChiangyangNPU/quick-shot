#ifndef SHORTCUTREGISTRY_H
#define SHORTCUTREGISTRY_H

#include <QObject>
#include <QHash>
#include <QKeySequence>
#include <functional>

#include "GlobalShortcut.h"
#include "ShortcutTypes.h"
#include "../log/Logger.h"

/**
 * @brief 全局热键注册表（注册表模式）
 *
 * 集中管理所有 GlobalShortcut 实例的生命周期、注册、更新、注销。
 *
 * 替代原先 main.cpp 中 9 个散落的 GlobalShortcut 局部变量，
 * 通过 ShortcutType 枚举作为统一索引，避免硬编码重复代码与散列管理。
 *
 * 使用方式：
 * @code
 *   ShortcutRegistry reg;
 *   reg.registerShortcut(ShortcutType::Snip, []() { ... });
 *   reg.update(ShortcutType::Snip, "Alt+T");
 * @endcode
 *
 * @author chiangyang
 */
class ShortcutRegistry : public QObject {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit ShortcutRegistry(QObject* parent = nullptr);

    /**
     * @brief 析构函数，调用 unregisterAll() 批量注销所有热键
     * @author chiangyang
     */
    ~ShortcutRegistry() override;

    /**
     * @brief 注册一个全局快捷键并绑定回调
     * @param type 快捷键类型
     * @param callback 触发时调用的回调函数
     * @return 是否注册成功
     * @note 若该类型已注册，则先注销旧的再注册新的；使用位置: ShortcutManager::registerAll()
     * @author chiangyang
     */
    bool registerShortcut(ShortcutType type, std::function<void()> callback);

    /**
     * @brief 更新某个快捷键的键位（保留原有回调）
     * @param type 快捷键类型
     * @param sequence 新的快捷键序列
     * @return 是否更新成功
     * @note 同时将新序列写回 QSettings 持久化；信号由上层 ShortcutManager::update() 统一发射
     * @author chiangyang
     */
    bool update(ShortcutType type, const QKeySequence& sequence);

    /**
     * @brief 注销某个快捷键
     * @param type 快捷键类型
     * @note 使用位置: registerShortcut() 重注册时、析构函数、unregisterAll()
     * @author chiangyang
     */
    void unregisterShortcut(ShortcutType type);

    /**
     * @brief 批量注销所有已注册快捷键
     * @note 使用位置: 析构函数、ShortcutManager 程序退出前
     * @author chiangyang
     */
    void unregisterAll();

    /**
     * @brief 获取指定类型的 GlobalShortcut 实例
     * @param type 快捷键类型
     * @return 实例指针，未注册返回 nullptr
     * @note 仅用于调试或底层操作，外部优先使用 ShortcutManager 高层 API
     * @author chiangyang
     */
    GlobalShortcut* get(ShortcutType type) const;

    /**
     * @brief 查询指定类型是否已注册
     * @param type 快捷键类型
     * @return 是否已注册
     * @author chiangyang
     */
    bool isRegistered(ShortcutType type) const;

    /**
     * @brief 获取某类型的触发回调
     * @param type 快捷键类型
     * @return 回调函数，不存在则返回空 function
     * @note 主要用于 update() 内部校验回调是否已设置，外部一般不需直接触发
     * @author chiangyang
     */
    std::function<void()> getCallback(ShortcutType type) const;

private:
    QHash<ShortcutType, GlobalShortcut*> m_shortcuts;   ///< 类型到实例映射
    QHash<ShortcutType, std::function<void()>> m_callbacks; ///< 类型到回调映射
};

#endif // SHORTCUTREGISTRY_H
