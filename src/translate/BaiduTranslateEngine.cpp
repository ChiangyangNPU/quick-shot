#include "BaiduTranslateEngine.h"
#include "../log/Logger.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QDateTime>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
BaiduTranslateEngine::BaiduTranslateEngine(QObject *parent)
    : TranslateEngine(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

/**
 * @brief 引擎是否可用
 * @return AppID 与密钥均非空时返回 true
 * @author chiangyang
 */
bool BaiduTranslateEngine::isAvailable() const {
    return !m_appId.isEmpty() && !m_key.isEmpty();
}

/**
 * @brief 将通用语言代码转换为百度语言代码
 * @param code 通用语言代码
 * @return 百度语言代码
 * @author chiangyang
 */
QString BaiduTranslateEngine::toBaiduLang(const QString &code) {
    if (code == "zh-CN") return "zh";
    if (code == "zh-TW") return "cht";
    if (code == "ja") return "jp";
    if (code == "ko") return "kor";
    if (code == "fr") return "fra";
    if (code == "es") return "spa";
    // en / de / ru / pt 与通用代码一致
    return code;
}

/**
 * @brief 异步翻译文本
 * @param text 源文本
 * @param sourceLang 源语言代码，支持 "auto"
 * @param targetLang 目标语言代码
 * @author chiangyang
 */
void BaiduTranslateEngine::translate(const QString &text,
                                     const QString &sourceLang,
                                     const QString &targetLang) {
    if (text.isEmpty()) {
        emit failed(TranslateError::EmptyText, "Empty text");
        return;
    }
    if (!isAvailable()) {
        emit failed(TranslateError::NotConfigured, "Baidu AppID/Key not configured");
        return;
    }

    QString from = (sourceLang == "auto" || sourceLang.isEmpty()) ? "auto" : toBaiduLang(sourceLang);
    QString to = toBaiduLang(targetLang);
    QString salt = QString::number(QDateTime::currentMSecsSinceEpoch());
    // 签名 = MD5(appid + q + salt + key)
    QString signStr = m_appId + text + salt + m_key;
    QByteArray sign = QCryptographicHash::hash(signStr.toUtf8(), QCryptographicHash::Md5).toHex();

    QUrl url("https://fanyi-api.baidu.com/api/trans/vip/translate");
    QUrlQuery query;
    query.addQueryItem("q", text);
    query.addQueryItem("from", from);
    query.addQueryItem("to", to);
    query.addQueryItem("appid", m_appId);
    query.addQueryItem("salt", salt);
    query.addQueryItem("sign", QString::fromUtf8(sign));
    url.setQuery(query);

    m_pendingOriginal = text;

    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &BaiduTranslateEngine::onReplyFinished);

    LOG_INFO(QString("BaiduTranslateEngine: request sent, from=%1 to=%2 length=%3")
                 .arg(from, to).arg(text.length()));
}

/**
 * @brief 网络回复完成槽函数，解析 JSON 并发出结果信号
 * @author chiangyang
 */
void BaiduTranslateEngine::onReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        LOG_INFO(QString("BaiduTranslateEngine: network error: %1").arg(errStr));
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

    if (obj.contains("error_code")) {
        QString code = obj.value("error_code").toString();
        QString msg = obj.value("error_msg").toString();
        LOG_INFO(QString("BaiduTranslateEngine: API error %1: %2").arg(code, msg));
        emit failed(TranslateError::ApiError, QString("Baidu %1: %2").arg(code, msg));
        return;
    }

    QJsonArray results = obj.value("trans_result").toArray();
    QStringList lines;
    for (const QJsonValue &v : results) {
        lines.append(v.toObject().value("dst").toString());
    }
    QString translated = lines.join("\n");

    if (translated.isEmpty()) {
        emit failed(TranslateError::ApiError, "Empty translation result");
        return;
    }

    LOG_INFO("BaiduTranslateEngine: translation succeeded");
    emit finished(m_pendingOriginal, translated);
}
