#ifndef SETTINGSTRANSLATETAB_H
#define SETTINGSTRANSLATETAB_H

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include "../ElidedLabel.h"

/**
 * @brief 设置窗口「翻译」选项卡
 *
 * 包含翻译引擎选择（MyMemory / 百度 / DeepL / LibreTranslate，各自配置区
 * 显隐切换）、目标语言、启用开关与隐私提示。从 SettingsWindow 拆分而来：
 * 配置读写（ConfigManager）、翻译引擎重载（TranslateService）、
 * 选项卡根容器背景与下拉框宽度自适应全部内聚于此。
 * @author chiangyang
 */
class SettingsTranslateTab : public QWidget {
    Q_OBJECT

public:
    explicit SettingsTranslateTab(QWidget *parent = nullptr);

    /**
     * @brief 语言切换时重翻译本选项卡文案（含引擎下拉条目与各引擎官网链接）
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 刷新选项卡内控件的样式（DPI / 样式重载时由宿主调用）
     * @author chiangyang
     */
    void refreshStyles();

    /**
     * @brief 应用选项卡背景色（TabWidgetBg 颜色变更时由宿主调用）
     * @param color 新的背景色
     * @author chiangyang
     */
    void applyTabBackground(const QColor &color);

    /**
     * @brief 统一两个下拉框的最小宽度（引擎名 / 语言名随语言变化后由宿主调用）
     * @author chiangyang
     */
    void updateComboWidths();

private slots:
    /**
     * @brief 翻译引擎切换：显隐对应引擎配置行
     * @param index 引擎下拉框索引
     * @author chiangyang
     */
    void onTranslateEngineChanged(int index);

    /**
     * @brief 翻译设置改变：保存配置并重载翻译引擎
     * @author chiangyang
     */
    void onTranslateSettingChanged();

private:
    void setupUi();

    QGroupBox *m_translateEngineGroup;  ///< 翻译引擎组
    QGroupBox *m_translateOptionsGroup; ///< 翻译选项组（目标语言+开关）
    QComboBox *m_translateEngineCombo;  ///< 翻译引擎下拉框
    QComboBox *m_translateLangCombo;    ///< 翻译语言下拉框
    QLineEdit *m_mymemoryEmailEdit;     ///< MyMemory 邮箱输入框
    QLineEdit *m_baiduAppIdEdit;        ///< 百度 AppID 输入框
    QLineEdit *m_baiduKeyEdit;          ///< 百度密钥输入框
    QLineEdit *m_deeplKeyEdit;          ///< DeepL Key 输入框
    QLineEdit *m_libreUrlEdit;          ///< LibreTranslate 地址输入框
    QCheckBox *m_translateEnabledCheck; ///< 启用翻译复选框
    QCheckBox *m_translatePrivacyCheck; ///< 隐私提示复选框
    QWidget *m_mymemoryConfigWidget;    ///< MyMemory 配置容器
    QWidget *m_baiduConfigWidget;       ///< 百度配置容器
    QWidget *m_deeplConfigWidget;       ///< DeepL 配置容器
    QWidget *m_libreConfigWidget;       ///< LibreTranslate 配置容器
    QLabel *lblTranslateEngine;         ///< 翻译引擎标签
    QLabel *lblTranslateLang;           ///< 翻译语言标签
    QLabel *lblMymemoryEmail;           ///< MyMemory 邮箱标签
    QLabel *lblMymemoryWebsite;         ///< MyMemory 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblMymemoryNetworkHint; ///< MyMemory 网络访问说明标签
    QLabel *lblBaiduAppId;              ///< 百度 AppID 标签
    QLabel *lblBaiduKey;                ///< 百度密钥标签
    QLabel *lblBaiduWebsite;            ///< 百度翻译官网链接标签（点击跳转浏览器）
    ElidedLabel *lblBaiduNetworkHint;   ///< 百度翻译网络访问说明标签
    QLabel *lblDeeplKey;                ///< DeepL Key 标签
    QLabel *lblDeeplWebsite;            ///< DeepL 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblDeeplNetworkHint;   ///< DeepL 网络访问说明标签
    QLabel *lblLibreUrl;                ///< LibreTranslate 地址标签
    QLabel *lblLibreWebsite;            ///< LibreTranslate 官网链接标签（点击跳转浏览器）
    ElidedLabel *lblLibreNetworkHint;   ///< LibreTranslate 网络访问说明标签
};

#endif // SETTINGSTRANSLATETAB_H
