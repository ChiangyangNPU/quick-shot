#include "BaiduTranslateEngine.h"
#include "../log/Logger.h"

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
    : TranslateEngine(parent) {
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
 * @brief 构造并发送百度 GET 请求（签名 = MD5(appid + q + salt + key)）
 * @author chiangyang
 */
QNetworkReply *BaiduTranslateEngine::sendRequest(const QString &text,
                                                 const QString &sourceLang,
                                                 const QString &targetLang) {
    QString from = (sourceLang == "auto" || sourceLang.isEmpty()) ? "auto" : toBaiduLang(sourceLang);
    QString to = toBaiduLang(targetLang);
    QString salt = QString::number(QDateTime::currentMSecsSinceEpoch());
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

    LOG_INFO(QString("BaiduTranslateEngine: request sent, from=%1 to=%2 length=%3")
                 .arg(from, to).arg(text.length()));
    return httpGet(url);
}

/**
 * @brief 解析百度 JSON 响应
 * @author chiangyang
 */
bool BaiduTranslateEngine::parseResponse(const QByteArray &data,
                                         QString &outTranslated,
                                         TranslateError &outError,
                                         QString &outDetail) {
    QJsonObject obj = QJsonDocument::fromJson(data).object();

    if (obj.contains("error_code")) {
        QString code = obj.value("error_code").toString();
        QString msg = obj.value("error_msg").toString();
        LOG_INFO(QString("BaiduTranslateEngine: API error %1: %2").arg(code, msg));
        outError = TranslateError::ApiError;
        outDetail = QString("Baidu %1: %2").arg(code, msg);
        return false;
    }

    QJsonArray results = obj.value("trans_result").toArray();
    QStringList lines;
    for (const QJsonValue &v : results) {
        lines.append(v.toObject().value("dst").toString());
    }
    outTranslated = lines.join("\n");
    return true;
}
