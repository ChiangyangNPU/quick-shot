#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QClipboard>
#include <QString>

/**
 * @brief 剪贴板监听器类
 *
 * 监听系统剪贴板变化，捕获用户的复制/剪切操作。
 * 通过 QClipboard::dataChanged 信号实现。
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

private:
    /**
     * @brief 获取剪贴板文本内容
     * @return 文本内容
     * @author chiangyang
     */
    QString getClipboardText();

    /**
     * @brief 获取来源应用名称
     * @return 应用名称
     * @author chiangyang
     */
    QString getSourceAppName();

    QClipboard *m_clipboard;    ///< 系统剪贴板对象
    QString m_lastText;         ///< 上次记录的文本，用于去重
    bool m_isActive;            ///< 是否正在监听
};

#endif // CLIPBOARDMONITOR_H
