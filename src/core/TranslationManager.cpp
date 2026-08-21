#include "TranslationManager.h"
#include "ConfigManager.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include "../log/Logger.h"

// 静态实例初始化
TranslationManager* TranslationManager::m_instance = nullptr;

/**
 * @brief 获取TranslationManager实例
 * @return TranslationManager的唯一实例
 * @author chiangyang
 */
TranslationManager* TranslationManager::instance() {
    if (!m_instance) {
        LOG_INFO("Create TranslationManager instance");
        m_instance = new TranslationManager();
    }
    return m_instance;
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
TranslationManager::TranslationManager(QObject *parent) : QObject(parent) {
    LOG_INFO("TranslationManager instance created");

    // 优先从配置文件加载用户保存的语言偏好
    QSettings *settings = ConfigManager::instance()->getSettings();
    QString savedLang = settings->value("language", "").toString();
    if (!savedLang.isEmpty()) {
        if (loadLanguage(savedLang)) {
            LOG_INFO(QString("Loaded saved language: %1").arg(savedLang));
            return;
        }
        LOG_WARNING(QString("Failed to load saved language: %1, falling back to system detection").arg(savedLang));
    }

    // 自动检测系统语言
    QString systemLang = QLocale::system().name();
    LOG_INFO(QString("System language detected: %1").arg(systemLang));
    
    // If system language is Chinese, load Chinese
    if (systemLang.startsWith("zh")) {
        if (loadLanguage("zh_CN")) {
            LOG_INFO("Loaded Chinese language");
            return;
        }
    } else {
        // Try to load language matching system language
        if (loadLanguage(systemLang)) {
            LOG_INFO(QString("Loaded language: %1").arg(systemLang));
            return;
        }
        // Try to load language without country code
        QString langCode = systemLang.split('_').first();
        if (loadLanguage(langCode)) {
            LOG_INFO(QString("Loaded language: %1").arg(langCode));
            return;
        }
    }
    
    // Fallback to English if no matching language found
    LOG_INFO("No matching language found, falling back to English");
    loadLanguage("en_US");
}

/**
 * @brief 析构函数
 * @author chiangyang
 */
TranslationManager::~TranslationManager() {
}

/**
 * @brief 加载指定语言的翻译
 * @param langCode 语言代码，如"zh_CN"、"en_US"
 * @return 是否加载成功
 * @author chiangyang
 */
bool TranslationManager::loadLanguage(const QString &langCode) {
    // Build language file path
    // Prioritize application directory to ensure files are found after release
    QString appDir = QCoreApplication::applicationDirPath();
    QString filePath;
    
    // Check if running on macOS app bundle
    #ifdef Q_OS_MAC
        // On macOS, language files are in Resources directory
        filePath = appDir + "/../Resources/languages/" + langCode + ".json";
        if (!QFile::exists(filePath)) {
            // Fallback to standard path
            filePath = appDir + "/languages/" + langCode + ".json";
        }
    #else
        // On other platforms, use standard path
        filePath = appDir + "/languages/" + langCode + ".json";
    #endif
    
    // Try to load from application directory
    if (!QFile::exists(filePath)) {
        // Try to load from source code directory (development environment)
        // Assuming structure: project/src/languages and project/build/bin
        // Go back two levels to find src
        filePath = appDir + "/../../src/languages/" + langCode + ".json";
        if (!QFile::exists(filePath)) {
            // Try one level back (e.g. build/src)
            filePath = appDir + "/../src/languages/" + langCode + ".json";
            if (!QFile::exists(filePath)) {
                // Try current working directory (useful for debugging)
                filePath = QDir::currentPath() + "/src/languages/" + langCode + ".json";
                if (!QFile::exists(filePath)) {
                    // Last try: look directly in current directory
                    filePath = QDir::currentPath() + "/languages/" + langCode + ".json";
                    if (!QFile::exists(filePath)) {
                        LOG_ERROR(QString("Language file not found for: %1").arg(langCode));
                        return false;
                    }
                }
            }
        }
    }

    // Clear existing translations
    m_translations.clear();
    
    // Load new language
    if (loadFromFile(filePath)) {
        m_currentLanguage = langCode;
        // Qt 标准控件（QColorDialog 等）文本由 Qt 官方翻译提供，随语言切换联动
        installQtTranslations(langCode);
        emit languageChanged(langCode);
        return true;
    } else {
        LOG_ERROR(QString("Failed to load language: %1").arg(langCode));
    }

    return false;
}

/**
 * @brief 安装 Qt 标准控件翻译（QColorDialog 等 Qt 内置控件）
 * @param langCode 语言代码，如 "zh_CN"
 * @note 项目自身字符串用 JSON 字典翻译，但 QColorDialog、QMessageBox
 *       标准按钮等 Qt 内置控件文本由 Qt 官方 qtbase_<lang>.qm 提供。
 *       此处先卸载旧翻译器，再按当前语言安装；英文是 Qt 默认语言，
 *       直接卸载即可。查找顺序：应用目录 translations/（发布环境，
 *       由 CMake 构建后复制）→ Qt 安装目录 translations/（开发环境）。
 * @author chiangyang
 */
void TranslationManager::installQtTranslations(const QString &langCode) {
    // 先卸载旧翻译器，避免切换语言后残留
    QCoreApplication::removeTranslator(&m_qtTranslator);

    // 英文是 Qt 默认语言，无需安装翻译器
    if (langCode.isEmpty() || langCode.startsWith("en", Qt::CaseInsensitive)) {
        return;
    }

    // 项目语言码 → Qt 官方翻译文件名基名映射：
    // Qt 对日/韩只用纯语言码（qtbase_ja.qm、qtbase_ko.qm，无国家码后缀），
    // 且无单独的 zh_HK 翻译，香港中文复用繁体（qtbase_zh_TW.qm）。
    QString qtLang = langCode;
    if (langCode == "ja_JP") qtLang = "ja";
    else if (langCode == "ko_KR") qtLang = "ko";
    else if (langCode == "zh_HK") qtLang = "zh_TW";
    const QString qmName = QString("qtbase_%1.qm").arg(qtLang);

    // 1) 应用目录 translations/（发布环境：随包分发的 Qt 翻译文件）
    QString qmPath = QCoreApplication::applicationDirPath() + "/translations/" + qmName;
    if (QFile::exists(qmPath)) {
        if (m_qtTranslator.load(qmPath)) {
            QCoreApplication::installTranslator(&m_qtTranslator);
            LOG_INFO(QString("Qt translations installed: %1").arg(qmPath));
        }
        return;
    }

    // 2) Qt 安装目录 translations/（开发环境：链接的 Qt 自带翻译）
    qmPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath) + "/" + qmName;
    if (QFile::exists(qmPath)) {
        if (m_qtTranslator.load(qmPath)) {
            QCoreApplication::installTranslator(&m_qtTranslator);
            LOG_INFO(QString("Qt translations installed: %1").arg(qmPath));
        }
        return;
    }

    LOG_WARNING(QString("Qt translation file not found: %1").arg(qmName));
}

/**
 * @brief 获取当前语言代码
 * @return 当前语言代码
 * @author chiangyang
 */
QString TranslationManager::currentLanguage() const {
    return m_currentLanguage;
}

/**
 * @brief 获取翻译文本
 * @param key 翻译键值，如"settings.windowTitle"
 * @param defaultValue 默认值，当翻译不存在时返回
 * @return 翻译后的文本
 * @author chiangyang
 */
QString TranslationManager::get(const QString &key, const QString &defaultValue) {
    // Check if translation map contains the key
    if (m_translations.contains(key)) {
        return m_translations.value(key);
    }
    // If not found, try to use default value
    if (!defaultValue.isEmpty()) {
        return defaultValue;
    }
    // If default value is also empty, return the key
    return key;
}

/**
 * @brief 格式化翻译文本
 * @param key 翻译键值
 * @param args 格式化参数
 * @return 格式化后的翻译文本
 * @author chiangyang
 */
QString TranslationManager::get(const QString &key, const QList<QString> &args) {
    QString text = get(key);
    
    // Replace placeholders %1, %2, etc.
    for (int i = 0; i < args.size(); ++i) {
        text.replace("%" + QString::number(i + 1), args.at(i));
    }
    
    return text;
}

/**
 * @brief 从JSON文件加载翻译
 * @param filePath JSON文件路径
 * @return 是否加载成功
 * @author chiangyang
 */
bool TranslationManager::loadFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open file: %1").arg(filePath));
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
    if (!jsonDoc.isObject()) {
        LOG_ERROR("Invalid JSON format");
        return false;
    }
    
    parseJsonObject(jsonDoc.object());
    return true;
}

/**
 * @brief 解析JSON对象，构建翻译映射
 * @param obj JSON对象
 * @param prefix 键值前缀
 * @author chiangyang
 */
void TranslationManager::parseJsonObject(const QJsonObject &obj, const QString &prefix) {
    // Clear existing translations
    if (prefix.isEmpty()) {
        m_translations.clear();
    }
    
    // Iterate through JSON object
    QJsonObject::const_iterator it;
    for (it = obj.constBegin(); it != obj.constEnd(); ++it) {
        QString key = it.key();
        QString fullKey = prefix.isEmpty() ? key : prefix + "." + key;
        QJsonValue value = it.value();
        
        if (value.isString()) {
            m_translations[fullKey] = value.toString();
        } else if (value.isObject()) {
            // Recursively parse nested objects
            parseJsonObject(value.toObject(), fullKey);
        }
    }
}
