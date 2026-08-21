#ifndef BAIDU_TRANSLATE_ENGINE_H
#define BAIDU_TRANSLATE_ENGINE_H

#include "TranslateEngine.h"
#include <QNetworkAccessManager>
#include <QString>

class QNetworkReply;

/**
 * @brief 百度翻译引擎
 *
 * 通过百度翻译开放平台 API 进行翻译，需要用户自填 AppID 与密钥。
 * 支持 from=auto 自动检测源语言。国内稳定，有免费额度。
 * @author chiangyang
 */
class BaiduTranslateEngine : public TranslateEngine {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit BaiduTranslateEngine(QObject *parent = nullptr);

    /**
     * @brief 获取引擎名称
     * @return "baidu"
     * @author chiangyang
     */
    QString name() const override { return "baidu"; }

    /**
     * @brief 引擎是否可用（AppID 与密钥均已配置）
     * @return 是否可用
     * @author chiangyang
     */
    bool isAvailable() const override;

    /**
     * @brief 是否需要 API Key
     * @return 百度需要 AppID + 密钥，返回 true
     * @author chiangyang
     */
    bool requiresApiKey() const override { return true; }

    /**
     * @brief 设置百度 AppID
     * @param id AppID
     * @author chiangyang
     */
    void setAppId(const QString &id) { m_appId = id; }

    /**
     * @brief 设置百度密钥
     * @param key 密钥
     * @author chiangyang
     */
    void setKey(const QString &key) { m_key = key; }

public slots:
    /**
     * @brief 异步翻译文本
     * @param text 源文本
     * @param sourceLang 源语言代码，支持 "auto"
     * @param targetLang 目标语言代码
     * @author chiangyang
     */
    void translate(const QString &text,
                   const QString &sourceLang,
                   const QString &targetLang) override;

private slots:
    /**
     * @brief 网络回复完成槽函数
     * @author chiangyang
     */
    void onReplyFinished();

private:
    /**
     * @brief 将通用语言代码转换为百度语言代码
     * @param code 通用语言代码（如 zh-CN、ja）
     * @return 百度语言代码（如 zh、jp）
     * @author chiangyang
     */
    static QString toBaiduLang(const QString &code);

    QNetworkAccessManager *m_networkManager; ///< 网络管理器
    QString m_appId;                         ///< 百度 AppID
    QString m_key;                           ///< 百度密钥
    QString m_pendingOriginal;               ///< 当前待翻译的原文
};

#endif // BAIDU_TRANSLATE_ENGINE_H
