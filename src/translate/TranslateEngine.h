#ifndef TRANSLATE_ENGINE_H
#define TRANSLATE_ENGINE_H

#include <QObject>
#include <QString>

/**
 * @brief 翻译引擎抽象接口
 *
 * 定义统一的异步翻译接口，屏蔽不同翻译引擎（MyMemory/百度/DeepL/LibreTranslate）的差异。
 * 各引擎实现该接口，通过 finished/failed 信号回传结果。
 * @author chiangyang
 */
class TranslateEngine : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit TranslateEngine(QObject *parent = nullptr) : QObject(parent) {}

    /**
     * @brief 虚析构函数
     * @author chiangyang
     */
    virtual ~TranslateEngine() = default;

    /**
     * @brief 获取引擎名称（用于设置页展示与配置标识）
     * @return 引擎名称，如 "mymemory"、"baidu"
     * @author chiangyang
     */
    virtual QString name() const = 0;

    /**
     * @brief 引擎是否可用（如 Key 已配置、URL 已填写）
     * @return 是否可用
     * @author chiangyang
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief 是否需要用户配置 API Key
     * @return 是否需要 Key
     * @author chiangyang
     */
    virtual bool requiresApiKey() const = 0;

    /**
     * @brief 翻译错误类型枚举
     *
     * 用于失败信号的稳定错误分类，UI 层据此查本地化文案。
     * 具体技术细节通过 failed 信号的 detail 参数透传，不丢失信息。
     * @author chiangyang
     */
    enum class TranslateError {
        NetworkFailed,  ///< 网络错误（DNS/超时/断开等）
        SslFailed,      ///< SSL/TLS 配置或握手失败
        SameLanguage,   ///< 源语言与目标语言相同
        RateLimit,      ///< 翻译额度用尽
        NotConfigured,  ///< 引擎未配置（缺 Key/URL）
        ApiError,       ///< 翻译服务业务错误（非 200 等）
        EmptyText,      ///< 空文本，无可翻译内容
        Unknown         ///< 未知错误（兜底）
    };

public slots:
    /**
     * @brief 异步翻译一段文本
     * @param text 源文本
     * @param sourceLang 源语言代码（如 "en"），"auto" 表示自动检测
     * @param targetLang 目标语言代码（如 "zh-CN"、"en"）
     * @author chiangyang
     */
    virtual void translate(const QString &text,
                           const QString &sourceLang,
                           const QString &targetLang) = 0;

signals:
    /**
     * @brief 翻译完成信号
     * @param original 原文
     * @param translated 译文
     * @author chiangyang
     */
    void finished(const QString &original, const QString &translated);

    /**
     * @brief 翻译失败信号
     * @param code 错误分类码，UI 据此查本地化文案
     * @param detail 原始技术细节（如 Qt errorString、HTTP 状态、API 返回信息），可空
     * @author chiangyang
     */
    void failed(TranslateError code, const QString &detail = QString());
};

#endif // TRANSLATE_ENGINE_H
