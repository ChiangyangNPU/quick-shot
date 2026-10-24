#include "ClipboardMonitor.h"
#include "HistoryManager.h"
#include "../log/Logger.h"

#include <QApplication>
#include <QClipboard>

#ifdef Q_OS_WIN
#include <QFileInfo>
#include <windows.h>
#elif defined(Q_OS_MACOS)
// 经 ObjC runtime 消息发送取前台应用（.cpp 内不引 AppKit 头，与
// SnipScreen.cpp 的 raiseWindowAboveMenuBar 同一惯用法）
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/objc.h>
#endif

ClipboardMonitor* ClipboardMonitor::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @author chiangyang
 */
ClipboardMonitor* ClipboardMonitor::instance()
{
    if (!s_instance) {
        s_instance = new ClipboardMonitor();
    }
    return s_instance;
}

/**
 * @brief 标记文本为已记录
 * @author chiangyang
 */
void ClipboardMonitor::markTextAsRecorded(const QString &text)
{
    m_lastText = text;
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
ClipboardMonitor::ClipboardMonitor(QObject *parent)
    : QObject(parent)
    , m_clipboard(nullptr)
    , m_isActive(false)
{
    LOG_INFO("ClipboardMonitor instance created");
}

/**
 * @brief 启动剪贴板监听
 * @author chiangyang
 */
void ClipboardMonitor::start()
{
    if (m_isActive) {
        return;
    }

    m_clipboard = QApplication::clipboard();
    if (!m_clipboard) {
        LOG_ERROR("Failed to get system clipboard");
        return;
    }

#ifdef Q_OS_MACOS
    // macOS：Cocoa 不为后台剪贴板变更派发 QClipboard::dataChanged（Qt 仅在
    // 应用激活时检查粘贴板，后台复制会漏记），改用 1s 轮询 changeCount 兜底，
    // 变化后手动走同一处理槽（onClipboardChanged 内有文本去重）。
    // 快照在启动时取，只捕获启动之后的变化（与其他平台 dataChanged 语义一致）。
    m_lastChangeCount = currentPasteboardChangeCount();
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout,
            this, &ClipboardMonitor::pollPasteboardChange);
    m_pollTimer->start(1000);
    LOG_INFO("Clipboard monitor started (macOS pasteboard polling, 1s)");
#else
    connect(m_clipboard, &QClipboard::dataChanged,
            this, &ClipboardMonitor::onClipboardChanged);
    LOG_INFO("Clipboard monitor started");
#endif

    m_isActive = true;
}

/**
 * @brief 停止剪贴板监听
 * @author chiangyang
 */
void ClipboardMonitor::stop()
{
    if (!m_isActive) {
        return;
    }

#ifdef Q_OS_MACOS
    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }
#else
    if (m_clipboard) {
        disconnect(m_clipboard, &QClipboard::dataChanged,
                   this, &ClipboardMonitor::onClipboardChanged);
    }
#endif

    m_isActive = false;
    LOG_INFO("Clipboard monitor stopped");
}

#ifdef Q_OS_MACOS
/**
 * @brief 读取系统粘贴板当前 changeCount（仅 macOS）
 *
 * [NSPasteboard generalPasteboard].changeCount，经 ObjC runtime 消息
 * 发送获取（纯 .cpp 不引 AppKit 头，与 getSourceAppName 同惯用法）。
 * @author chiangyang
 */
qint64 ClipboardMonitor::currentPasteboardChangeCount() const
{
    id gp = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSPasteboard"),
                                            sel_registerName("generalPasteboard"));
    if (!gp) {
        return 0;
    }
    // changeCount 返回 NSInteger（64 位下为 long）
    return static_cast<qint64>(((long (*)(id, SEL))objc_msgSend)(gp,
                                                                 sel_registerName("changeCount")));
}

/**
 * @brief 粘贴板轮询槽（仅 macOS）
 *
 * changeCount 相对上次快照有变化即视为一次剪贴板变更，
 * 手动调用与 dataChanged 相同的处理槽；文本去重由
 * onClipboardChanged 内部的 m_lastText 比较兜底（应用激活期间
 * dataChanged 与轮询可能对同一次变更各触发一次）。
 * @author chiangyang
 */
void ClipboardMonitor::pollPasteboardChange()
{
    if (!m_isActive) {
        return;
    }
    const qint64 count = currentPasteboardChangeCount();
    if (count == m_lastChangeCount) {
        return;
    }
    m_lastChangeCount = count;
    onClipboardChanged();
}
#endif

/**
 * @brief 获取当前监听状态
 * @return 是否正在监听
 * @author chiangyang
 */
bool ClipboardMonitor::isActive() const
{
    return m_isActive;
}

/**
 * @brief 剪贴板变化处理槽函数
 *
 * 读取剪贴板文本内容，避免重复记录，
 * 然后调用 HistoryManager 保存记录。
 * @author chiangyang
 */
void ClipboardMonitor::onClipboardChanged()
{
    if (!HistoryManager::instance()->isClipboardEnabled()) {
        return;
    }

    QString text = getClipboardText();
    if (text.isEmpty()) {
        return;
    }

    // 避免重复记录相同文本
    if (text == m_lastText) {
        return;
    }

    QString sourceApp = getSourceAppName();
    HistoryManager::instance()->addClipboardText(text, sourceApp);

    m_lastText = text;
    LOG_INFO(QString("Clipboard text captured from source: %1").arg(sourceApp));
}

/**
 * @brief 获取剪贴板文本内容
 * @return 文本内容
 * @author chiangyang
 */
QString ClipboardMonitor::getClipboardText()
{
    if (!m_clipboard) {
        return QString();
    }

    // 只获取文本类型，忽略图片等其他类型
    QString text = m_clipboard->text();
    return text.trimmed();
}

/**
 * @brief 获取来源应用名称（前台应用，按平台取真实进程）
 *
 * 复制动作发生在前台应用，其显示名对历史记录才有意义。原实现取
 * QApplication::activeWindow()——那是本应用的窗口（剪贴板变化多来自
 * 其他应用，本应用通常并无活动窗口），记录值基本无意义。
 * @author chiangyang
 */
#if defined(Q_OS_WIN)
QString ClipboardMonitor::getSourceAppName()
{
    const QString fallback = QApplication::applicationName();
    HWND fg = GetForegroundWindow();
    if (!fg) return fallback;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return fallback;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return fallback;
    WCHAR path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(proc, 0, path, &size)) {
        name = QFileInfo(QString::fromWCharArray(path, static_cast<int>(size))).fileName();
    }
    CloseHandle(proc);
    return name.isEmpty() ? fallback : name;
}
#elif defined(Q_OS_MACOS)
QString ClipboardMonitor::getSourceAppName()
{
    const QString fallback = QApplication::applicationName();
    // [NSWorkspace sharedWorkspace]
    id ws = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSWorkspace"),
                                            sel_registerName("sharedWorkspace"));
    if (!ws) return fallback;
    // ws.frontmostApplication
    id app = ((id (*)(id, SEL))objc_msgSend)(ws, sel_registerName("frontmostApplication"));
    if (!app) return fallback;
    // app.localizedName（NSObject* 与 CFStringRef 无成本桥接，纯 C 下直接强转）
    CFStringRef cfName = (CFStringRef)((id (*)(id, SEL))objc_msgSend)(app,
                                                                      sel_registerName("localizedName"));
    if (!cfName) return fallback;
    char buf[512]{};
    if (CFStringGetCString(cfName, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        const QString name = QString::fromUtf8(buf);
        if (!name.isEmpty()) return name;
    }
    return fallback;
}
#else
// Linux：Qt 无前台应用查询 API，X11 方案（_NET_ACTIVE_WINDOW + _NET_WM_PID）
// 成本较高暂不引入，回退本应用名（来源字段在 Linux 上暂无参考价值）
QString ClipboardMonitor::getSourceAppName()
{
    return QApplication::applicationName();
}
#endif
