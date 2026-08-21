#include "GlobalShortcut.h"
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

    // 将Qt::Key转换为Windows虚拟键码
    // 字母键 A-Z 和数字键 0-9 的 Qt::Key 与 ASCII 码相同，可直接用作 VK
    // 功能键 F1-F24 通过偏移计算
    // 标点键（反引号/减号/方括号等）需通过 OEM 虚拟键码映射表转换
    UINT vk = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        vk = key;
    } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        vk = key;
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = VK_F1 + (key - Qt::Key_F1);
    } else {
        // 常用标点键映射表（OEM 虚拟键码）
        // 解决反引号(`)等标点键无法注册全局热键的问题
        static const QHash<int, UINT> extraKeyMap = {
            { Qt::Key_QuoteLeft,   VK_OEM_3 },      // ` 反引号
            { Qt::Key_Minus,       VK_OEM_MINUS },  // - 减号
            { Qt::Key_Equal,       VK_OEM_PLUS },   // = 等号
            { Qt::Key_BracketLeft,  VK_OEM_4 },     // [ 左方括号
            { Qt::Key_BracketRight, VK_OEM_6 },     // ] 右方括号
            { Qt::Key_Semicolon,   VK_OEM_1 },      // ; 分号
            { Qt::Key_Apostrophe,  VK_OEM_7 },      // ' 单引号
            { Qt::Key_Comma,       VK_OEM_COMMA },  // , 逗号
            { Qt::Key_Period,      VK_OEM_PERIOD }, // . 句号
            { Qt::Key_Slash,       VK_OEM_2 },      // / 斜杠
            { Qt::Key_Backslash,   VK_OEM_5 },      // \ 反斜杠
        };
        auto it = extraKeyMap.find(key);
        if (it != extraKeyMap.end()) {
            vk = it.value();
        }
    }

    if (vk == 0) {
        LOG_ERROR(QString("不支持的键: %1, ID: %2")
                  .arg(key).arg(m_hotkeyId));
        LOG_WARNING(QString("Unsupported key for global shortcut: %1").arg(key));
        return false;
    }

    // RegisterHotKey(HWND, ID, Modifiers, VK)
    // HWND可以为NULL，以便在消息循环线程中接收WM_HOTKEY
    BOOL result = RegisterHotKey(NULL, m_hotkeyId, fsModifiers | MOD_NOREPEAT, vk);
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

    // Qt::Key 到 macOS 虚拟键码（kVK_ANSI_*）映射表（与 Carbon 一致）
    static const QHash<int, int> keyMapping = {
        // 字母键
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_S, kVK_ANSI_S},
        {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_F, kVK_ANSI_F},
        {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_G, kVK_ANSI_G},
        {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_V, kVK_ANSI_V},
        {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_Q, kVK_ANSI_Q},
        {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_E, kVK_ANSI_E},
        {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_Y, kVK_ANSI_Y},
        {Qt::Key_T, kVK_ANSI_T}, {Qt::Key_O, kVK_ANSI_O},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_I, kVK_ANSI_I},
        {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K},
        {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_M, kVK_ANSI_M},
        // 数字键
        {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2},
        {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4},
        {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6},
        {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8},
        {Qt::Key_9, kVK_ANSI_9}, {Qt::Key_0, kVK_ANSI_0},
        // 标点键
        {Qt::Key_QuoteLeft,   kVK_ANSI_Grave},
        {Qt::Key_Minus,       kVK_ANSI_Minus},
        {Qt::Key_Equal,       kVK_ANSI_Equal},
        {Qt::Key_BracketLeft, kVK_ANSI_LeftBracket},
        {Qt::Key_BracketRight,kVK_ANSI_RightBracket},
        {Qt::Key_Semicolon,   kVK_ANSI_Semicolon},
        {Qt::Key_Apostrophe,  kVK_ANSI_Quote},
        {Qt::Key_Comma,       kVK_ANSI_Comma},
        {Qt::Key_Period,      kVK_ANSI_Period},
        {Qt::Key_Slash,       kVK_ANSI_Slash},
        {Qt::Key_Backslash,   kVK_ANSI_Backslash},
    };

    if (keyMapping.contains(key)) {
        m_targetKeyCode = keyMapping.value(key);
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        m_targetKeyCode = kVK_F1 + (key - Qt::Key_F1);
    } else {
        LOG_ERROR(QString("不支持的键: %1, ID: %2").arg(key).arg(m_hotkeyId));
        return false;
    }

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
