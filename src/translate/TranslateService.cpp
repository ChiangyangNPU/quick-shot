#include "TranslateService.h"
#include "MyMemoryEngine.h"
#include "BaiduTranslateEngine.h"
#include "DeepLEngine.h"
#include "LibreTranslateEngine.h"
#include "ConfigManager.h"
#include "../core/TranslationManager.h"
#include "../core/StyleManager.h"
#include "../log/Logger.h"

#include <QSettings>
#include "../widgets/MessageBox.h"
#include <QCheckBox>
#include <QPushButton>
#include <QWidget>
#include <QRect>

// 初始化静态成员
TranslateService *TranslateService::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @return TranslateService 唯一实例
 * @author chiangyang
 */
TranslateService *TranslateService::instance() {
    if (!s_instance) {
        s_instance = new TranslateService();
    }
    return s_instance;
}

/**
 * @brief 检查翻译功能是否启用并处理首次隐私提示
 * @param parent 父窗口（用于定位隐私提示弹窗）
 * @return true 表示可以继续翻译，false 表示功能未启用或用户拒绝
 * @author chiangyang
 */
bool TranslateService::checkEnabledAndPrivacy(QWidget *parent, const QRect &centerRect) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    bool enabled = settings->value("translate/enabled", true).toBool();
    if (!enabled) {
        return false;
    }

    bool showWarning = settings->value("translate/showPrivacyWarning", true).toBool();
    if (!showWarning) {
        return true;
    }

    // 显示首次翻译隐私提示
    TranslationManager *tm = TranslationManager::instance();

    MessageBox msgBox(parent);
    msgBox.setContent(tm->get("translate.privacyTitle", "Translation Privacy Notice"),
                      tm->get("translate.privacyMsg",
                              "Translation sends recognized text to a third-party service. Continue?"));

    QCheckBox *dontAsk = new QCheckBox(tm->get("translate.privacyDontAsk", "Don't ask again"), &msgBox);
    msgBox.setCheckBox(dontAsk);

    QPushButton *yesButton = nullptr;
    msgBox.addYesNoButtons(&yesButton, nullptr, true);

    // 弹窗居中定位：优先使用 centerRect（如截图选区），否则居中在 parent 窗口
    QRect targetRect = centerRect.isValid() ? centerRect :
                       (parent ? parent->frameGeometry() : QRect());
    if (targetRect.isValid()) {
        msgBox.centerOn(targetRect);
    }

    msgBox.exec();

    if (msgBox.clickedButton() == yesButton) {
        if (dontAsk->isChecked()) {
            settings->setValue("translate/showPrivacyWarning", false);
        }
        return true;
    }
    return false;
}

/**
 * @brief 将翻译错误码转换为本地化提示消息
 * @param code 翻译错误码
 * @return 本地化的错误消息
 * @author chiangyang
 */
QString TranslateService::errorMessage(TranslateEngine::TranslateError code) {
    TranslationManager *tm = TranslationManager::instance();
    switch (code) {
    case TranslateEngine::TranslateError::NetworkFailed:
        return tm->get("translate.errNetwork");
    case TranslateEngine::TranslateError::SslFailed:
        return tm->get("translate.errSsl");
    case TranslateEngine::TranslateError::SameLanguage:
        return tm->get("translate.errSameLang");
    case TranslateEngine::TranslateError::RateLimit:
        return tm->get("translate.errRateLimit");
    case TranslateEngine::TranslateError::NotConfigured:
        return tm->get("translate.errNotConfigured");
    case TranslateEngine::TranslateError::ApiError:
        return tm->get("translate.errApi");
    case TranslateEngine::TranslateError::EmptyText:
        return tm->get("translate.errEmpty");
    default:
        return tm->get("translate.errUnknown");
    }
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
TranslateService::TranslateService(QObject *parent)
    : QObject(parent) {
    // 单例首次访问时自动从 ConfigManager 加载配置
    QSettings *settings = ConfigManager::instance()->getSettings();
    loadConfig(settings);
}

/**
 * @brief 从 QSettings 加载翻译配置并重建引擎
 * @param settings 配置对象
 * @author chiangyang
 */
void TranslateService::loadConfig(QSettings *settings) {
    if (!settings) {
        return;
    }

    m_targetLang = settings->value("translate/targetLang", "en").toString();
    m_sourceLang = settings->value("translate/sourceLang", "auto").toString();
    m_mymemoryEmail = settings->value("translate/mymemoryEmail", "").toString();
    m_baiduAppId = settings->value("translate/baiduAppId", "").toString();
    m_baiduKey = settings->value("translate/baiduKey", "").toString();
    m_deeplKey = settings->value("translate/deeplKey", "").toString();
    m_libreUrl = settings->value("translate/libreUrl", "").toString();

    // 重建所有引擎实例
    clearEngines();

    MyMemoryEngine *mm = new MyMemoryEngine(this);
    mm->setEmail(m_mymemoryEmail);
    registerEngine(mm);

    BaiduTranslateEngine *bd = new BaiduTranslateEngine(this);
    bd->setAppId(m_baiduAppId);
    bd->setKey(m_baiduKey);
    registerEngine(bd);

    DeepLEngine *dl = new DeepLEngine(this);
    dl->setKey(m_deeplKey);
    registerEngine(dl);

    LibreTranslateEngine *lt = new LibreTranslateEngine(this);
    lt->setUrl(m_libreUrl);
    registerEngine(lt);

    // 设置当前引擎
    QString engineName = settings->value("translate/engine", "mymemory").toString();
    setCurrentEngine(engineName);

    LOG_INFO(QString("TranslateService: config loaded, engine=%1 target=%2")
                 .arg(m_currentEngineName, m_targetLang));
}

/**
 * @brief 注册引擎到管理器并连接信号
 * @param engine 引擎实例
 * @author chiangyang
 */
void TranslateService::registerEngine(TranslateEngine *engine) {
    if (!engine) {
        return;
    }
    m_engines.insert(engine->name(), engine);
    connect(engine, &TranslateEngine::finished, this, &TranslateService::onEngineFinished);
    connect(engine, &TranslateEngine::failed, this, &TranslateService::onEngineFailed);
}

/**
 * @brief 清理所有引擎实例
 * @author chiangyang
 */
void TranslateService::clearEngines() {
    m_currentEngine = nullptr;
    m_currentEngineName.clear();
    qDeleteAll(m_engines);
    m_engines.clear();
}

/**
 * @brief 设置当前引擎
 * @param name 引擎名称
 * @author chiangyang
 */
void TranslateService::setCurrentEngine(const QString &name) {
    auto it = m_engines.find(name);
    if (it != m_engines.end()) {
        m_currentEngine = it.value();
        m_currentEngineName = name;
        emit currentEngineChanged(name);
        LOG_INFO(QString("TranslateService: current engine set to %1").arg(name));
    } else if (!m_engines.isEmpty()) {
        // 指定引擎不存在时回退到第一个
        m_currentEngine = m_engines.first();
        m_currentEngineName = m_currentEngine->name();
        LOG_INFO(QString("TranslateService: engine %1 not found, fallback to %2")
                     .arg(name, m_currentEngineName));
    }
}

/**
 * @brief 翻译文本
 * @param text 源文本
 * @param sourceLang 源语言代码，空串则使用配置默认值
 * @param targetLang 目标语言代码，空串则使用配置默认值
 * @author chiangyang
 */
void TranslateService::translate(const QString &text,
                                 const QString &sourceLang,
                                 const QString &targetLang) {
    if (!m_currentEngine) {
        emit failed(TranslateEngine::TranslateError::NotConfigured, "No translation engine available");
        return;
    }
    if (!m_currentEngine->isAvailable()) {
        emit failed(TranslateEngine::TranslateError::NotConfigured,
                    QString("Engine '%1' is not available or not configured")
                        .arg(m_currentEngine->name()));
        return;
    }

    QString src = sourceLang.isEmpty() ? m_sourceLang : sourceLang;
    QString tgt = targetLang.isEmpty() ? m_targetLang : targetLang;
    m_currentEngine->translate(text, src, tgt);
}

/**
 * @brief 批量翻译多段文本
 * @param texts 源文本列表
 * @param sourceLang 源语言代码，空串则用配置默认值
 * @param targetLang 目标语言代码，空串则用配置默认值
 * @author chiangyang
 */
void TranslateService::translateBatch(const QStringList &texts,
                                      const QString &sourceLang,
                                      const QString &targetLang) {
    if (texts.isEmpty()) {
        emit batchFinished(QStringList());
        return;
    }
    if (!m_currentEngine || !m_currentEngine->isAvailable()) {
        emit failed(TranslateEngine::TranslateError::NotConfigured,
                    QStringLiteral("Engine not available for batch translation"));
        // 引擎不可用时返回原文列表，避免上层流程卡住
        emit batchFinished(texts);
        return;
    }

    m_batchOriginals = texts;
    m_batchTranslated = QStringList(texts.size());
    m_batchTotal = texts.size();
    m_batchIndex = 0;
    m_batchActive = true;
    m_batchSourceLang = sourceLang.isEmpty() ? m_sourceLang : sourceLang;
    m_batchTargetLang = targetLang.isEmpty() ? m_targetLang : targetLang;

    LOG_INFO(QString("TranslateService: batch translation started, %1 segments").arg(texts.size()));
    translateNextBatchItem();
}

/**
 * @brief 翻译批量中的下一段
 * @author chiangyang
 */
void TranslateService::translateNextBatchItem() {
    if (m_batchIndex >= m_batchTotal) {
        return;
    }
    const QString &text = m_batchOriginals[m_batchIndex];
    if (text.isEmpty()) {
        // 空文本直接跳过
        m_batchTranslated[m_batchIndex] = QString();
        onBatchItemFinished();
        return;
    }
    m_currentEngine->translate(text, m_batchSourceLang, m_batchTargetLang);
}

/**
 * @brief 批量中单段完成处理
 * @author chiangyang
 */
void TranslateService::onBatchItemFinished() {
    m_batchIndex++;
    if (m_batchIndex >= m_batchTotal) {
        // 全部段翻译完成
        m_batchActive = false;
        QStringList result = m_batchTranslated;
        m_batchOriginals.clear();
        m_batchTranslated.clear();
        m_batchTotal = 0;
        m_batchIndex = 0;
        LOG_INFO("TranslateService: batch translation finished");
        emit batchFinished(result);
    } else {
        // 继续下一段
        translateNextBatchItem();
    }
}

/**
 * @brief 引擎翻译完成槽函数
 * @param original 原文
 * @param translated 译文
 * @author chiangyang
 */
void TranslateService::onEngineFinished(const QString &original, const QString &translated) {
    if (m_batchActive) {
        // 批量模式：收集当前段译文，继续下一段
        m_batchTranslated[m_batchIndex] = translated;
        onBatchItemFinished();
    } else {
        emit finished(original, translated);
    }
}

/**
 * @brief 引擎翻译失败槽函数
 * @param code 错误分类码
 * @param detail 原始技术细节
 * @author chiangyang
 */
void TranslateService::onEngineFailed(TranslateEngine::TranslateError code, const QString &detail) {
    if (m_batchActive) {
        // 批量模式：失败的段用原文占位，不中断整体流程
        m_batchTranslated[m_batchIndex] = m_batchOriginals[m_batchIndex];
        LOG_WARNING(QString("TranslateService: batch item %1 failed (code=%2), fallback to original")
                        .arg(m_batchIndex).arg(static_cast<int>(code)));
        onBatchItemFinished();
    } else {
        emit failed(code, detail);
    }
}
