#ifndef GLOBALSHORTCUT_H
#define GLOBALSHORTCUT_H

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <functional>
#include <QKeySequence>
#include <QHash>
#include <Qt>

#ifdef Q_OS_MAC
#include <Carbon/Carbon.h>
#endif

/**
 * @brief 全局快捷键类
 * 
 * 用于注册和管理全局快捷键，支持跨平台的快捷键监听
 * @author chiangyang
 */
class GlobalShortcut : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit GlobalShortcut(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     * @author chiangyang
     */
    virtual ~GlobalShortcut();

    /**
     * @brief 注册全局快捷键
     * @param keySequence 快捷键序列
     * @param callback 快捷键触发时的回调函数
     * @return 是否注册成功
     * @author chiangyang
     */
    bool registerShortcut(const QString &keySequence, std::function<void()> callback);
    
    /**
     * @brief 注销全局快捷键
     * @author chiangyang
     */
    void unregisterShortcut();
    
    /**
     * @brief 更新全局快捷键
     * @param newKeySequence 新的快捷键序列
     * @return 是否更新成功
     * @author chiangyang
     */
    bool updateShortcut(const QString &newKeySequence);

#ifdef Q_OS_MAC
    /**
     * @brief 触发快捷键回调（供 Carbon 事件回调调用，切回 Qt 事件循环执行）
     * @author chiangyang
     */
    void triggerCallback();

public:
    /**
     * @brief 设置 Fn 键回调（Carbon 方案不支持，保留接口兼容）
     * @param callback 回调函数
     * @author chiangyang
     */
    void setFnKeyCallback(std::function<void()> callback);
#endif

protected:
    /**
     * @brief 原生事件过滤器
     * @param eventType 事件类型
     * @param message 事件消息
     * @param result 处理结果
     * @return 是否处理了事件
     * @author chiangyang
     */
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    int m_hotkeyId;                     ///< 热键ID（Windows平台）
    std::function<void()> m_callback;   ///< 快捷键触发回调
    bool m_isRegistered;                ///< 是否已注册快捷键
    std::function<void()> m_fnKeyCallback; ///< Fn键回调（macOS平台）

#ifdef Q_OS_MAC
    int m_targetKeyCode;                ///< 目标按键键码（Carbon 虚拟键码）
    EventHotKeyRef m_carbonHotKeyRef;   ///< Carbon 全局热键引用
#endif

    /**
     * @brief 平台原生快捷键注册
     * @param modifiers 修饰键
     * @param key 按键
     * @return 是否注册成功
     * @author chiangyang
     */
    bool nativeRegister(Qt::KeyboardModifiers modifiers, Qt::Key key);

    /**
     * @brief 平台原生快捷键注销
     * @author chiangyang
     */
    void nativeUnregister();
};

#endif // GLOBALSHORTCUT_H
