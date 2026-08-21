#include "ClipboardMonitor.h"
#include "HistoryManager.h"
#include "../log/Logger.h"

#include <QApplication>
#include <QClipboard>
#include <QWindow>
#include <QWidget>

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

    connect(m_clipboard, &QClipboard::dataChanged,
            this, &ClipboardMonitor::onClipboardChanged);

    m_isActive = true;
    LOG_INFO("Clipboard monitor started");
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

    if (m_clipboard) {
        disconnect(m_clipboard, &QClipboard::dataChanged,
                   this, &ClipboardMonitor::onClipboardChanged);
    }

    m_isActive = false;
    LOG_INFO("Clipboard monitor stopped");
}

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
 * @brief 获取来源应用名称
 * @return 应用名称
 * @author chiangyang
 */
QString ClipboardMonitor::getSourceAppName()
{
    // 尝试获取当前活动窗口的应用名称
    // 由于 Qt 没有直接提供获取活动窗口应用的 API，
    // 这里使用一个通用的方法，在不同平台上可能不同
    QString appName = qApp->applicationName();

    // 如果能获取到活动窗口，则尝试获取其所属应用
    QWidget *activeWindow = QApplication::activeWindow();
    if (activeWindow) {
        QWindow *window = activeWindow->windowHandle();
        if (window) {
            // 尝试获取窗口标题作为来源信息
            QString title = activeWindow->windowTitle();
            if (!title.isEmpty()) {
                return title;
            }
        }
    }

    return appName;
}
