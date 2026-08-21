#ifndef SHORTCUTTYPES_H
#define SHORTCUTTYPES_H

#include <QString>
#include <QKeySequence>
#include <QList>
#include <QMetaType>
#include <QHash>
#include <optional>

#include "../core/ConfigManager.h"

/**
 * @brief 全局快捷键类型枚举
 *
 * 作为注册表、管理器、托盘构建器的统一标识符，
 * 用枚举替代原先散落在 main.cpp 中的字符串 type，
 * 获得编译期类型检查，避免拼写错误。
 * @author chiangyang
 */
enum class ShortcutType {
    Snip,           ///< 截图（选区截图）
    Record,         ///< 录屏
    History,        ///< 历史记录
    Pin,            ///< 贴图剪贴板（Alt+P 历史截图翻页）
    Fullscreen,     ///< 全屏截图
    ActiveWindow,   ///< 活动窗口截图
    RecordPause,    ///< 录屏暂停/恢复
    RecordStop,     ///< 录屏停止
    TogglePins      ///< 隐藏/显示所有贴图
};

/**
 * @brief 快捷键配置项（数据驱动配置表的单条记录）
 *
 * 结合 ShortcutType 枚举与配置文件/翻译/i18n 的所有静态信息，
 * 一份数据同时服务于：注册热键、设置 UI、托盘菜单、默认值读取。
 * @author chiangyang
 */
struct ShortcutConfigItem {
    ShortcutType type;           ///< 类型枚举
    const char* configKey;       ///< 配置文件键名（QSettings key）
    const char* defaultValue;    ///< 默认快捷键字符串
    const char* trayTextKey;     ///< 托盘菜单文本的 i18n 键（nullptr=无托盘菜单项）
    const char* trayFallback;    ///< 托盘菜单文本回退值（i18n 缺失时使用）
};

/**
 * @brief 全局快捷键配置数据表（数据驱动设计）
 *
 * 9 种全局快捷键的静态元信息集中定义，
 * 新增/删除快捷键只需修改此表即可，其它代码自动生效。
 * @author chiangyang
 */
static const ShortcutConfigItem kShortcutConfigs[] = {
    { ShortcutType::Snip,         "shortcut_snip",         "Alt+Q",        "trayCapture",      "Capture" },
    { ShortcutType::Record,       "shortcut_record",       "Alt+S",        "trayRecord",       "Record" },
    { ShortcutType::History,      "shortcut_history",      "Alt+H",        "tray.history",     "History" },
    { ShortcutType::Pin,          "shortcut_pin",          "Alt+P",        "tray.pin",         "Pin Clipboard" },
    { ShortcutType::Fullscreen,   "shortcut_fullscreen",   "Alt+Shift+F",  "tray.fullscreen",  "Fullscreen Capture" },
    { ShortcutType::ActiveWindow, "shortcut_activewindow", "Alt+Shift+W",  "tray.activewindow","Active Window Capture" },
    { ShortcutType::RecordPause,  "shortcut_recordpause",  "Alt+Shift+S",  "tray.recordpause", "Record Pause/Resume" },
    { ShortcutType::RecordStop,   "shortcut_recordstop",   "Alt+Shift+Q",  "tray.recordstop",  "Record Stop" },
    { ShortcutType::TogglePins,   "shortcut_togglepins",   "Alt+Shift+P",  "tray.togglepins",  "Toggle All Pins" },
};

/**
 * @brief 配置表元素总数
 * @author chiangyang
 */
static constexpr int kShortcutConfigCount =
    sizeof(kShortcutConfigs) / sizeof(kShortcutConfigs[0]);

/**
 * @brief ShortcutType 枚举转字符串
 * @param type 枚举值
 * @return 对应字符串表示（与 SettingsWindow::shortcutChanged 兼容）
 * @author chiangyang
 */
inline QString shortcutTypeToString(ShortcutType type) {
    static const QHash<ShortcutType, QString> mapping = {
        { ShortcutType::Snip,         "snip" },
        { ShortcutType::Record,       "record" },
        { ShortcutType::History,      "history" },
        { ShortcutType::Pin,          "pin" },
        { ShortcutType::Fullscreen,   "fullscreen" },
        { ShortcutType::ActiveWindow, "activewindow" },
        { ShortcutType::RecordPause,  "recordpause" },
        { ShortcutType::RecordStop,   "recordstop" },
        { ShortcutType::TogglePins,   "togglepins" },
    };
    return mapping.value(type, QString());
}

/**
 * @brief 字符串转 ShortcutType 枚举
 * @param typeStr 字符串（如 "snip"、"record"）
 * @return 对应枚举值，解析失败返回 std::nullopt
 * @author chiangyang
 */
inline std::optional<ShortcutType> shortcutTypeFromString(const QString& typeStr) {
    static const QHash<QString, ShortcutType> mapping = {
        { "snip",         ShortcutType::Snip },
        { "record",       ShortcutType::Record },
        { "history",      ShortcutType::History },
        { "pin",          ShortcutType::Pin },
        { "fullscreen",   ShortcutType::Fullscreen },
        { "activewindow", ShortcutType::ActiveWindow },
        { "recordpause",  ShortcutType::RecordPause },
        { "recordstop",   ShortcutType::RecordStop },
        { "togglepins",   ShortcutType::TogglePins },
    };
    auto it = mapping.find(typeStr);
    if (it == mapping.end()) return std::nullopt;
    return it.value();
}

/**
 * @brief 根据类型获取配置项
 * @param type 快捷键类型
 * @return 对应配置项指针，未找到返回 nullptr
 * @author chiangyang
 */
inline const ShortcutConfigItem* getShortcutConfig(ShortcutType type) {
    for (int i = 0; i < kShortcutConfigCount; ++i) {
        if (kShortcutConfigs[i].type == type) {
            return &kShortcutConfigs[i];
        }
    }
    return nullptr;
}

/**
 * @brief 从配置文件读取快捷键序列（向后兼容旧 key）
 * @param config 配置项
 * @return 实际使用的快捷键序列
 *
 * 历史记录快捷键的配置键从 "history/shortcut" 更名为 "shortcut_history"，
 * 本函数优先读取新键，键不存在时自动回退到旧键（history/shortcut），
 * 保证用户已有配置不丢失。
 * @author chiangyang
 */
inline QKeySequence getShortcutSequence(const ShortcutConfigItem& config) {
    QSettings* settings = ConfigManager::instance()->getSettings();
    QString value = settings->value(config.configKey, QString()).toString();
    if (!value.isEmpty()) {
        return QKeySequence(value);
    }
    // 向后兼容：历史记录快捷键旧配置键 history/shortcut
    if (config.type == ShortcutType::History) {
        QString legacy = settings->value("history/shortcut", QString()).toString();
        if (!legacy.isEmpty()) {
            return QKeySequence(legacy);
        }
    }
    return QKeySequence(config.defaultValue);
}

/**
 * @brief 获取所有配置项列表
 * @return 配置项引用列表（浅拷贝，顺序同数据表定义）
 * @author chiangyang
 */
inline QList<const ShortcutConfigItem*> getAllShortcutConfigs() {
    QList<const ShortcutConfigItem*> result;
    result.reserve(kShortcutConfigCount);
    for (int i = 0; i < kShortcutConfigCount; ++i) {
        result.append(&kShortcutConfigs[i]);
    }
    return result;
}

Q_DECLARE_METATYPE(ShortcutType)

#endif // SHORTCUTTYPES_H
