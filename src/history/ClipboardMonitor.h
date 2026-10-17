#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QClipboard>
#include <QString>
#include <QTimer>

/**
 * @brief 剪贴板监听器类
 *
 * 监听系统剪贴板变化，捕获用户的复制/剪切操作。
 * Windows/Linux：QClipboard::dataChanged 信号（系统事件驱动，后台也触发）；
 * macOS：Cocoa 不为后台剪贴板变更派发通知（Qt 仅在应用激活时检查粘贴板），
 * 改用 1s 定时器轮询 NSPasteboard changeCount 兜底，变化后走同一处理槽。
 * @author chiangyang
 */
class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit ClipboardMonitor(QObject *parent = nullptr);

    /**
     * @brief 启动剪贴板监听
     * @author chiangyang
     */
    void start();

    /**
     * @brief 停止剪贴板监听
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 获取当前监听状态
     * @return 是否正在监听
     * @author chiangyang
     */
    bool isActive() const;

private slots:
    /**
     * @brief 剪贴板变化处理槽函数
     *
     * 读取剪贴板文本内容，避免重复记录，
     * 然后调用 HistoryManager 保存记录。
     * @author chiangyang
     */
    void onClipboardChanged();

#ifdef Q_OS_MACOS
    /**
     * @brief 粘贴板轮询槽（仅 macOS）
     *
     * Cocoa 不为后台剪贴板变更派发 QClipboard::dataChanged（Qt 仅在应用
     * 激活时检查粘贴板），以 1s 定时器轮询 NSPasteboard changeCount 兜底，
     * 变化后手动走同一处理槽 onClipboardChanged（内部有文本去重）。
     * @author chiangyang
     */
    void pollPasteboardChange();
#endif

private:
    /**
     * @brief 获取剪贴板文本内容
     * @return 文本内容
     * @author chiangyang
     */
    QString getClipboardText();

#ifdef Q_OS_MACOS
    /**
     * @brief 读取系统粘贴板当前 changeCount（仅 macOS）
     * @author chiangyang
     */
    qint64 currentPasteboardChangeCount() const;
#endif

    /**
     * @brief 获取来源应用名称
     * @return 前台应用的显示名（如 "Safari"、"chrome.exe"）；
     *         取不到时回退本应用名。Windows 用 GetForegroundWindow +
     *         进程镜像名，macOS 用 NSWorkspace frontmostApplication，
     *         Linux 无低成本 Qt API 暂回退。
     * @author chiangyang
     */
    QString getSourceAppName();

    QClipboard *m_clipboard;    ///< 系统剪贴板对象
    QString m_lastText;         ///< 上次记录的文本，用于去重
    bool m_isActive;            ///< 是否正在监听

#ifdef Q_OS_MACOS
    QTimer *m_pollTimer = nullptr;  ///< macOS 后台剪贴板轮询定时器
    qint64 m_lastChangeCount = 0;   ///< 上次粘贴板 changeCount 快照（启动时初始化，只捕获启动后的变化）
#endif
};

#endif // CLIPBOARDMONITOR_H
