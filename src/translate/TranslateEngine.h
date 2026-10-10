#ifndef TRANSLATE_ENGINE_H
#define TRANSLATE_ENGINE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;
class QNetworkAccessManager;

/**
 * @brief 翻译引擎抽象基类（模板方法）
 *
 * 定义统一的异步翻译接口，屏蔽不同翻译引擎（MyMemory/百度/DeepL/LibreTranslate）的差异。
 * translate() 为模板方法：统一处理空文本/未配置检查、原文记录、网络/SSL 错误分类、
 * 空译文兜底与结果回传；子类只需实现 sendRequest()（构造并发送 HTTP 请求，含
 * 语言映射与签名）与 parseResponse()（解析响应体）。结果通过 finished/failed 信号回传。
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
    explicit TranslateEngine(QObject *parent = nullptr);

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
        Busy,           ///< 翻译服务忙（批量翻译进行中，暂不接受新请求）
        Unknown         ///< 未知错误（兜底）
    };

public slots:
    /**
     * @brief 翻译模板方法：前置检查 → 子类发请求 → 基类统一回传结果
     * @param text 源文本
     * @param sourceLang 源语言代码（如 "en"），"auto" 表示自动检测
     * @param targetLang 目标语言代码（如 "zh-CN"、"en"）
     * @author chiangyang
     */
    void translate(const QString &text,
                   const QString &sourceLang,
                   const QString &targetLang);

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

protected:
    /**
     * @brief 构造并发送本引擎的 HTTP 请求（子类实现）
     *
     * 包含语言代码映射、鉴权/签名、URL 与报文构造、请求日志。
     * 请求成功发出时返回 reply（finished 由基类连接，无需子类 connect）；
     * 无法发起请求时返回 nullptr，子类需已自行 emit failed 并记录日志。
     * @param text 源文本（已通过空文本/可用性检查）
     * @param sourceLang 源语言代码（可能为 "auto"）
     * @param targetLang 目标语言代码
     * @return 已发出的 reply，或 nullptr
     * @author chiangyang
     */
    virtual QNetworkReply *sendRequest(const QString &text,
                                       const QString &sourceLang,
                                       const QString &targetLang) = 0;

    /**
     * @brief 解析本引擎的响应体（子类实现）
     * @param data 响应体
     * @param outTranslated 解析成功时输出译文
     * @param outError 解析失败时输出错误分类
     * @param outDetail 解析失败时输出技术细节
     * @return 解析成功返回 true；失败返回 false 并置 outError/outDetail
     * @author chiangyang
     */
    virtual bool parseResponse(const QByteArray &data,
                               QString &outTranslated,
                               TranslateError &outError,
                               QString &outDetail) = 0;

    /**
     * @brief 经引擎共享的网络管理器发起 GET 请求
     * @param url 请求地址
     * @return 已发出的 reply
     * @author chiangyang
     */
    QNetworkReply *httpGet(const QUrl &url);

    /**
     * @brief 经引擎共享的网络管理器发起 POST 请求
     * @param request 已设置 Content-Type/鉴权等头部的请求
     * @param body 请求体
     * @return 已发出的 reply
     * @author chiangyang
     */
    QNetworkReply *httpPost(const QNetworkRequest &request, const QByteArray &body);

private slots:
    /**
     * @brief 网络回复统一处理槽：网络/SSL 错误分类 → 子类解析 → 发出结果
     * @author chiangyang
     */
    void onReplyFinished();

private:
    QNetworkAccessManager *m_networkManager; ///< 引擎内共享的网络管理器
    QString m_pendingOriginal;               ///< 当前待翻译原文（finished 回传时使用）
};

#endif // TRANSLATE_ENGINE_H
