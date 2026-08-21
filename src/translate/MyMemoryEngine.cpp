#include "MyMemoryEngine.h"
#include "../log/Logger.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
MyMemoryEngine::MyMemoryEngine(QObject *parent)
    : TranslateEngine(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

/**
 * @brief 异步翻译文本
 * @param text 源文本
 * @param sourceLang 源语言代码，"auto" 时使用 Autodetect 自动检测
 * @param targetLang 目标语言代码
 * @author chiangyang
 */
void MyMemoryEngine::translate(const QString &text,
                               const QString &sourceLang,
                               const QString &targetLang) {
    if (text.isEmpty()) {
        emit failed(TranslateError::EmptyText, "Empty text");
        return;
    }

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

    m_pendingOriginal = text;

    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &MyMemoryEngine::onReplyFinished);

    LOG_INFO(QString("MyMemoryEngine: request sent, src=%1 tgt=%2 length=%3")
                 .arg(src, targetLang).arg(text.length()));
}

/**
 * @brief 网络回复完成槽函数，解析 JSON 并发出结果信号
 * @author chiangyang
 */
void MyMemoryEngine::onReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        LOG_INFO(QString("MyMemoryEngine: network error: %1").arg(errStr));
        // SSL/TLS 握手或初始化失败单独分类，提示用户部署 TLS 后端插件
        if (reply->error() == QNetworkReply::SslHandshakeFailedError
            || errStr.contains("SSL", Qt::CaseInsensitive)) {
            emit failed(TranslateError::SslFailed, errStr);
        } else {
            emit failed(TranslateError::NetworkFailed, errStr);
        }
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    int status = obj.value("responseStatus").toInt();
    QString translated = obj.value("responseData").toObject().value("translatedText").toString();

    if (status != 200 || translated.isEmpty()) {
        QString detail = obj.value("responseDetails").toString();
        // 源语言与目标语言相同时，MyMemory 返回 403 提示选择两个不同语言
        if (status == 403 || detail.contains("DISTINCT LANGUAGES", Qt::CaseInsensitive)) {
            LOG_INFO("MyMemoryEngine: source language matches target language");
            emit failed(TranslateError::SameLanguage, detail);
            return;
        }
        // 额度用尽：MyMemory 返回 quota/limit 相关提示
        if (detail.contains("QUOTA", Qt::CaseInsensitive)
            || detail.contains("LIMIT", Qt::CaseInsensitive)) {
            LOG_INFO(QString("MyMemoryEngine: rate limit, detail=%1").arg(detail));
            emit failed(TranslateError::RateLimit, detail);
            return;
        }
        LOG_INFO(QString("MyMemoryEngine: API error, status=%1 detail=%2")
                     .arg(status).arg(detail));
        emit failed(TranslateError::ApiError, detail.isEmpty() ? "Translation API error" : detail);
        return;
    }

    LOG_INFO("MyMemoryEngine: translation succeeded");
    emit finished(m_pendingOriginal, translated);
}
