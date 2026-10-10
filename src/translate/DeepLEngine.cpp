#include "DeepLEngine.h"
#include "../log/Logger.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
DeepLEngine::DeepLEngine(QObject *parent)
    : TranslateEngine(parent) {
}

/**
 * @brief 引擎是否可用
 * @return Key 非空时返回 true
 * @author chiangyang
 */
bool DeepLEngine::isAvailable() const {
    return !m_key.isEmpty();
}

/**
 * @brief 将通用语言代码转换为 DeepL 语言代码
 * @param code 通用语言代码
 * @return DeepL 语言代码（大写）
 * @author chiangyang
 */
QString DeepLEngine::toDeepLLang(const QString &code) {
    if (code == "zh-CN" || code == "zh-TW") return "ZH";
    if (code == "pt") return "PT-PT";
    return code.toUpper();
}

/**
 * @brief 构造并发送 DeepL POST 请求（表单编码 + DeepL-Auth-Key 鉴权）
 * @author chiangyang
 */
QNetworkReply *DeepLEngine::sendRequest(const QString &text,
                                        const QString &sourceLang,
                                        const QString &targetLang) {
    QUrl url("https://api-free.deepl.com/v2/translate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Authorization", ("DeepL-Auth-Key " + m_key).toUtf8());

    QUrlQuery query;
    query.addQueryItem("text", text);
    query.addQueryItem("target_lang", toDeepLLang(targetLang));
    // source_lang 可选，不传则 DeepL 自动检测
    if (sourceLang != "auto" && !sourceLang.isEmpty()) {
        query.addQueryItem("source_lang", toDeepLLang(sourceLang));
    }
    QByteArray body = query.toString(QUrl::FullyEncoded).toUtf8();

    LOG_INFO(QString("DeepLEngine: request sent, target=%1 length=%2")
                 .arg(toDeepLLang(targetLang)).arg(text.length()));
    return httpPost(request, body);
}

/**
 * @brief 解析 DeepL JSON 响应
 * @author chiangyang
 */
bool DeepLEngine::parseResponse(const QByteArray &data,
                                QString &outTranslated,
                                TranslateError &outError,
                                QString &outDetail) {
    QJsonArray translations = QJsonDocument::fromJson(data).object().value("translations").toArray();
    QStringList lines;
    for (const QJsonValue &v : translations) {
        lines.append(v.toObject().value("text").toString());
    }
    outTranslated = lines.join("\n");
    return true;
}
