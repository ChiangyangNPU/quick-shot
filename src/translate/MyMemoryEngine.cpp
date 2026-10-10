#include "MyMemoryEngine.h"
#include "../log/Logger.h"

#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
MyMemoryEngine::MyMemoryEngine(QObject *parent)
    : TranslateEngine(parent) {
}

/**
 * @brief 构造并发送 MyMemory GET 请求
 * @author chiangyang
 */
QNetworkReply *MyMemoryEngine::sendRequest(const QString &text,
                                           const QString &sourceLang,
                                           const QString &targetLang) {
    // MyMemory 支持 "Autodetect" 作为源语言，可自动检测源语言
    QString src = (sourceLang == "auto" || sourceLang.isEmpty()) ? "Autodetect" : sourceLang;

    QUrl url("https://api.mymemory.translated.net/get");
    QUrlQuery query;
    query.addQueryItem("q", text);
    query.addQueryItem("langpair", src + "|" + targetLang);
    if (!m_email.isEmpty()) {
        query.addQueryItem("de", m_email);
    }
    url.setQuery(query);

    LOG_INFO(QString("MyMemoryEngine: request sent, src=%1 tgt=%2 length=%3")
                 .arg(src, targetLang).arg(text.length()));
    return httpGet(url);
}

/**
 * @brief 解析 MyMemory JSON 响应
 * @author chiangyang
 */
bool MyMemoryEngine::parseResponse(const QByteArray &data,
                                   QString &outTranslated,
                                   TranslateError &outError,
                                   QString &outDetail) {
    QJsonObject obj = QJsonDocument::fromJson(data).object();

    int status = obj.value("responseStatus").toInt();
    outTranslated = obj.value("responseData").toObject().value("translatedText").toString();

    if (status == 200 && !outTranslated.isEmpty()) {
        return true;
    }

    QString detail = obj.value("responseDetails").toString();
    // 先按响应内容分类：MyMemory 的额度耗尽与同语言错误都可能伴随 403 状态码，
    // 若先判状态码会把"额度用尽"误报成"语言相同"
    // 同语言：MyMemory 返回 "PLEASE SELECT TWO DISTINCT LANGUAGES"
    if (detail.contains("DISTINCT LANGUAGES", Qt::CaseInsensitive)) {
        LOG_INFO("MyMemoryEngine: source language matches target language");
        outError = TranslateError::SameLanguage;
        outDetail = detail;
        return false;
    }
    // 额度用尽：quota / limit / 今日免费额度提示
    if (detail.contains("QUOTA", Qt::CaseInsensitive)
        || detail.contains("LIMIT", Qt::CaseInsensitive)
        || detail.contains("TRANSLATIONS FOR TODAY", Qt::CaseInsensitive)) {
        LOG_INFO(QString("MyMemoryEngine: rate limit, detail=%1").arg(detail));
        outError = TranslateError::RateLimit;
        outDetail = detail;
        return false;
    }
    LOG_INFO(QString("MyMemoryEngine: API error, status=%1 detail=%2")
                 .arg(status).arg(detail));
    outError = TranslateError::ApiError;
    outDetail = detail.isEmpty() ? "Translation API error" : detail;
    return false;
}
