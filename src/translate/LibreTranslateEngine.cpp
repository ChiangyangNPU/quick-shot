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
    : TranslateEngine(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
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
    if (code == "zh-CN" || code == "zh-TW") return "zh";
    return code;
}

/**
 * @brief 异步翻译文本
 * @param text 源文本
 * @param sourceLang 源语言代码，支持 "auto"
 * @param targetLang 目标语言代码
 * @author chiangyang
 */
void LibreTranslateEngine::translate(const QString &text,
                                     const QString &sourceLang,
                                     const QString &targetLang) {
    if (text.isEmpty()) {
        emit failed(TranslateError::EmptyText, "Empty text");
        return;
    }
    if (!isAvailable()) {
        emit failed(TranslateError::NotConfigured, "LibreTranslate URL not configured");
        return;
    }

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

    m_pendingOriginal = text;

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, &LibreTranslateEngine::onReplyFinished);

    LOG_INFO(QString("LibreTranslateEngine: request sent, target=%1 length=%2")
                 .arg(toLibreLang(targetLang)).arg(text.length()));
}

/**
 * @brief 网络回复完成槽函数，解析 JSON 并发出结果信号
 * @author chiangyang
 */
void LibreTranslateEngine::onReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        LOG_INFO(QString("LibreTranslateEngine: network error: %1").arg(errStr));
        if (reply->error() == QNetworkReply::SslHandshakeFailedError
            || errStr.contains("SSL", Qt::CaseInsensitive)) {
            emit failed(TranslateError::SslFailed, errStr);
        } else {
            emit failed(TranslateError::NetworkFailed, errStr);
        }
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();

    if (obj.contains("error")) {
        QString msg = obj.value("error").toString();
        LOG_INFO(QString("LibreTranslateEngine: API error: %1").arg(msg));
        emit failed(TranslateError::ApiError, msg);
        return;
    }

    QString translated = obj.value("translatedText").toString();
    if (translated.isEmpty()) {
        emit failed(TranslateError::ApiError, "Empty translation result");
        return;
    }

    LOG_INFO("LibreTranslateEngine: translation succeeded");
    emit finished(m_pendingOriginal, translated);
}
