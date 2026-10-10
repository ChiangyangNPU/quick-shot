#include "LibreTranslateEngine.h"
#include "../log/Logger.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
LibreTranslateEngine::LibreTranslateEngine(QObject *parent)
    : TranslateEngine(parent) {
}

/**
 * @brief 引擎是否可用
 * @return 服务地址非空时返回 true
 * @author chiangyang
 */
bool LibreTranslateEngine::isAvailable() const {
    return !m_url.isEmpty();
}

/**
 * @brief 将通用语言代码转换为 LibreTranslate 语言代码
 * @param code 通用语言代码
 * @return LibreTranslate 语言代码
 * @author chiangyang
 */
QString LibreTranslateEngine::toLibreLang(const QString &code) {
    // LibreTranslate 用 "zt" 表示繁体中文，映射成 "zh" 会导致繁体退化为简体
    if (code == "zh-CN") return "zh";
    if (code == "zh-TW" || code == "zh-HK") return "zt";
    return code;
}

/**
 * @brief 构造并发送 LibreTranslate POST 请求（JSON 报文）
 * @author chiangyang
 */
QNetworkReply *LibreTranslateEngine::sendRequest(const QString &text,
                                                 const QString &sourceLang,
                                                 const QString &targetLang) {
    // 规范化地址：确保以 /translate 结尾
    QString base = m_url;
    if (!base.endsWith("/translate")) {
        if (base.endsWith("/")) base.chop(1);
        base += "/translate";
    }

    QUrl url(base);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["q"] = text;
    body["source"] = (sourceLang == "auto" || sourceLang.isEmpty()) ? "auto" : toLibreLang(sourceLang);
    body["target"] = toLibreLang(targetLang);
    body["format"] = "text";

    LOG_INFO(QString("LibreTranslateEngine: request sent, target=%1 length=%2")
                 .arg(toLibreLang(targetLang)).arg(text.length()));
    return httpPost(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

/**
 * @brief 解析 LibreTranslate JSON 响应
 * @author chiangyang
 */
bool LibreTranslateEngine::parseResponse(const QByteArray &data,
                                         QString &outTranslated,
                                         TranslateError &outError,
                                         QString &outDetail) {
    QJsonObject obj = QJsonDocument::fromJson(data).object();

    if (obj.contains("error")) {
        QString msg = obj.value("error").toString();
        LOG_INFO(QString("LibreTranslateEngine: API error: %1").arg(msg));
        outError = TranslateError::ApiError;
        outDetail = msg;
        return false;
    }

    outTranslated = obj.value("translatedText").toString();
    return true;
}
