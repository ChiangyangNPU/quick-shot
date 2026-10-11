#ifndef CAPTURESESSION_H
#define CAPTURESESSION_H

#include <QPixmap>
#include <QString>
#include <QGuiApplication>
#include <QApplication>
#include <QClipboard>
#include "../history/HistoryManager.h"

/**
 * @brief 截图会话产物分发
 *
 * 统一"截图产物 → 剪贴板 + 历史记录"的分发逻辑。此前该三件套在
 * SnipScreen（复制 / 贴图 / 全屏 / 活动窗口 / 录屏快照）与 PinWindow
 * （复制）共 7 处重复，且窗口标题回退（空标题 → 应用名）各处写法不一。
 *
 * 行为约定（与原各调用点逐一对齐）：
 * - toClipboard：无条件写入剪贴板（含 pixmap 为空的边界情况，保持
 *   原 copy() 的语义——空图清空剪贴板）
 * - recordHistory：pixmap 非空且 HistoryManager 开启截图历史时，
 *   以 sourceTitle（空则回退应用名）记录，供 Alt+P 翻页
 * @author chiangyang
 */
namespace CaptureSession {

inline void publish(const QPixmap &pixmap, const QString &sourceTitle = QString(),
                    bool toClipboard = true, bool recordHistory = true) {
    if (toClipboard) {
        QGuiApplication::clipboard()->setPixmap(pixmap);
    }
    if (recordHistory && !pixmap.isNull()
        && HistoryManager::instance()->isScreenshotEnabled()) {
        const QString title = sourceTitle.isEmpty()
            ? QApplication::applicationName() : sourceTitle;
        HistoryManager::instance()->addScreenshotPixmap(pixmap, title);
    }
}

} // namespace CaptureSession

#endif // CAPTURESESSION_H
