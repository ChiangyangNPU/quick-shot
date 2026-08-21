#ifndef HISTORYITEM_H
#define HISTORYITEM_H

#include <QString>
#include <QDateTime>
#include <QSize>
#include <QList>

/**
 * @brief 历史记录类型枚举
 *
 * 区分不同类型的历史记录，用于分类显示和筛选
 * @author chiangyang
 */
enum class HistoryType {
    All = -1,          ///< 全部类型（用于筛选时不过滤）
    Screenshot = 0,    ///< 截图记录
    ClipboardText = 1  ///< 剪贴板文本
};

/**
 * @brief 历史记录数据结构
 *
 * 描述一条历史记录的完整信息，包括截图和剪贴板文本两种类型。
 * @author chiangyang
 */
struct HistoryItem {
    qint64 id = 0;              ///< 自增 ID
    HistoryType type = HistoryType::Screenshot; ///< 类型（截图或剪贴板文本）
    QString content;            ///< 文本内容或截图文件路径
    QString thumbnailPath;      ///< 缩略图路径（仅截图类型）
    QString sourceApp;          ///< 来源应用名称（仅剪贴板类型）
    QString windowTitle;        ///< 窗口标题（仅截图类型）
    QDateTime timestamp;        ///< 记录时间戳
    QSize imageSize;            ///< 图片尺寸（仅截图类型）

    /**
     * @brief 构造函数
     * @author chiangyang
     */
    HistoryItem() = default;

    /**
     * @brief 判断是否为截图类型
     * @return 是否为截图类型
     * @author chiangyang
     */
    bool isScreenshot() const { return type == HistoryType::Screenshot; }

    /**
     * @brief 判断是否为剪贴板文本类型
     * @return 是否为剪贴板文本类型
     * @author chiangyang
     */
    bool isClipboardText() const { return type == HistoryType::ClipboardText; }

    /**
     * @brief 获取类型名称
     * @return 类型名称字符串
     * @author chiangyang
     */
    QString typeName() const {
        return isScreenshot() ? "Screenshot" : "ClipboardText";
    }
};

/**
 * @brief 历史记录列表类型定义
 * @author chiangyang
 */
using HistoryItemList = QList<HistoryItem>;

#endif // HISTORYITEM_H
