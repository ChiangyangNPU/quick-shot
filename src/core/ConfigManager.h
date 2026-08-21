#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QWidget>

/**
 * @brief 配置文件管理类
 * 
 * 负责管理应用程序的配置文件，包括配置的加载、保存、切换等功能
 * 支持多平台（Windows/macOS/Linux），提供异步操作和错误处理
 * @author chiangyang
 */
class ConfigManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取 ConfigManager 单例实例
     * @return ConfigManager 实例指针
     * @note 保留单例模式向后兼容，推荐使用依赖注入
     * @author chiangyang
     */
    static ConfigManager* instance();

    /**
     * @brief 设置 ConfigManager 单例实例
     * @param instance 要设置的实例指针
     * @note 用于依赖注入场景，允许外部设置单例实例
     * @author chiangyang
     */
    static void setInstance(ConfigManager* instance);

    /**
     * @brief 构造函数
     * @param parent 父对象
     * @note 公开构造函数，支持依赖注入创建独立实例
     * @author chiangyang
     */
    explicit ConfigManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~ConfigManager();

    /**
     * @brief 获取当前配置文件路径
     * @return 当前配置文件完整路径
     * @author chiangyang
     */
    QString currentConfigFilePath() const;

    /**
     * @brief 获取当前配置文件名称
     * @return 当前配置文件名称（不含路径）
     * @author chiangyang
     */
    QString currentConfigFileName() const;

    /**
     * @brief 获取默认配置文件路径
     * @return 默认配置文件完整路径
     * @author chiangyang
     */
    QString defaultConfigFilePath() const;

    /**
     * @brief 检查当前配置文件是否存在
     * @return 配置文件是否存在
     * @author chiangyang
     */
    bool isConfigFileExists() const;

    /**
     * @brief 检查是否为默认配置文件
     * @return 是否为默认配置文件
     * @author chiangyang
     */
    bool isDefaultConfig() const;

    /**
     * @brief 打开配置文件所在文件夹并选中文件
     * @return 是否成功打开
     * @author chiangyang
     */
    bool openConfigFileLocation();

    /**
     * @brief 更改配置文件
     * @param newFilePath 新的配置文件路径
     * @param parent 父窗口，用于显示对话框
     * @return 是否成功更改
     * @author chiangyang
     */
    bool changeConfigFile(const QString &newFilePath, QWidget *parent = nullptr);

    /**
     * @brief 重置为默认配置文件
     * @return 是否成功重置
     * @author chiangyang
     */
    bool resetToDefaultConfig();

    /**
     * @brief 获取 QSettings 实例
     * @return QSettings 实例指针
     * @author chiangyang
     */
    QSettings* getSettings() const;

    /**
     * @brief 获取配置目录路径
     * @return 配置目录路径
     * @author chiangyang
     */
    QString getConfigDirectory() const;

    /**
     * @brief 设置值
     * @param key 键名
     * @param value 值
     * @author chiangyang
     */
    void setValue(const QString &key, const QVariant &value);

    /**
     * @brief 获取值
     * @param key 键名
     * @param defaultValue 默认值
     * @return 值
     * @author chiangyang
     */
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;

    /**
     * @brief 同步所有设置到磁盘
     * @author chiangyang
     */
    void sync();

signals:
    /**
     * @brief 配置文件加载完成信号
     * @param success 加载是否成功
     * @param message 消息
     * @author chiangyang
     */
    void configLoaded(bool success, const QString &message);

    /**
     * @brief 配置文件保存完成信号
     * @param success 保存是否成功
     * @param message 消息
     * @author chiangyang
     */
    void configSaved(bool success, const QString &message);

    /**
     * @brief 配置文件路径更改信号
     * @param newPath 新配置文件路径
     * @author chiangyang
     */
    void configPathChanged(const QString &newPath);

    /**
     * @brief 操作错误信号
     * @param errorMessage 错误消息
     * @author chiangyang
     */
    void errorOccurred(const QString &errorMessage);

private:
    /**
     * @brief 初始化默认配置路径
     * @author chiangyang
     */

    /**
     * @brief 初始化默认配置路径
     * @author chiangyang
     */
    void initDefaultConfigPath();

    /**
     * @brief 确保配置文件包含所有默认值
     * @author chiangyang
     */
    void ensureDefaultValues();

    /**
     * @brief 保存配置路径到注册表/配置文件
     * @param path 配置路径
     * @author chiangyang
     */
    void saveConfigPath(const QString &path);

    /**
     * @brief 从注册表/配置文件加载配置路径
     * @return 保存的配置路径，如果没有则返回空字符串
     * @author chiangyang
     */
    QString loadConfigPath();

    /**
     * @brief 加密字符串（简单的 Base64 编码，实际使用中建议使用更安全的加密方式）
     * @param plainText 明文
     * @return 加密后的文本
     * @author chiangyang
     */
    QString encryptString(const QString &plainText);

    /**
     * @brief 解密字符串
     * @param encryptedText 加密文本
     * @return 解密后的明文
     * @author chiangyang
     */
    QString decryptString(const QString &encryptedText);

    /**
     * @brief 创建默认配置文件
     * @return 是否创建成功
     * @author chiangyang
     */
    bool createDefaultConfig();

    static ConfigManager* s_instance; ///< 单例实例
    QString m_defaultConfigPath; ///< 默认配置文件路径
    QString m_currentConfigPath; ///< 当前配置文件路径
    mutable QSettings* m_settings; ///< QSettings 实例

    // 配置项键名常量
    static const char* KEY_CONFIG_PATH; ///< 配置路径键名
    static const char* ORGANIZATION_NAME; ///< 组织名称
    static const char* APPLICATION_NAME; ///< 应用名称

    // 配置默认值常量（从 StyleManager 迁移：这些属于配置层而非样式层）
    static const char* DEFAULT_VERSION;            ///< 默认版本号
    static const char* DEFAULT_LANGUAGE;           ///< 默认语言（中文）
    static const bool DEFAULT_LOG_PRINT_ENABLED;   ///< 默认启用日志打印
};

#endif // CONFIGMANAGER_H
