#ifndef MYMEMORY_ENGINE_H
#define MYMEMORY_ENGINE_H

#include "TranslateEngine.h"
#include <QString>

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

protected:
    /**
     * @brief 发送 GET 请求（langpair + 可选 email）
     * @author chiangyang
     */
    QNetworkReply *sendRequest(const QString &text,
                               const QString &sourceLang,
                               const QString &targetLang) override;

    /**
     * @brief 解析 MyMemory JSON 响应（responseStatus/responseData/responseDetails）
     * @author chiangyang
     */
    bool parseResponse(const QByteArray &data,
                       QString &outTranslated,
                       TranslateError &outError,
                       QString &outDetail) override;

private:
    QString m_email; ///< 联系邮箱（可选）
};

#endif // MYMEMORY_ENGINE_H
