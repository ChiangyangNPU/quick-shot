#ifndef MYMEMORY_ENGINE_H
#define MYMEMORY_ENGINE_H

#include "TranslateEngine.h"
#include <QNetworkAccessManager>
#include <QString>

class QNetworkReply;

/**
 * @brief MyMemory 翻译引擎
 *
 * 默认免注册引擎，通过 MyMemory 免费 API 进行翻译。
 * 服务器在欧洲，国内可直连，无需翻墙。
 * 无 email 时每日额度 5000 词；填写 email 后提升至 50000 词/天。
 * @author chiangyang
 */
class MyMemoryEngine : public TranslateEngine {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit MyMemoryEngine(QObject *parent = nullptr);

    /**
     * @brief 获取引擎名称
     * @return "mymemory"
     * @author chiangyang
     */
    QString name() const override { return "mymemory"; }

    /**
     * @brief 引擎是否可用
     * @return MyMemory 免注册始终可用，返回 true
     * @author chiangyang
     */
    bool isAvailable() const override { return true; }

    /**
     * @brief 是否需要 API Key
     * @return MyMemory 免注册，返回 false（email 为可选项）
     * @author chiangyang
     */
    bool requiresApiKey() const override { return false; }

    /**
     * @brief 设置联系邮箱（可选，用于提升免费额度）
     * @param email 邮箱地址
     * @author chiangyang
     */
    void setEmail(const QString &email) { m_email = email; }

public slots:
    /**
     * @brief 异步翻译文本
     * @param text 源文本
     * @param sourceLang 源语言代码，"auto" 时按英文兜底
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
    QNetworkAccessManager *m_networkManager; ///< 网络管理器
    QString m_email;                         ///< 联系邮箱（可选）
    QString m_pendingOriginal;               ///< 当前待翻译的原文（用于结果回传）
};

#endif // MYMEMORY_ENGINE_H
