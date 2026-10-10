#ifndef DEEPL_ENGINE_H
#define DEEPL_ENGINE_H

#include "TranslateEngine.h"
#include <QString>

/**
 * @brief DeepL 翻译引擎
 *
 * 通过 DeepL API 进行翻译，需要用户自填 API Key。
 * 翻译质量高，使用免费版接口（api-free.deepl.com）。
 * 不传 source_lang 时自动检测源语言。
 * @author chiangyang
 */
class DeepLEngine : public TranslateEngine {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit DeepLEngine(QObject *parent = nullptr);

    /**
     * @brief 获取引擎名称
     * @return "deepl"
     * @author chiangyang
     */
    QString name() const override { return "deepl"; }

    /**
     * @brief 引擎是否可用（Key 已配置）
     * @return 是否可用
     * @author chiangyang
     */
    bool isAvailable() const override;

    /**
     * @brief 是否需要 API Key
     * @return DeepL 需要 Key，返回 true
     * @author chiangyang
     */
    bool requiresApiKey() const override { return true; }

    /**
     * @brief 设置 DeepL API Key
     * @param key API Key
     * @author chiangyang
     */
    void setKey(const QString &key) { m_key = key; }

protected:
    /**
     * @brief 发送 POST 请求（表单编码 + DeepL-Auth-Key 鉴权头）
     * @author chiangyang
     */
    QNetworkReply *sendRequest(const QString &text,
                               const QString &sourceLang,
                               const QString &targetLang) override;

    /**
     * @brief 解析 DeepL JSON 响应（translations[].text）
     * @author chiangyang
     */
    bool parseResponse(const QByteArray &data,
                       QString &outTranslated,
                       TranslateError &outError,
                       QString &outDetail) override;

private:
    /**
     * @brief 将通用语言代码转换为 DeepL 语言代码（大写）
     * @param code 通用语言代码
     * @return DeepL 语言代码（如 EN、ZH）
     * @author chiangyang
     */
    static QString toDeepLLang(const QString &code);

    QString m_key; ///< DeepL API Key
};

#endif // DEEPL_ENGINE_H
