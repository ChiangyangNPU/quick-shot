#include "GlobalShortcut.h"
#include "ShortcutKeyMap.h"
#include <QCoreApplication>
#include <atomic>
#include "../log/Logger.h"
#include "../core/TranslationManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GlobalShortcut::GlobalShortcut(QObject *parent)
    : QObject(parent), m_hotkeyId(0), m_isRegistered(false)
#ifdef Q_OS_MAC
    , m_targetKeyCode(0), m_carbonHotKeyRef(nullptr)
#endif
{
    static std::atomic<int> s_nextId{100};
    m_hotkeyId = s_nextId.fetch_add(1);
    QCoreApplication::instance()->installNativeEventFilter(this);
    LOG_INFO(QString("GlobalShortcut instance created, ID: %1")
             .arg(m_hotkeyId));
}

GlobalShortcut::~GlobalShortcut() {
    unregisterShortcut();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalShortcut::registerShortcut(const QString &keySequence, std::function<void()> callback) {
    if (m_isRegistered) {
        unregisterShortcut();
    }

    QKeySequence ks(keySequence);
    if (ks.isEmpty()) {
        LOG_ERROR(QString("快捷键序列为空，ID: %1")
                  .arg(m_hotkeyId));
        return false;
    }

    QKeyCombination keyCombo = ks[0];
    Qt::KeyboardModifiers modifiers = keyCombo.keyboardModifiers();
    Qt::Key key = keyCombo.key();
    
    LOG_INFO(QString("Qt原始键值: %1 (Qt::Key_A=%2)")
             .arg(key).arg(Qt::Key_A));

    m_callback = callback;
    bool result = nativeRegister(modifiers, key);
    return result;
}

void GlobalShortcut::unregisterShortcut() {
    if (m_isRegistered) {
        nativeUnregister();
        m_isRegistered = false;
    }
}

bool GlobalShortcut::updateShortcut(const QString &newKeySequence) {
    LOG_INFO(QString("更新快捷键为: %1, ID: %2")
             .arg(newKeySequence).arg(m_hotkeyId));
    if (!m_callback) {
        LOG_ERROR(QString("无回调函数，无法更新快捷键，ID: %1")
                  .arg(m_hotkeyId));
        return false;
    }
    bool result = registerShortcut(newKeySequence, m_callback);
    LOG_INFO(QString("快捷键更新 %1, ID: %2")
             .arg(result ? "成功" : "失败").arg(m_hotkeyId));
    return result;
}

#ifdef Q_OS_WIN
/**
 * @brief 平台特定的注册方法（Windows）
 * @param modifiers 键盘修饰键
 * @param key 按键
 * @return 是否注册成功
 * @author chiangyang
 */
bool GlobalShortcut::nativeRegister(Qt::KeyboardModifiers modifiers, Qt::Key key) {
    UINT fsModifiers = 0;
    if (modifiers & Qt::AltModifier) fsModifiers |= MOD_ALT;
    if (modifiers & Qt::ControlModifier) fsModifiers |= MOD_CONTROL;
    if (modifiers & Qt::ShiftModifier) fsModifiers |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) fsModifiers |= MOD_WIN;

    // 将Qt::Key转换为Windows虚拟键码（映射表在 ShortcutKeyMap，可独立单测）
    const int vk = ShortcutKeyMap::qtKeyToVirtualKeyCode(key);
    if (vk < 0) {
        LOG_ERROR(QString("不支持的键: %1, ID: %2")
                  .arg(key).arg(m_hotkeyId));
        LOG_WARNING(QString("Unsupported key for global shortcut: %1").arg(key));
        return false;
    }

    // RegisterHotKey(HWND, ID, Modifiers, VK)
    // HWND可以为NULL，以便在消息循环线程中接收WM_HOTKEY
    BOOL result = RegisterHotKey(NULL, m_hotkeyId, fsModifiers | MOD_NOREPEAT, static_cast<UINT>(vk));
    if (result) {
        m_isRegistered = true;
    } else {
        DWORD error = GetLastError();
        LOG_ERROR(QString("RegisterHotKey 失败，错误: %1, ID: %2")
                  .arg(error).arg(m_hotkeyId));
        LOG_WARNING(QString("Failed to register hotkey. Error: %1").arg(error));
    }
    return result;
}

/**
 * @brief 平台特定的注销方法（Windows）
 * @author chiangyang
 */
void GlobalShortcut::nativeUnregister() {
    UnregisterHotKey(NULL, m_hotkeyId);
}

/**
 * @brief 重写原生事件过滤器
 * @param eventType 事件类型
 * @param message 消息指针
 * @param result 结果指针
 * @return 是否处理了事件
 * @author chiangyang
 */
bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            if (msg->wParam == m_hotkeyId) {
                if (m_callback) {
                    m_callback();
                }
                return true;
            }
        }
    }
    return false;
}

#elif defined(Q_OS_MAC)

/**
 * @brief Carbon 全局热键实现：通过 RegisterEventHotKey 注册系统级快捷键
 *
 * 相比旧 CGEventTap 方案的优势：
 * 1. 无需"辅助功能"权限即可生效，适合 DMG 分发后开箱即用的场景
 * 2. 由系统直接派发按键事件，不监听整个按键流，资源占用更低
 */

/// Carbon 应用级键盘事件处理器（全局只安装一次，由 ensureCarbonHandlerInstalled 创建）
static EventHandlerRef g_carbonHandler = nullptr;
/// 热键 ID -> GlobalShortcut 实例映射，用于 Carbon 回调派发
static QHash<uint32_t, GlobalShortcut*> g_carbonHotKeyMap;
/// Carbon EventHotKeyID 的签名常量（四字符码，区分于其他注册方的热键）
static constexpr OSType kHotKeySignature = 'QSH1';

/**
 * @brief Carbon 键盘事件回调：处理 kEventHotKeyPressed 事件
 *
 * 当用户按下已通过 RegisterEventHotKey 注册的组合键时被 Carbon 事件循环调用。
 * 通过 EventHotKeyID.id 在 g_carbonHotKeyMap 中找到对应 GlobalShortcut，并通过
 * QueuedConnection 切回 Qt 主线程再触发回调，避免在 Carbon 回调线程内执行可能
 * 引起重入或 UI 操作的逻辑。
 *
 * @param nextHandler 下一个事件处理器（未使用）
 * @param event       Carbon 事件引用
 * @param userData    用户数据（未使用，改用 g_carbonHotKeyMap 查表）
 * @return OSStatus   noErr 表示已处理，eventNotHandledErr 表示未匹配
 * @author chiangyang
 */
static OSStatus carbonHotKeyHandler(EventHandlerCallRef /*nextHandler*/,
                                    EventRef event,
                                    void* /*userData*/) {
    EventHotKeyID hotKeyID;
    OSStatus status = GetEventParameter(event, kEventParamDirectObject,
                                         typeEventHotKeyID, nullptr,
                                         sizeof(hotKeyID), nullptr, &hotKeyID);
    if (status != noErr) return status;
    if (hotKeyID.signature != kHotKeySignature) return eventNotHandledErr;

    GlobalShortcut *shortcut = g_carbonHotKeyMap.value(hotKeyID.id, nullptr);
    if (!shortcut) return eventNotHandledErr;
    GlobalShortcut *s = shortcut;
    QMetaObject::invokeMethod(s, [s]() { s->triggerCallback(); }, Qt::QueuedConnection);
    return noErr;
}

/**
 * @brief 确保 Carbon 应用级事件处理器已安装（全局只安装一次）
 *
 * 首次调用时通过 InstallApplicationEventHandler 注册 kEventHotKeyPressed 事件的回调
 * carbonHotKeyHandler。后续所有 RegisterEventHotKey 注册的热键触发都走这条回调分发。
 * 如果 InstallApplicationEventHandler 失败（极端情况下）则写错误日志，后续调用
 * nativeRegister 会检测 g_carbonHandler 为空并直接返回失败，避免无声失败。
 *
 * @author chiangyang
 */
static void ensureCarbonHandlerInstalled() {
    if (g_carbonHandler != nullptr) return;
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;
    OSStatus status = InstallApplicationEventHandler(
        NewEventHandlerUPP(carbonHotKeyHandler),
        1, &eventType,
        nullptr,
        &g_carbonHandler
    );
    if (status != noErr) {
        LOG_ERROR(QString("InstallApplicationEventHandler failed: %1").arg(status));
    }
}

/**
 * @brief 平台特定的注册方法（macOS / Carbon）
 *
 * 通过 Carbon RegisterEventHotKey 注册系统级全局热键，无需辅助功能权限。
 * 流程：Qt Key → kVK_ANSI_* 键码映射 → Qt Modifier → Carbon 修饰键映射
 *        → 安装应用级事件处理器（首次） → RegisterEventHotKey 注册
 *        → 保存 EventHotKeyRef 并登记到 g_carbonHotKeyMap 供回调查表。
 *
 * @param modifiers 键盘修饰键（Qt::KeyboardModifiers）
 * @param key 按键（Qt::Key）
 * @return 是否注册成功
 * @author chiangyang
 */
bool GlobalShortcut::nativeRegister(Qt::KeyboardModifiers modifiers, Qt::Key key) {
    m_targetKeyCode = 0;

    // Qt::Key → Carbon 虚拟键码（映射表在 ShortcutKeyMap，可独立单测；
    // kVK_F* 宏不连续，F 键已改显式查表修复旧算术映射的错键问题）
    const int vk = ShortcutKeyMap::qtKeyToVirtualKeyCode(key);
    if (vk < 0) {
        LOG_ERROR(QString("不支持的键: %1, ID: %2").arg(key).arg(m_hotkeyId));
        return false;
    }
    m_targetKeyCode = vk;

    // 修饰键：Qt → Carbon 经典修饰键位
    // Qt::AltModifier     → optionKey  (0x08, ⌥)
    // Qt::ControlModifier → cmdKey     (0x10, ⌘)
    // Qt::ShiftModifier   → shiftKey   (0x02, ⇧)
    // Qt::MetaModifier    → controlKey (0x04, ⌃)
    UInt32 carbonModifiers = 0;
    if (modifiers & Qt::AltModifier)     carbonModifiers |= optionKey;
    if (modifiers & Qt::ControlModifier) carbonModifiers |= cmdKey;
    if (modifiers & Qt::ShiftModifier)   carbonModifiers |= shiftKey;
    if (modifiers & Qt::MetaModifier)    carbonModifiers |= controlKey;

    LOG_INFO(QString("修饰键映射: Qt Alt=%1 Ctrl=%2 Shift=%3 Meta=%4 → Carbon Option=%5 Cmd=%6 Shift=%7 Ctrl=%8")
             .arg(modifiers & Qt::AltModifier     ? 1 : 0)
             .arg(modifiers & Qt::ControlModifier ? 1 : 0)
             .arg(modifiers & Qt::ShiftModifier   ? 1 : 0)
             .arg(modifiers & Qt::MetaModifier    ? 1 : 0)
             .arg(carbonModifiers & optionKey     ? 1 : 0)
             .arg(carbonModifiers & cmdKey        ? 1 : 0)
             .arg(carbonModifiers & shiftKey      ? 1 : 0)
             .arg(carbonModifiers & controlKey    ? 1 : 0));

    // 安装 Carbon 事件处理器（全局只装一次）
    ensureCarbonHandlerInstalled();
    if (g_carbonHandler == nullptr) {
        LOG_ERROR(QString("Carbon EventHandler 未安装，快捷键注册失败，ID: %1").arg(m_hotkeyId));
        return false;
    }

    // 清理之前可能残留的注册
    if (m_carbonHotKeyRef != nullptr) {
        UnregisterEventHotKey(m_carbonHotKeyRef);
        m_carbonHotKeyRef = nullptr;
        g_carbonHotKeyMap.remove(static_cast<uint32_t>(m_hotkeyId));
    }

    EventHotKeyID hotKeyID;
    hotKeyID.signature = kHotKeySignature;
    hotKeyID.id = static_cast<uint32_t>(m_hotkeyId);

    EventHotKeyRef hotKeyRef = nullptr;
    OSStatus status = RegisterEventHotKey(
        static_cast<UInt32>(m_targetKeyCode),
        carbonModifiers,
        hotKeyID,
        GetApplicationEventTarget(),
        0,
        &hotKeyRef
    );

    if (status != noErr || hotKeyRef == nullptr) {
        LOG_ERROR(QString("RegisterEventHotKey 失败, OSStatus=%1, ID=%2").arg(status).arg(m_hotkeyId));
        return false;
    }

    m_carbonHotKeyRef = hotKeyRef;
    g_carbonHotKeyMap.insert(static_cast<uint32_t>(m_hotkeyId), this);
    m_isRegistered = true;
    LOG_INFO(QString("Carbon快捷键注册成功, ID: %1, 键码: %2, 修饰键: %3")
             .arg(m_hotkeyId).arg(m_targetKeyCode).arg(carbonModifiers));
    return true;
}

/**
 * @brief 平台特定的注销方法（macOS / Carbon）
 *
 * 通过 UnregisterEventHotKey 释放当前实例对应的 Carbon 热键引用，
 * 同时从全局 g_carbonHotKeyMap 中移除热键 ID 映射，避免回调误派。
 *
 * @author chiangyang
 */
void GlobalShortcut::nativeUnregister() {
    if (m_carbonHotKeyRef != nullptr) {
        UnregisterEventHotKey(m_carbonHotKeyRef);
        m_carbonHotKeyRef = nullptr;
        g_carbonHotKeyMap.remove(static_cast<uint32_t>(m_hotkeyId));
        LOG_INFO(QString("Carbon 热键已注销，ID: %1").arg(m_hotkeyId));
    }
    m_isRegistered = false;
}

/**
 * @brief 触发快捷键回调（macOS / Carbon）
 *
 * 由 Carbon 回调通过 QueuedConnection 切回 Qt 主线程后调用，
 * 内部先校验 m_isRegistered 再执行 m_callback，确保回调已注销或
 * 回调为空时不触发意外行为。
 *
 * @author chiangyang
 */
void GlobalShortcut::triggerCallback() {
    if (m_isRegistered && m_callback) {
        m_callback();
    }
}

/**
 * @brief 原生事件过滤器（macOS / Carbon）
 *
 * Carbon 方案不依赖 nativeEventFilter：所有热键事件都由 Carbon 应用级
 * 事件处理器派发，这里直接返回 false 让 Qt 继续处理其他原生事件。
 *
 * @param eventType 事件类型（未使用）
 * @param message   事件消息指针（未使用）
 * @param result    处理结果指针（未使用）
 * @return 始终返回 false，表示未拦截
 * @author chiangyang
 */
bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
}

/**
 * @brief 设置 Fn 键回调（macOS / Carbon）
 *
 * Carbon RegisterEventHotKey 不支持将 Fn 单独作为修饰键注册全局热键，
 * 因此此接口在 Carbon 方案下仅保留兼容，不产生实际效果，并写入警告日志
 * 以便开发者在运行时感知。
 *
 * @param callback 回调函数（未使用）
 * @author chiangyang
 */
void GlobalShortcut::setFnKeyCallback(std::function<void()> callback) {
    Q_UNUSED(callback);
    LOG_WARNING("Carbon RegisterEventHotKey 不支持 Fn 键单独监听，setFnKeyCallback 无效果");
}

#else

bool GlobalShortcut::nativeRegister(Qt::KeyboardModifiers modifiers, Qt::Key key) { return false; }
void GlobalShortcut::nativeUnregister() {}
bool GlobalShortcut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) { return false; }
void GlobalShortcut::setFnKeyCallback(std::function<void()> callback) { Q_UNUSED(callback); }

#endif
