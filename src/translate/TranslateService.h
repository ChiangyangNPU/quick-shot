#ifndef TRANSLATE_SERVICE_H
#define TRANSLATE_SERVICE_H

#include "TranslateEngine.h"
#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QRect>

class QSettings;
class QWidget;

/**
 * @brief 翻译服务单例
 *
 * 管理所有翻译引擎实例，负责加载配置、切换当前引擎、转发翻译请求与结果。
 * 单例首次访问时自动从 ConfigManager 加载 translate/ 配置。
 * 注意：本类负责「文本翻译」，与 UI 多语言翻译的 TranslationManager 区分。
 * @author chiangyang
 */
class TranslateService : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return TranslateService 唯一实例
     * @author chiangyang
     */
    static TranslateService *instance();

    /**
     * @brief 检查翻译功能是否启用并处理首次隐私提示
     *
     * 统一的翻译前置检查：读取 translate/enabled 配置判断功能是否启用，
     * 若启用且首次使用则弹出隐私提示对话框。供 SnipScreen、PinWindow、
     * OcrResultDialog 等所有翻译入口复用，消除重复代码。
     * @param parent 父窗口（用于定位隐私提示弹窗），可为 nullptr
     * @param centerRect 弹窗居中区域（全局坐标），为空则居中在 parent
     * @return true 表示可以继续翻译，false 表示功能未启用或用户拒绝
     * @author chiangyang
     */
    static bool checkEnabledAndPrivacy(QWidget *parent = nullptr,
                                       const QRect &centerRect = QRect());

    /**
     * @brief 将翻译错误码转换为本地化提示消息
     *
     * 统一的错误码→本地化文案映射，供所有翻译失败处理逻辑复用。
     * @param code 翻译错误码
     * @return 本地化的错误消息
     * @author chiangyang
     */
    static QString errorMessage(TranslateEngine::TranslateError code);

    /**
     * @brief 从 QSettings 加载翻译配置并重建引擎
     * @param settings 配置对象
     * @author chiangyang
     */
    void loadConfig(QSettings *settings);

    /**
     * @brief 设置当前引擎
     * @param name 引擎名称
     * @author chiangyang
     */
    void setCurrentEngine(const QString &name);

    /**
     * @brief 获取当前引擎名称
     * @return 引擎名称
     * @author chiangyang
     */
    QString currentEngineName() const { return m_currentEngineName; }

    /**
     * @brief 获取当前引擎
     * @return 当前引擎指针
     * @author chiangyang
     */
    TranslateEngine *currentEngine() const { return m_currentEngine; }

    /**
     * @brief 获取所有已注册引擎名称
     * @return 引擎名称列表
     * @author chiangyang
     */
    QStringList availableEngineNames() const { return m_engines.keys(); }

    /**
     * @brief 获取目标语言
     * @return 目标语言代码
     * @author chiangyang
     */
    QString targetLang() const { return m_targetLang; }

    /**
     * @brief 获取源语言
     * @return 源语言代码
     * @author chiangyang
     */
    QString sourceLang() const { return m_sourceLang; }

public slots:
    /**
     * @brief 翻译文本（使用当前引擎）
     * @param text 源文本
     * @param sourceLang 源语言代码，空串则使用配置默认值
     * @param targetLang 目标语言代码，空串则使用配置默认值
     * @author chiangyang
     */
    void translate(const QString &text,
                   const QString &sourceLang = QString(),
                   const QString &targetLang = QString());

    /**
     * @brief 批量翻译多段文本（逐段顺序翻译，全部完成后发 batchFinished）
     * @param texts 源文本列表
     * @param sourceLang 源语言代码，空串则用配置默认值
     * @param targetLang 目标语言代码，空串则用配置默认值
     * @author chiangyang
     */
    void translateBatch(const QStringList &texts,
                        const QString &sourceLang = QString(),
                        const QString &targetLang = QString());

signals:
    /**
     * @brief 翻译完成信号
     * @param original 原文
     * @param translated 译文
     * @author chiangyang
     */
    void finished(const QString &original, const QString &translated);

    /**
     * @brief 翻译失败信号
     * @param code 错误分类码，UI 据此查本地化文案
     * @param detail 原始技术细节，可空
     * @author chiangyang
     */
    void failed(TranslateEngine::TranslateError code, const QString &detail = QString());

    /**
     * @brief 批量翻译完成信号
     * @param translatedTexts 译文列表（与请求顺序一一对应，失败段回退为原文）
     * @author chiangyang
     */
    void batchFinished(const QStringList &translatedTexts);

    /**
     * @brief 当前引擎改变信号
     * @param name 新引擎名称
     * @author chiangyang
     */
    void currentEngineChanged(const QString &name);

private:
    /**
     * @brief 构造函数（私有，单例）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit TranslateService(QObject *parent = nullptr);

    /**
     * @brief 注册引擎到管理器并连接信号
     * @param engine 引擎实例
     * @author chiangyang
     */
    void registerEngine(TranslateEngine *engine);

    /**
     * @brief 清理所有引擎实例
     * @author chiangyang
     */
    void clearEngines();

private slots:
    /**
     * @brief 引擎翻译完成槽函数
     * @param original 原文
     * @param translated 译文
     * @author chiangyang
     */
    void onEngineFinished(const QString &original, const QString &translated);

    /**
     * @brief 引擎翻译失败槽函数
     * @param code 错误分类码
     * @param detail 原始技术细节
     * @author chiangyang
     */
    void onEngineFailed(TranslateEngine::TranslateError code, const QString &detail);

    /**
     * @brief 翻译批量中的下一段
     * @author chiangyang
     */
    void translateNextBatchItem();

    /**
     * @brief 批量中单段完成处理
     * @author chiangyang
     */
    void onBatchItemFinished();

private:
    QMap<QString, TranslateEngine *> m_engines; ///< 引擎映射表
    TranslateEngine *m_currentEngine = nullptr; ///< 当前引擎
    QString m_currentEngineName;                ///< 当前引擎名称

    QString m_targetLang;      ///< 目标语言代码
    QString m_sourceLang;      ///< 源语言代码
    QString m_mymemoryEmail;   ///< MyMemory 邮箱
    QString m_baiduAppId;      ///< 百度 AppID
    QString m_baiduKey;        ///< 百度密钥
    QString m_deeplKey;        ///< DeepL Key
    QString m_libreUrl;        ///< LibreTranslate 地址

    // 批量翻译状态
    QStringList m_batchOriginals;      ///< 批量翻译原文列表
    QStringList m_batchTranslated;     ///< 批量翻译结果
    int m_batchTotal = 0;              ///< 批量总段数
    int m_batchIndex = 0;              ///< 批量当前索引
    bool m_batchActive = false;        ///< 是否正在批量翻译
    QString m_batchSourceLang;         ///< 批量源语言
    QString m_batchTargetLang;         ///< 批量目标语言

    static TranslateService *s_instance; ///< 单例实例
};

#endif // TRANSLATE_SERVICE_H
