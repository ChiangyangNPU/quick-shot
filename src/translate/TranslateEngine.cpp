#include "TranslateEngine.h"
#include "../log/Logger.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

/**
 * @brief 构造函数：创建引擎内共享的网络管理器
 * @param parent 父对象
 * @author chiangyang
 */
TranslateEngine::TranslateEngine(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

/**
 * @brief 翻译模板方法：前置检查 → 子类发请求 → 基类统一回传
 * @param text 源文本
 * @param sourceLang 源语言代码
 * @param targetLang 目标语言代码
 * @author chiangyang
 */
void TranslateEngine::translate(const QString &text,
                                const QString &sourceLang,
                                const QString &targetLang) {
    if (text.isEmpty()) {
        emit failed(TranslateError::EmptyText, "Empty text");
        return;
    }
    if (!isAvailable()) {
        emit failed(TranslateError::NotConfigured,
                    QString("Engine '%1' is not available or not configured").arg(name()));
        return;
    }

    m_pendingOriginal = text;
    QNetworkReply *reply = sendRequest(text, sourceLang, targetLang);
    if (!reply) {
        // 子类已自行 emit failed 并记录日志
        return;
    }
    connect(reply, &QNetworkReply::finished, this, &TranslateEngine::onReplyFinished);
}

/**
 * @brief 网络回复统一处理：网络/SSL 错误分类 → 子类解析 → 空译文兜底 → 发出结果
 * @author chiangyang
 */
void TranslateEngine::onReplyFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        LOG_INFO(QString("%1: network error: %2").arg(name(), errStr));
        // SSL/TLS 握手或初始化失败单独分类，提示用户部署 TLS 后端插件
        if (reply->error() == QNetworkReply::SslHandshakeFailedError
            || errStr.contains("SSL", Qt::CaseInsensitive)) {
            emit failed(TranslateError::SslFailed, errStr);
        } else {
            emit failed(TranslateError::NetworkFailed, errStr);
        }
        return;
    }

    QString translated;
    TranslateError error = TranslateError::Unknown;
    QString detail;
    if (!parseResponse(reply->readAll(), translated, error, detail)) {
        emit failed(error, detail);
        return;
    }
    if (translated.isEmpty()) {
        emit failed(TranslateError::ApiError, "Empty translation result");
        return;
    }

    LOG_INFO(QString("%1: translation succeeded").arg(name()));
    emit finished(m_pendingOriginal, translated);
}

/**
 * @brief 经引擎共享的网络管理器发起 GET 请求
 * @author chiangyang
 */
QNetworkReply *TranslateEngine::httpGet(const QUrl &url) {
    return m_networkManager->get(QNetworkRequest(url));
}

/**
 * @brief 经引擎共享的网络管理器发起 POST 请求
 * @author chiangyang
 */
QNetworkReply *TranslateEngine::httpPost(const QNetworkRequest &request, const QByteArray &body) {
    return m_networkManager->post(request, body);
}
