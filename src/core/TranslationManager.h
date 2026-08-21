#ifndef TRANSLATIONMANAGER_H
#define TRANSLATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QTranslator>

/**
 * @brief 翻译管理类
 * 
 * 用于加载和管理不同语言的翻译，提供全局翻译服务
 * 采用单例模式，确保全局只有一个实例
 * @author chiangyang
 */
class TranslationManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取TranslationManager实例
     * @return TranslationManager的唯一实例
     * @author chiangyang
     */
    static TranslationManager* instance();

    /**
     * @brief 加载指定语言的翻译
     * @param langCode 语言代码，如"zh_CN"、"en_US"
     * @return 是否加载成功
     * @author chiangyang
     */
    bool loadLanguage(const QString &langCode);

    /**
     * @brief 获取当前语言代码
     * @return 当前语言代码
     * @author chiangyang
     */
    QString currentLanguage() const;

    /**
     * @brief 获取翻译文本
     * @param key 翻译键值，如"settings.windowTitle"
     * @param defaultValue 默认值，当翻译不存在时返回
     * @return 翻译后的文本
     * @author chiangyang
     */
    QString get(const QString &key, const QString &defaultValue = "");

    /**
     * @brief 格式化翻译文本
     * @param key 翻译键值
     * @param args 格式化参数
     * @return 格式化后的翻译文本
     * @author chiangyang
     */
    QString get(const QString &key, const QList<QString> &args);

signals:
    /**
     * @brief 语言改变信号
     * @param langCode 新的语言代码
     * @author chiangyang
     */
    void languageChanged(const QString &langCode);

private:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit TranslationManager(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~TranslationManager();

    /**
     * @brief 从JSON文件加载翻译
     * @param filePath JSON文件路径
     * @return 是否加载成功
     * @author chiangyang
     */
    bool loadFromFile(const QString &filePath);

    /**
     * @brief 解析JSON对象，构建翻译映射
     * @param obj JSON对象
     * @param prefix 键值前缀
     * @author chiangyang
     */
    void parseJsonObject(const QJsonObject &obj, const QString &prefix = "");

    /**
     * @brief 安装 Qt 标准控件翻译（QColorDialog 等 Qt 内置控件）
     * @param langCode 语言代码，如 "zh_CN"
     * @note 项目自身字符串用 JSON 字典翻译，但 QColorDialog、QMessageBox
     *       标准按钮等 Qt 内置控件文本由 Qt 官方 qtbase_<lang>.qm 提供，
     *       此处按当前语言安装/卸载该翻译器，使颜色选择器等随语言切换。
     *       查找顺序：应用目录 translations/ → Qt 安装目录 translations/。
     * @author chiangyang
     */
    void installQtTranslations(const QString &langCode);

private:
    static TranslationManager* m_instance; ///< 单例实例
    QString m_currentLanguage; ///< 当前语言代码
    QMap<QString, QString> m_translations; ///< 翻译映射表
    QTranslator m_qtTranslator; ///< Qt 标准控件翻译器（QColorDialog 等）
};

#endif // TRANSLATIONMANAGER_H