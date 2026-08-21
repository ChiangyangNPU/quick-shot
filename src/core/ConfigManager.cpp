#include "ConfigManager.h"
#include "StyleManager.h"
#include "../shortcut/ShortcutTypes.h"
#include "../log/Logger.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QProcess>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 初始化静态成员变量
ConfigManager* ConfigManager::s_instance = nullptr;

// 配置键名常量
const char* ConfigManager::KEY_CONFIG_PATH = "general/configFilePath";
const char* ConfigManager::ORGANIZATION_NAME = "MyCompany";
const char* ConfigManager::APPLICATION_NAME = "QuickShot";

// 配置默认值常量（原属 StyleManager，迁移至此：配置默认值归配置层）
const char* ConfigManager::DEFAULT_VERSION = "1.0";          ///< 默认版本号
const char* ConfigManager::DEFAULT_LANGUAGE = "zh_CN";       ///< 默认语言（中文）
const bool ConfigManager::DEFAULT_LOG_PRINT_ENABLED = true;  ///< 默认启用日志打印



/**
 * @brief 获取 ConfigManager 单例实例
 * @return ConfigManager 实例指针
 * @note 保留单例模式向后兼容，推荐使用依赖注入
 * @author chiangyang
 */
ConfigManager* ConfigManager::instance() {
    if (!s_instance) {
        s_instance = new ConfigManager();
    }
    return s_instance;
}

/**
 * @brief 设置 ConfigManager 单例实例
 * @param instance 要设置的实例指针
 * @note 用于依赖注入场景，允许外部设置单例实例
 * @author chiangyang
 */
void ConfigManager::setInstance(ConfigManager* instance) {
    if (s_instance && s_instance != instance) {
        LOG_WARNING("ConfigManager::setInstance: overwriting existing instance");
    }
    s_instance = instance;
}

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr) {
    // 初始化默认配置路径
    initDefaultConfigPath();

    // 加载配置路径
    QString savedPath = loadConfigPath();
    if (!savedPath.isEmpty() && QFile::exists(savedPath)) {
        m_currentConfigPath = savedPath;
    } else {
        m_currentConfigPath = m_defaultConfigPath;
    }
}

/**
 * @brief 析构函数
 * @author chiangyang
 */
ConfigManager::~ConfigManager() {
    if (m_settings) {
        delete m_settings;
        m_settings = nullptr;
    }
}

/**
 * @brief 初始化默认配置路径
 * @author chiangyang
 */
void ConfigManager::initDefaultConfigPath() {
    // 获取应用程序所在目录
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // Windows: 保存在应用程序目录下
    m_defaultConfigPath = appDir + "/QuickShot.ini";
#elif defined(Q_OS_MAC)
    // macOS: 保存在应用程序包 Resources 目录下或用户配置目录
    m_defaultConfigPath = appDir + "/../Resources/QuickShot.ini";
    if (!QFile::exists(m_defaultConfigPath)) {
        m_defaultConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/QuickShot.ini";
    }
#else
    // Linux: 保存在用户配置目录
    m_defaultConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/QuickShot.ini";
#endif

    // 确保默认配置文件存在，如果不存在则创建
    if (!QFile::exists(m_defaultConfigPath)) {
        createDefaultConfig();
    } else {
        // 文件已存在，补充缺失的配置项
        ensureDefaultValues();
    }

    LOG_INFO(QString("Default config path: %1").arg(m_defaultConfigPath));
}

/**
 * @brief 确保配置文件包含所有默认值
 * @author chiangyang
 */
void ConfigManager::ensureDefaultValues() {
    QSettings *settings = new QSettings(m_defaultConfigPath, QSettings::IniFormat);
    
    // 检查并补充缺失的配置项
    if (!settings->contains("General/version")) {
        settings->setValue("General/version", ConfigManager::DEFAULT_VERSION);
    }
    if (!settings->contains("language")) {
        settings->setValue("language", ConfigManager::DEFAULT_LANGUAGE);
    }
    if (!settings->contains("logPrintEnabled")) {
        settings->setValue("logPrintEnabled", ConfigManager::DEFAULT_LOG_PRINT_ENABLED);
    }
    // 快捷键默认值：遍历数据表补全所有快捷键（含 history，共 9 项）
    for (int i = 0; i < kShortcutConfigCount; ++i) {
        const ShortcutConfigItem &item = kShortcutConfigs[i];
        if (!settings->contains(item.configKey)) {
            settings->setValue(item.configKey, item.defaultValue);
        }
    }
    if (!settings->contains("capture/saveDir")) {
        settings->setValue("capture/saveDir", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    }
    if (!settings->contains("record/saveDir")) {
        settings->setValue("record/saveDir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    }
    if (!settings->contains("record/systemAudio")) {
        settings->setValue("record/systemAudio", false);
    }
    if (!settings->contains("record/microphone")) {
        settings->setValue("record/microphone", false);
    }

    // History - 历史记录默认配置
    if (!settings->contains("history/thumbnailSize")) {
        settings->setValue("history/thumbnailSize", 200);
    }

    // OCR - OCR 默认配置（空串表示使用内置 models/ocr 目录）
    if (!settings->contains("ocr/modelPath")) {
        settings->setValue("ocr/modelPath", "");
    }

    // Style - 遍历颜色配置表补全所有颜色键（单一数据源，缺键才补）
    for (const auto& s : StyleManager::colorSettingTable()) {
        QString key = QString("style/%1").arg(s.settingsKey);
        if (!settings->contains(key)) {
            settings->setValue(key, s.defaultColor.name());
        }
    }
    if (!settings->contains("style/toolbarButtonStyle")) {
        settings->setValue("style/toolbarButtonStyle", StyleManager::DEFAULT_TOOLBAR_BUTTON_STYLE);
    }
    if (!settings->contains("style/defaultPenWidth")) {
        settings->setValue("style/defaultPenWidth", StyleManager::DEFAULT_PEN_WIDTH);
    }
    if (!settings->contains("style/defaultFontSize")) {
        settings->setValue("style/defaultFontSize", StyleManager::DEFAULT_FONT_SIZE);
    }
    if (!settings->contains("style/defaultEraserWidth")) {
        settings->setValue("style/defaultEraserWidth", StyleManager::DEFAULT_ERASER_WIDTH);
    }
    if (!settings->contains("style/defaultMosaicSize")) {
        settings->setValue("style/defaultMosaicSize", StyleManager::DEFAULT_MOSAIC_SIZE);
    }

    // Translate - 翻译功能默认配置
    if (!settings->contains("translate/enabled")) {
        settings->setValue("translate/enabled", true);
    }
    if (!settings->contains("translate/engine")) {
        settings->setValue("translate/engine", "mymemory");
    }
    if (!settings->contains("translate/sourceLang")) {
        settings->setValue("translate/sourceLang", "auto");
    }
    if (!settings->contains("translate/targetLang")) {
        settings->setValue("translate/targetLang", "en");
    }
    if (!settings->contains("translate/mymemoryEmail")) {
        settings->setValue("translate/mymemoryEmail", "");
    }
    if (!settings->contains("translate/baiduAppId")) {
        settings->setValue("translate/baiduAppId", "");
    }
    if (!settings->contains("translate/baiduKey")) {
        settings->setValue("translate/baiduKey", "");
    }
    if (!settings->contains("translate/deeplKey")) {
        settings->setValue("translate/deeplKey", "");
    }
    if (!settings->contains("translate/libreUrl")) {
        settings->setValue("translate/libreUrl", "");
    }
    if (!settings->contains("translate/showPrivacyWarning")) {
        settings->setValue("translate/showPrivacyWarning", true);
    }
    
    settings->sync();
    delete settings;
}

/**
 * @brief 创建默认配置文件
 * @return 是否创建成功
 * @author chiangyang
 */
bool ConfigManager::createDefaultConfig() {
    // 使用 QSettings 创建包含所有默认值的配置文件
    QSettings *settings = new QSettings(m_defaultConfigPath, QSettings::IniFormat);
    
    // General
    settings->setValue("General/version", ConfigManager::DEFAULT_VERSION);
    settings->setValue("language", ConfigManager::DEFAULT_LANGUAGE);
    settings->setValue("logPrintEnabled", ConfigManager::DEFAULT_LOG_PRINT_ENABLED);
    
    // Shortcuts: 遍历数据表写入所有快捷键默认值（含 history，共 9 项）
    for (int i = 0; i < kShortcutConfigCount; ++i) {
        settings->setValue(kShortcutConfigs[i].configKey, kShortcutConfigs[i].defaultValue);
    }
    
    // Capture
    settings->setValue("capture/saveDir", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    
    // Record
    settings->setValue("record/saveDir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    settings->setValue("record/systemAudio", false);
    settings->setValue("record/microphone", false);

    // History - 历史记录默认配置
    settings->setValue("history/thumbnailSize", 200);

    // OCR - OCR 默认配置（空串表示使用内置 models/ocr 目录）
    settings->setValue("ocr/modelPath", "");

    // Style - 遍历颜色配置表写入所有颜色默认值（单一数据源）
    for (const auto& s : StyleManager::colorSettingTable()) {
        settings->setValue(QString("style/%1").arg(s.settingsKey), s.defaultColor.name());
    }
    settings->setValue("style/toolbarButtonStyle", StyleManager::DEFAULT_TOOLBAR_BUTTON_STYLE);
    settings->setValue("style/defaultPenWidth", StyleManager::DEFAULT_PEN_WIDTH);
    settings->setValue("style/defaultFontSize", StyleManager::DEFAULT_FONT_SIZE);
    settings->setValue("style/defaultEraserWidth", StyleManager::DEFAULT_ERASER_WIDTH);
    settings->setValue("style/defaultMosaicSize", StyleManager::DEFAULT_MOSAIC_SIZE);

    // Translate - 翻译功能默认配置
    settings->setValue("translate/enabled", true);
    settings->setValue("translate/engine", "mymemory");
    settings->setValue("translate/sourceLang", "auto");
    settings->setValue("translate/targetLang", "en");
    settings->setValue("translate/mymemoryEmail", "");
    settings->setValue("translate/baiduAppId", "");
    settings->setValue("translate/baiduKey", "");
    settings->setValue("translate/deeplKey", "");
    settings->setValue("translate/libreUrl", "");
    settings->setValue("translate/showPrivacyWarning", true);
    
    settings->sync();
    delete settings;
    
    LOG_INFO(QString("Created default config file: %1").arg(m_defaultConfigPath));
    return true;
}

/**
 * @brief 获取当前配置文件路径
 * @return 当前配置文件完整路径
 * @author chiangyang
 */
QString ConfigManager::currentConfigFilePath() const {
    return m_currentConfigPath;
}

/**
 * @brief 获取当前配置文件名称
 * @return 当前配置文件名称（不含路径）
 * @author chiangyang
 */
QString ConfigManager::currentConfigFileName() const {
    return QFileInfo(m_currentConfigPath).fileName();
}

/**
 * @brief 获取默认配置文件路径
 * @return 默认配置文件完整路径
 * @author chiangyang
 */
QString ConfigManager::defaultConfigFilePath() const {
    return m_defaultConfigPath;
}

/**
 * @brief 检查当前配置文件是否存在
 * @return 配置文件是否存在
 * @author chiangyang
 */
bool ConfigManager::isConfigFileExists() const {
    return QFile::exists(m_currentConfigPath);
}

/**
 * @brief 检查是否为默认配置文件
 * @return 是否为默认配置文件
 * @author chiangyang
 */
bool ConfigManager::isDefaultConfig() const {
    return m_currentConfigPath == m_defaultConfigPath;
}

/**
 * @brief 打开配置文件所在文件夹并选中文件
 * @return 是否成功打开
 * @author chiangyang
 */
bool ConfigManager::openConfigFileLocation() {
    if (!isConfigFileExists()) {
        LOG_ERROR("Config file does not exist");
        emit errorOccurred("配置文件不存在");
        return false;
    }

    QFileInfo fileInfo(m_currentConfigPath);
    QString folderPath = fileInfo.absolutePath();
    QString fileName = fileInfo.fileName();

#ifdef Q_OS_WIN
    // Windows: 使用 Explorer 的 /select 参数
    QString command = QString("explorer.exe /select,\"%1\"").arg(m_currentConfigPath);
    QProcess process;
    process.startDetached(command);
    LOG_INFO(QString("Opened file location: %1").arg(folderPath));
    return true;
#elif defined(Q_OS_MAC)
    // macOS: 使用 open 命令
    QString command = QString("open -R \"%1\"").arg(m_currentConfigPath);
    QProcess process;
    process.startDetached(command);
    LOG_INFO(QString("Opened file location: %1").arg(m_currentConfigPath));
    return true;
#else
    // Linux: 使用 xdg-open 打开文件夹
    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    LOG_INFO(QString("Opened file location: %1").arg(folderPath));
    return true;
#endif
}

/**
 * @brief 更改配置文件
 * @param newFilePath 新的配置文件路径
 * @param parent 父窗口，用于显示对话框
 * @return 是否成功更改
 * @author chiangyang
 */
bool ConfigManager::changeConfigFile(const QString &newFilePath, QWidget *parent) {
    if (newFilePath.isEmpty()) {
        LOG_ERROR("New config file path is empty");
        emit errorOccurred("配置文件路径不能为空");
        return false;
    }

    // 检查文件是否存在
    if (!QFile::exists(newFilePath)) {
        LOG_ERROR(QString("Config file does not exist: %1").arg(newFilePath));
        emit errorOccurred("配置文件不存在: " + newFilePath);
        return false;
    }

    // 检查文件是否有读取权限
    QFile file(newFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Cannot read config file: %1").arg(newFilePath));
        emit errorOccurred("无法读取配置文件，请检查文件权限");
        return false;
    }
    file.close();

    // 保存旧配置路径
    QString oldPath = m_currentConfigPath;

    // 更新配置路径
    m_currentConfigPath = newFilePath;
    saveConfigPath(newFilePath);

    // 重新创建 QSettings 实例
    if (m_settings) {
        delete m_settings;
        m_settings = nullptr;
    }

    // 发出配置路径更改信号
    emit configPathChanged(newFilePath);

    LOG_INFO(QString("Config file changed from %1 to %2").arg(oldPath).arg(newFilePath));

    return true;
}

/**
 * @brief 重置为默认配置文件
 * @return 是否成功重置
 * @author chiangyang
 */
bool ConfigManager::resetToDefaultConfig() {
    // 确保默认配置文件存在
    if (!QFile::exists(m_defaultConfigPath)) {
        if (!createDefaultConfig()) {
            LOG_ERROR("Failed to create default config file");
            emit errorOccurred("无法创建默认配置文件");
            return false;
        }
    }

    return changeConfigFile(m_defaultConfigPath);
}

/**
 * @brief 获取 QSettings 实例
 * @return QSettings 实例指针
 * @author chiangyang
 */
QSettings* ConfigManager::getSettings() const {
    if (!m_settings) {
        // 使用当前配置文件路径创建 QSettings
        const_cast<ConfigManager*>(this)->m_settings = new QSettings(
            m_currentConfigPath,
            QSettings::IniFormat
        );
    }
    return m_settings;
}

/**
 * @brief 获取配置目录路径
 * @return 配置目录路径
 * @author chiangyang
 */
QString ConfigManager::getConfigDirectory() const {
    return QFileInfo(m_currentConfigPath).absolutePath();
}

/**
 * @brief 设置值
 * @param key 键名
 * @param value 值
 * @author chiangyang
 */
void ConfigManager::setValue(const QString &key, const QVariant &value) {
    if (!m_settings) {
        m_settings = new QSettings(m_currentConfigPath, QSettings::IniFormat);
    }
    m_settings->setValue(key, value);
}

/**
 * @brief 获取值
 * @param key 键名
 * @param defaultValue 默认值
 * @return 值
 * @author chiangyang
 */
QVariant ConfigManager::value(const QString &key, const QVariant &defaultValue) const {
    if (!m_settings) {
        const_cast<ConfigManager*>(this)->m_settings = new QSettings(
            m_currentConfigPath,
            QSettings::IniFormat
        );
    }
    return m_settings->value(key, defaultValue);
}

/**
 * @brief 同步所有设置到磁盘
 * @author chiangyang
 */
void ConfigManager::sync() {
    if (m_settings) {
        m_settings->sync();
    }
}

/**
 * @brief 保存配置路径到 QuickShot.ini
 * @param path 配置路径
 * @author chiangyang
 */
void ConfigManager::saveConfigPath(const QString &path) {
    // 使用默认配置文件保存配置路径信息
    QSettings defaultSettings(m_defaultConfigPath, QSettings::IniFormat);
    defaultSettings.setValue(KEY_CONFIG_PATH, encryptString(path));
    defaultSettings.sync();
    LOG_INFO(QString("Saved config path: %1").arg(path));
}

/**
 * @brief 从 QuickShot.ini 加载配置路径
 * @return 保存的配置路径，如果没有则返回空字符串
 * @author chiangyang
 */
QString ConfigManager::loadConfigPath() {
    QSettings defaultSettings(m_defaultConfigPath, QSettings::IniFormat);
    QString encryptedPath = defaultSettings.value(KEY_CONFIG_PATH).toString();
    if (encryptedPath.isEmpty()) {
        return QString();
    }
    return decryptString(encryptedPath);
}

/**
 * @brief 加密字符串（简单的 Base64 编码）
 * @param plainText 明文
 * @return 加密后的文本
 * @author chiangyang
 */
QString ConfigManager::encryptString(const QString &plainText) {
    QByteArray data = plainText.toUtf8();
    return data.toBase64();
}

/**
 * @brief 解密字符串
 * @param encryptedText 加密文本
 * @return 解密后的明文
 * @author chiangyang
 */
QString ConfigManager::decryptString(const QString &encryptedText) {
    QByteArray data = QByteArray::fromBase64(encryptedText.toUtf8());
    return QString::fromUtf8(data);
}
