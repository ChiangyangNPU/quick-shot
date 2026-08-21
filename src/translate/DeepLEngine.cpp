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
    : TranslateEngine(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
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
 * @brief 异步翻译文本
 * @param text 源文本
 * @param sourceLang 源语言代码，"auto" 时不传 source_lang
 * @param targetLang 目标语言代码
 * @author chiangyang
 */
void DeepLEngine::translate(const QString &text,
                            const QString &sourceLang,
                            const QString &targetLang) {
    if (text.isEmpty()) {
        emit failed(TranslateError::EmptyText, "Empty text");
        return;
    }
    if (!isAvailable()) {
        emit failed(TranslateError::NotConfigured, "DeepL Key not configured");
        return;
    }

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

    m_pendingOriginal = text;

    QNetworkReply *reply = m_networkManager->post(request, body);
    connect(reply, &QNetworkReply::finished, this, &DeepLEngine::onReplyFinished);

    LOG_INFO(QString("DeepLEngine: request sent, target=%1 length=%2")
                 .arg(toDeepLLang(targetLang)).arg(text.length()));
}

/**
 * @brief 网络回复完成槽函数，解析 JSON 并发出结果信号
 * @author chiangyang
 */
void DeepLEngine::onReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        LOG_INFO(QString("DeepLEngine: network error: %1").arg(errStr));
        if (reply->error() == QNetworkReply::SslHandshakeFailedError
            || errStr.contains("SSL", Qt::CaseInsensitive)) {
            emit failed(TranslateError::SslFailed, errStr);
        } else {
            emit failed(TranslateError::NetworkFailed, errStr);
        }
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray translations = doc.object().value("translations").toArray();
    QStringList lines;
    for (const QJsonValue &v : translations) {
        lines.append(v.toObject().value("text").toString());
    }
    QString translated = lines.join("\n");

    if (translated.isEmpty()) {
        emit failed(TranslateError::ApiError, "Empty translation result");
        return;
    }

    LOG_INFO("DeepLEngine: translation succeeded");
    emit finished(m_pendingOriginal, translated);
}
