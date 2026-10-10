#ifndef LIBRE_TRANSLATE_ENGINE_H
#define LIBRE_TRANSLATE_ENGINE_H

#include "TranslateEngine.h"
#include <QString>

/**
 * @brief LibreTranslate 翻译引擎
 *
 * 通过 LibreTranslate 实例进行翻译，支持用户自托管服务实现完全离线。
 * 需要用户填写服务地址（如 https://libretranslate.com 或自建实例）。
 * 支持 source=auto 自动检测源语言。
 * @author chiangyang
 */
class LibreTranslateEngine : public TranslateEngine {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit LibreTranslateEngine(QObject *parent = nullptr);

    /**
     * @brief 获取引擎名称
     * @return "libretranslate"
     * @author chiangyang
     */
    QString name() const override { return "libretranslate"; }

    /**
     * @brief 引擎是否可用（服务地址已填写）
     * @return 是否可用
     * @author chiangyang
     */
    bool isAvailable() const override;

    /**
     * @brief 是否需要 API Key
     * @return LibreTranslate 以 URL 为主，返回 false
     * @author chiangyang
     */
    bool requiresApiKey() const override { return false; }

    /**
     * @brief 设置 LibreTranslate 服务地址
     * @param url 服务地址（如 https://libretranslate.com）
     * @author chiangyang
     */
    void setUrl(const QString &url) { m_url = url; }

protected:
    /**
     * @brief 发送 POST 请求（JSON 报文）
     * @author chiangyang
     */
    QNetworkReply *sendRequest(const QString &text,
                               const QString &sourceLang,
                               const QString &targetLang) override;

    /**
     * @brief 解析 LibreTranslate JSON 响应（error / translatedText）
     * @author chiangyang
     */
    bool parseResponse(const QByteArray &data,
                       QString &outTranslated,
                       TranslateError &outError,
                       QString &outDetail) override;

private:
    /**
     * @brief 将通用语言代码转换为 LibreTranslate 语言代码
     * @param code 通用语言代码
     * @return LibreTranslate 语言代码（如 zh、zt）
     * @author chiangyang
     */
    static QString toLibreLang(const QString &code);

    QString m_url; ///< 服务地址
};

#endif // LIBRE_TRANSLATE_ENGINE_H
