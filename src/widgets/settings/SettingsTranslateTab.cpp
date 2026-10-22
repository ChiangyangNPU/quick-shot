#include "SettingsTranslateTab.h"
#include "../../core/ConfigManager.h"
#include "../../core/SecretStore.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../translate/TranslateService.h"
#include "Logger.h"

#include <QVBoxLayout>
#include <QFontMetrics>
#include <QHBoxLayout>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsTranslateTab::SettingsTranslateTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsTranslateTab::setupUi() {
    TranslationManager *tm = TranslationManager::instance();
    QSettings *settings = ConfigManager::instance()->getSettings();

    // 本控件即选项卡根容器（#translateTabBg 选择器只匹配自身）
    setObjectName("translateTabBg");
    setStyleSheet(QString("#translateTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // ===== 翻译引擎组 =====
    QGroupBox *engineGroup = new QGroupBox(tm->get("translate.engine", "Translation Engine"), this);
    m_translateEngineGroup = engineGroup;
    engineGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *engineLayout = new QVBoxLayout(engineGroup);
    engineLayout->setContentsMargins(10, 10, 10, 10);
    engineLayout->setSpacing(8);

    // 引擎选择行
    QHBoxLayout *engineRow = new QHBoxLayout();
    lblTranslateEngine = new QLabel(tm->get("translate.engine", "Translation Engine") + ":", engineGroup);
    m_translateEngineCombo = new QComboBox(engineGroup);
    StyleManager::useHighlightOnlyPopup(m_translateEngineCombo);
    m_translateEngineCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    m_translateEngineCombo->addItem(tm->get("translate.engineMyMemory", "MyMemory (No Key)"), "mymemory");
    m_translateEngineCombo->addItem(tm->get("translate.engineBaidu", "Baidu Translate"), "baidu");
    m_translateEngineCombo->addItem(tm->get("translate.engineDeepL", "DeepL"), "deepl");
    m_translateEngineCombo->addItem(tm->get("translate.engineLibre", "LibreTranslate (Self-hosted)"), "libretranslate");
    engineRow->addWidget(lblTranslateEngine);
    engineRow->addWidget(m_translateEngineCombo);
    engineRow->addStretch();
    engineLayout->addLayout(engineRow);

    // MyMemory 邮箱配置区（邮箱输入框 + 官网链接，各占一行）
    m_mymemoryConfigWidget = new QWidget(engineGroup);
    QVBoxLayout *mmLayout = new QVBoxLayout(m_mymemoryConfigWidget);
    mmLayout->setContentsMargins(0, 0, 0, 0);
    mmLayout->setSpacing(6);
    QHBoxLayout *mmRow = new QHBoxLayout();
    mmRow->setContentsMargins(0, 0, 0, 0);
    lblMymemoryEmail = new QLabel(tm->get("translate.mymemoryEmail", "MyMemory Email (optional)"), m_mymemoryConfigWidget);
    m_mymemoryEmailEdit = new QLineEdit(m_mymemoryConfigWidget);
    m_mymemoryEmailEdit->setStyleSheet(StyleManager::getPathEditStyle());
    m_mymemoryEmailEdit->setPlaceholderText(tm->get("translate.mymemoryEmailHint", "Fill to raise quota to 50000 words/day"));
    mmRow->addWidget(lblMymemoryEmail);
    mmRow->addWidget(m_mymemoryEmailEdit, 1);
    mmLayout->addLayout(mmRow);
    // MyMemory 官网链接（点击用系统浏览器打开，便于用户了解服务与额度说明）
    lblMymemoryWebsite = new QLabel(m_mymemoryConfigWidget);
    lblMymemoryWebsite->setOpenExternalLinks(true);
    lblMymemoryWebsite->setTextFormat(Qt::RichText);
    mmLayout->addWidget(lblMymemoryWebsite);
    // 网络访问说明（服务器在欧洲，国内可直连无需翻墙）
    lblMymemoryNetworkHint = new ElidedLabel(m_mymemoryConfigWidget);
    lblMymemoryNetworkHint->setWordWrap(false);
    mmLayout->addWidget(lblMymemoryNetworkHint);
    engineLayout->addWidget(m_mymemoryConfigWidget);

    // 百度配置区（AppID、Key、官网链接，各占一行）
    m_baiduConfigWidget = new QWidget(engineGroup);
    QVBoxLayout *bdLayout = new QVBoxLayout(m_baiduConfigWidget);
    bdLayout->setContentsMargins(0, 0, 0, 0);
    bdLayout->setSpacing(6);
    QHBoxLayout *bdAppIdRow = new QHBoxLayout();
    bdAppIdRow->setContentsMargins(0, 0, 0, 0);
    lblBaiduAppId = new QLabel(tm->get("translate.baiduAppId", "Baidu AppID"), m_baiduConfigWidget);
    m_baiduAppIdEdit = new QLineEdit(m_baiduConfigWidget);
    m_baiduAppIdEdit->setStyleSheet(StyleManager::getPathEditStyle());
    bdAppIdRow->addWidget(lblBaiduAppId);
    bdAppIdRow->addWidget(m_baiduAppIdEdit, 1);
    bdLayout->addLayout(bdAppIdRow);
    QHBoxLayout *bdKeyRow = new QHBoxLayout();
    bdKeyRow->setContentsMargins(0, 0, 0, 0);
    lblBaiduKey = new QLabel(tm->get("translate.baiduKey", "Baidu Key"), m_baiduConfigWidget);
    m_baiduKeyEdit = new QLineEdit(m_baiduConfigWidget);
    m_baiduKeyEdit->setStyleSheet(StyleManager::getPathEditStyle());
    bdKeyRow->addWidget(lblBaiduKey);
    bdKeyRow->addWidget(m_baiduKeyEdit, 1);
    bdLayout->addLayout(bdKeyRow);
    // 百度翻译开放平台官网链接（注册开发者获取 AppID/Key）
    lblBaiduWebsite = new QLabel(m_baiduConfigWidget);
    lblBaiduWebsite->setOpenExternalLinks(true);
    lblBaiduWebsite->setTextFormat(Qt::RichText);
    bdLayout->addWidget(lblBaiduWebsite);
    // 网络访问说明（国内服务，可直连）
    lblBaiduNetworkHint = new ElidedLabel(m_baiduConfigWidget);
    lblBaiduNetworkHint->setWordWrap(false);
    bdLayout->addWidget(lblBaiduNetworkHint);
    engineLayout->addWidget(m_baiduConfigWidget);

    // DeepL 配置区（API Key、官网链接，各占一行）
    m_deeplConfigWidget = new QWidget(engineGroup);
    QVBoxLayout *dlLayout = new QVBoxLayout(m_deeplConfigWidget);
    dlLayout->setContentsMargins(0, 0, 0, 0);
    dlLayout->setSpacing(6);
    QHBoxLayout *dlRow = new QHBoxLayout();
    dlRow->setContentsMargins(0, 0, 0, 0);
    lblDeeplKey = new QLabel(tm->get("translate.deeplKey", "DeepL API Key"), m_deeplConfigWidget);
    m_deeplKeyEdit = new QLineEdit(m_deeplConfigWidget);
    m_deeplKeyEdit->setStyleSheet(StyleManager::getPathEditStyle());
    dlRow->addWidget(lblDeeplKey);
    dlRow->addWidget(m_deeplKeyEdit, 1);
    dlLayout->addLayout(dlRow);
    // DeepL 官网链接（注册 DeepL Pro 获取 API Key）
    lblDeeplWebsite = new QLabel(m_deeplConfigWidget);
    lblDeeplWebsite->setOpenExternalLinks(true);
    lblDeeplWebsite->setTextFormat(Qt::RichText);
    dlLayout->addWidget(lblDeeplWebsite);
    // 网络访问说明（国内访问不稳定，通常需翻墙）
    lblDeeplNetworkHint = new ElidedLabel(m_deeplConfigWidget);
    lblDeeplNetworkHint->setWordWrap(false);
    dlLayout->addWidget(lblDeeplNetworkHint);
    engineLayout->addWidget(m_deeplConfigWidget);

    // LibreTranslate 配置区（地址、官网链接，各占一行）
    m_libreConfigWidget = new QWidget(engineGroup);
    QVBoxLayout *ltLayout = new QVBoxLayout(m_libreConfigWidget);
    ltLayout->setContentsMargins(0, 0, 0, 0);
    ltLayout->setSpacing(6);
    QHBoxLayout *ltRow = new QHBoxLayout();
    ltRow->setContentsMargins(0, 0, 0, 0);
    lblLibreUrl = new QLabel(tm->get("translate.libreUrl", "LibreTranslate URL"), m_libreConfigWidget);
    m_libreUrlEdit = new QLineEdit(m_libreConfigWidget);
    m_libreUrlEdit->setStyleSheet(StyleManager::getPathEditStyle());
    m_libreUrlEdit->setPlaceholderText("https://libretranslate.com");
    ltRow->addWidget(lblLibreUrl);
    ltRow->addWidget(m_libreUrlEdit, 1);
    ltLayout->addLayout(ltRow);
    // LibreTranslate 官网链接（注册获取 API Key 或查看自托管文档）
    lblLibreWebsite = new QLabel(m_libreConfigWidget);
    lblLibreWebsite->setOpenExternalLinks(true);
    lblLibreWebsite->setTextFormat(Qt::RichText);
    ltLayout->addWidget(lblLibreWebsite);
    // 网络访问说明（官方服务需翻墙；自托管无需外网）
    lblLibreNetworkHint = new ElidedLabel(m_libreConfigWidget);
    lblLibreNetworkHint->setWordWrap(false);
    ltLayout->addWidget(lblLibreNetworkHint);
    engineLayout->addWidget(m_libreConfigWidget);

    layout->addWidget(engineGroup);

    // ===== 翻译选项组（目标语言 + 开关）=====
    m_translateOptionsGroup = new QGroupBox(tm->get("translate.options", "Translation Options"), this);
    m_translateOptionsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *optionsLayout = new QVBoxLayout(m_translateOptionsGroup);
    optionsLayout->setContentsMargins(10, 10, 10, 10);
    optionsLayout->setSpacing(8);

    // 目标语言行
    QHBoxLayout *langRow = new QHBoxLayout();
    lblTranslateLang = new QLabel(tm->get("translate.targetLang", "Target Language") + ":", m_translateOptionsGroup);
    m_translateLangCombo = new QComboBox(m_translateOptionsGroup);
    StyleManager::useHighlightOnlyPopup(m_translateLangCombo);
    m_translateLangCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    // 语言名称使用本地名称，无需额外翻译
    m_translateLangCombo->addItem("English", "en");
    m_translateLangCombo->addItem("中文", "zh-CN");
    m_translateLangCombo->addItem("繁體中文", "zh-TW");
    m_translateLangCombo->addItem("日本語", "ja");
    m_translateLangCombo->addItem("한국어", "ko");
    m_translateLangCombo->addItem("Français", "fr");
    m_translateLangCombo->addItem("Deutsch", "de");
    m_translateLangCombo->addItem("Español", "es");
    m_translateLangCombo->addItem("Русский", "ru");
    m_translateLangCombo->addItem("Português", "pt");
    langRow->addWidget(lblTranslateLang);
    langRow->addWidget(m_translateLangCombo);
    langRow->addStretch();
    optionsLayout->addLayout(langRow);

    // 开关
    m_translateEnabledCheck = new QCheckBox(tm->get("translate.enabled", "Enable Translation"), m_translateOptionsGroup);
    m_translateEnabledCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_translatePrivacyCheck = new QCheckBox(tm->get("translate.privacyWarning", "Show privacy notice before first translation"), m_translateOptionsGroup);
    m_translatePrivacyCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    optionsLayout->addWidget(m_translateEnabledCheck);
    optionsLayout->addWidget(m_translatePrivacyCheck);

    layout->addWidget(m_translateOptionsGroup);

    layout->addStretch();

    // 统一两个下拉框宽度（取两者最宽条目文本，谁长选谁）
    updateComboWidths();

    // 从配置加载当前值
    QString engineName = settings->value("translate/engine", "mymemory").toString();
    int eIdx = m_translateEngineCombo->findData(engineName);
    if (eIdx >= 0) {
        m_translateEngineCombo->setCurrentIndex(eIdx);
    }
    m_mymemoryEmailEdit->setText(settings->value("translate/mymemoryEmail", "").toString());
    m_baiduAppIdEdit->setText(SecretStore::readWithLegacyMigration(settings, "translate/baiduAppId"));
    m_baiduKeyEdit->setText(SecretStore::readWithLegacyMigration(settings, "translate/baiduKey"));
    m_deeplKeyEdit->setText(SecretStore::readWithLegacyMigration(settings, "translate/deeplKey"));
    m_libreUrlEdit->setText(settings->value("translate/libreUrl", "").toString());
    int tIdx = m_translateLangCombo->findData(settings->value("translate/targetLang", "en").toString());
    if (tIdx >= 0) {
        m_translateLangCombo->setCurrentIndex(tIdx);
    }
    m_translateEnabledCheck->setChecked(settings->value("translate/enabled", true).toBool());
    m_translatePrivacyCheck->setChecked(settings->value("translate/showPrivacyWarning", true).toBool());

    // 初始显隐引擎配置行（在连接信号之前，避免触发保存）
    onTranslateEngineChanged(m_translateEngineCombo->currentIndex());

    // 连接信号：引擎切换显隐 + 保存
    connect(m_translateEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsTranslateTab::onTranslateEngineChanged);
    connect(m_translateEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_translateLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_mymemoryEmailEdit, &QLineEdit::textChanged, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_baiduAppIdEdit, &QLineEdit::textChanged, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_baiduKeyEdit, &QLineEdit::textChanged, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_deeplKeyEdit, &QLineEdit::textChanged, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_libreUrlEdit, &QLineEdit::textChanged, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_translateEnabledCheck, &QCheckBox::toggled, this, &SettingsTranslateTab::onTranslateSettingChanged);
    connect(m_translatePrivacyCheck, &QCheckBox::toggled, this, &SettingsTranslateTab::onTranslateSettingChanged);
}
// ============================================================
// 引擎切换与配置保存
// ============================================================

void SettingsTranslateTab::onTranslateEngineChanged(int index) {
    Q_UNUSED(index);
    QString name = m_translateEngineCombo->currentData().toString();
    m_mymemoryConfigWidget->setVisible(name == "mymemory");
    m_baiduConfigWidget->setVisible(name == "baidu");
    m_deeplConfigWidget->setVisible(name == "deepl");
    m_libreConfigWidget->setVisible(name == "libretranslate");
}

void SettingsTranslateTab::onTranslateSettingChanged() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->setValue("translate/engine", m_translateEngineCombo->currentData().toString());
    settings->setValue("translate/targetLang", m_translateLangCombo->currentData().toString());
    settings->setValue("translate/mymemoryEmail", m_mymemoryEmailEdit->text());
    // 凭据经 SecretStore 加密存储（空串=删除）；仅存储成功时才清除可能
    // 残留的旧明文键——存储失败时保留明文兜底，避免用户输入丢失
    const bool appOk = SecretStore::storeSecret("translate/baiduAppId", m_baiduAppIdEdit->text());
    const bool baiduOk = SecretStore::storeSecret("translate/baiduKey", m_baiduKeyEdit->text());
    const bool deeplOk = SecretStore::storeSecret("translate/deeplKey", m_deeplKeyEdit->text());
    if (appOk) settings->remove("translate/baiduAppId");
    if (baiduOk) settings->remove("translate/baiduKey");
    if (deeplOk) settings->remove("translate/deeplKey");
    settings->setValue("translate/libreUrl", m_libreUrlEdit->text());
    settings->setValue("translate/enabled", m_translateEnabledCheck->isChecked());
    settings->setValue("translate/showPrivacyWarning", m_translatePrivacyCheck->isChecked());
    settings->sync();

    // 重新加载翻译引擎配置
    TranslateService::instance()->loadConfig(settings);
    LOG_INFO("SettingsWindow: translate settings saved and reloaded");
}

// ============================================================
// 样式与布局
// ============================================================

void SettingsTranslateTab::refreshStyles() {
    if (m_translateEngineGroup) m_translateEngineGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_translateOptionsGroup) m_translateOptionsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());

    // 输入控件配色随主题刷新（主题切换时经 SettingsWindow::refreshAllStyles 调用）
    const QString comboStyle = StyleManager::getSettingsComboBoxStyle();
    if (m_translateEngineCombo) m_translateEngineCombo->setStyleSheet(comboStyle);
    if (m_translateLangCombo) m_translateLangCombo->setStyleSheet(comboStyle);
    const QString editStyle = StyleManager::getPathEditStyle();
    if (m_mymemoryEmailEdit) m_mymemoryEmailEdit->setStyleSheet(editStyle);
    if (m_baiduAppIdEdit) m_baiduAppIdEdit->setStyleSheet(editStyle);
    if (m_baiduKeyEdit) m_baiduKeyEdit->setStyleSheet(editStyle);
    if (m_deeplKeyEdit) m_deeplKeyEdit->setStyleSheet(editStyle);
    if (m_libreUrlEdit) m_libreUrlEdit->setStyleSheet(editStyle);
    const QString checkStyle = StyleManager::getSettingsCheckBoxStyle();
    if (m_translateEnabledCheck) m_translateEnabledCheck->setStyleSheet(checkStyle);
    if (m_translatePrivacyCheck) m_translatePrivacyCheck->setStyleSheet(checkStyle);
}

void SettingsTranslateTab::applyTabBackground(const QColor &color) {
    setStyleSheet(QString("#translateTabBg { background-color: %1; }").arg(color.name()));
}

void SettingsTranslateTab::updateComboWidths()
{
    if (!m_translateEngineCombo || !m_translateLangCombo) {
        return;
    }

    // 参照通用选项卡下拉框宽度计算方式：取两个下拉框所有条目文本最大宽度 + 预留 padding
    QFontMetrics fm(m_translateEngineCombo->font());
    int engineMaxTextWidth = 0;
    for (int i = 0; i < m_translateEngineCombo->count(); ++i) {
        engineMaxTextWidth = qMax(engineMaxTextWidth, fm.horizontalAdvance(m_translateEngineCombo->itemText(i)));
    }
    int langMaxTextWidth = 0;
    for (int i = 0; i < m_translateLangCombo->count(); ++i) {
        langMaxTextWidth = qMax(langMaxTextWidth, fm.horizontalAdvance(m_translateLangCombo->itemText(i)));
    }
    const int comboMinWidth = qMax(engineMaxTextWidth, langMaxTextWidth) + 40;
    m_translateEngineCombo->setMinimumWidth(comboMinWidth);
    m_translateLangCombo->setMinimumWidth(comboMinWidth);
}

// ============================================================
// 语言切换
// ============================================================

void SettingsTranslateTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    // Translate 选项卡文案
    if (m_translateOptionsGroup) {
        m_translateOptionsGroup->setTitle(tm->get("translate.options", "Translation Options"));
    }
    // 翻译引擎组标题（仅在 setup 时设置过，语言切换需重新渲染）
    if (m_translateEngineGroup) {
        m_translateEngineGroup->setTitle(tm->get("translate.engine", "Translation Engine"));
    }
    if (lblTranslateEngine) {
        lblTranslateEngine->setText(tm->get("translate.engine", "Translation Engine") + ":");
    }
    if (m_translateEngineCombo) {
        m_translateEngineCombo->setItemText(0, tm->get("translate.engineMyMemory", "MyMemory (No Key)"));
        m_translateEngineCombo->setItemText(1, tm->get("translate.engineBaidu", "Baidu Translate"));
        m_translateEngineCombo->setItemText(2, tm->get("translate.engineDeepL", "DeepL"));
        m_translateEngineCombo->setItemText(3, tm->get("translate.engineLibre", "LibreTranslate (Self-hosted)"));
        // 引擎名称随语言变化，重算两个下拉框统一宽度
        updateComboWidths();
    }
    if (lblTranslateLang) {
        lblTranslateLang->setText(tm->get("translate.targetLang", "Target Language") + ":");
    }
    if (lblMymemoryEmail) {
        lblMymemoryEmail->setText(tm->get("translate.mymemoryEmail", "MyMemory Email (optional)"));
    }
    if (lblMymemoryWebsite) {
        // 链接文本走翻译，URL 固定不变；setOpenExternalLinks 已启用，点击即跳转系统浏览器
        lblMymemoryWebsite->setText(QString("<a href=\"https://mymemory.translated.net/\">%1</a>")
            .arg(tm->get("translate.mymemoryWebsiteLink", "Go to MyMemory website")));
    }
    if (lblMymemoryNetworkHint) {
        lblMymemoryNetworkHint->setText(tm->get("translate.mymemoryNetworkHint",
            "Server in Europe. Direct access from network-restricted regions (e.g., mainland China), no VPN required."));
    }
    if (m_mymemoryEmailEdit) {
        m_mymemoryEmailEdit->setPlaceholderText(tm->get("translate.mymemoryEmailHint", "Fill to raise quota to 50000 words/day"));
    }
    if (lblBaiduAppId) {
        lblBaiduAppId->setText(tm->get("translate.baiduAppId", "Baidu AppID"));
    }
    if (lblBaiduKey) {
        lblBaiduKey->setText(tm->get("translate.baiduKey", "Baidu Key"));
    }
    if (lblBaiduWebsite) {
        // 百度翻译开放平台，注册开发者获取 AppID/Key
        lblBaiduWebsite->setText(QString("<a href=\"https://fanyi-api.baidu.com/\">%1</a>")
            .arg(tm->get("translate.baiduWebsiteLink", "Go to Baidu Translate platform")));
    }
    if (lblBaiduNetworkHint) {
        lblBaiduNetworkHint->setText(tm->get("translate.baiduNetworkHint",
            "Available worldwide. Direct access from any region, no VPN required."));
    }
    if (lblDeeplKey) {
        lblDeeplKey->setText(tm->get("translate.deeplKey", "DeepL API Key"));
    }
    if (lblDeeplWebsite) {
        // DeepL Pro API，注册获取 API Key
        lblDeeplWebsite->setText(QString("<a href=\"https://www.deepl.com/pro-api\">%1</a>")
            .arg(tm->get("translate.deeplWebsiteLink", "Go to DeepL website")));
    }
    if (lblDeeplNetworkHint) {
        lblDeeplNetworkHint->setText(tm->get("translate.deeplNetworkHint",
            "Unstable access from network-restricted regions (e.g., mainland China). A VPN is usually required."));
    }
    if (lblLibreUrl) {
        lblLibreUrl->setText(tm->get("translate.libreUrl", "LibreTranslate URL"));
    }
    if (lblLibreWebsite) {
        // LibreTranslate 官方服务，注册获取 API Key 或查看自托管文档
        lblLibreWebsite->setText(QString("<a href=\"https://libretranslate.com/\">%1</a>")
            .arg(tm->get("translate.libreWebsiteLink", "Go to LibreTranslate website")));
    }
    if (lblLibreNetworkHint) {
        lblLibreNetworkHint->setText(tm->get("translate.libreNetworkHint",
            "Official service usually requires a VPN in restricted regions; self-hosted instances need no VPN."));
    }
    if (m_translateEnabledCheck) {
        m_translateEnabledCheck->setText(tm->get("translate.enabled", "Enable Translation"));
    }
    if (m_translatePrivacyCheck) {
        m_translatePrivacyCheck->setText(tm->get("translate.privacyWarning", "Show privacy notice before first translation"));
    }
}
