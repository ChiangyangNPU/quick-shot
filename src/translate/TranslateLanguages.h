#ifndef TRANSLATELANGUAGES_H
#define TRANSLATELANGUAGES_H

#include <QVector>

/**
 * @brief 翻译目标语言条目
 * @author chiangyang
 */
struct TranslateLanguageOption {
    const char *name; ///< 显示名称
    const char *code; ///< 语言代码（与翻译引擎约定的代码一致）
};

/**
 * @brief 翻译目标语言清单
 *
 * 设置页语言下拉框与翻译结果窗口的语言切换器共用本清单，
 * 避免两处维护不一致。
 * @return 语言条目列表（静态只读）
 * @author chiangyang
 */
inline const QVector<TranslateLanguageOption> &translateTargetLanguageOptions() {
    static const QVector<TranslateLanguageOption> options = {
        {"English", "en"},
        {"中文", "zh-CN"},
        {"繁體中文", "zh-TW"},
        {"日本語", "ja"},
        {"한국어", "ko"},
        {"Français", "fr"},
        {"Deutsch", "de"},
        {"Español", "es"},
        {"Русский", "ru"},
        {"Português", "pt"},
    };
    return options;
}

#endif // TRANSLATELANGUAGES_H
