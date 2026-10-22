#include "SettingsGeneralTab.h"
// 注意：OcrEngine.h 会拉入 windows.h（其将 MessageBox 定义为 MessageBoxW 宏），
// 必须位于 ../MessageBox.h（内部 #undef MessageBox）之前
#ifdef ENABLE_OCR
#include "../ocr/OcrEngine.h"
#endif
#include "../MessageBox.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../log/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QUrl>
#include <QDesktopServices>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsGeneralTab::SettingsGeneralTab(QWidget *parent)
    : QWidget(parent)
    , m_configManager(nullptr)
{
    setupUi();
}

void SettingsGeneralTab::setupUi() {
    // 本控件即选项卡根容器（#generalTabBg 选择器只匹配自身）
    setObjectName("generalTabBg");
    setStyleSheet(QString("#generalTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    // Language
    QHBoxLayout *langLayout = new QHBoxLayout();
    lblLanguage = new QLabel(TranslationManager::instance()->get("language", "Language:"), this);
    langCombo = new QComboBox(this);
    StyleManager::useHighlightOnlyPopup(langCombo);
    langCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    langCombo->addItem("简体中文", "zh_CN");
    langCombo->addItem("繁體中文（香港）", "zh_HK");
    langCombo->addItem("繁體中文（台灣）", "zh_TW");
    langCombo->addItem("English", "en_US");
    langCombo->addItem("日本語", "ja_JP");
    langCombo->addItem("한국어", "ko_KR");
    connect(langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsGeneralTab::onLanguageChanged);
    
    langLayout->addWidget(lblLanguage);
    langLayout->addWidget(langCombo);
    langLayout->addStretch();
    
    // Auto Start
    autoStartCheck = new QCheckBox(TranslationManager::instance()->get("autoStart", "Run on Startup"), this);
    autoStartCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(autoStartCheck, &QCheckBox::checkStateChanged, this, &SettingsGeneralTab::onAutoStartChanged);

    // Log Print
    logPrintCheck = new QCheckBox(TranslationManager::instance()->get("logPrint.checkbox", "Enable Log Printing"), this);
    logPrintCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(logPrintCheck, &QCheckBox::checkStateChanged, this, &SettingsGeneralTab::onLogPrintChanged);

    btnOpenLogFile = new QPushButton(TranslationManager::instance()->get("logPrint.openLogFile", "Open Log File"), this);
    btnOpenLogFile->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnOpenLogFile, &QPushButton::clicked, this, &SettingsGeneralTab::onOpenLogFile);

    btnClearLogFile = new QPushButton(TranslationManager::instance()->get("logPrint.clearLogFile", "Clear Logs"), this);
    btnClearLogFile->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnClearLogFile, &QPushButton::clicked, this, &SettingsGeneralTab::onClearLogFile);

    QHBoxLayout *logPrintLayout = new QHBoxLayout();
    logPrintLayout->addWidget(logPrintCheck);
    logPrintLayout->addWidget(btnOpenLogFile);
    logPrintLayout->addWidget(btnClearLogFile);
    logPrintLayout->addStretch();

    layout->addLayout(langLayout);
    layout->addWidget(autoStartCheck);
    layout->addLayout(logPrintLayout);

    // OCR Language
    QHBoxLayout *ocrLayout = new QHBoxLayout();
    lblOcrLanguage = new QLabel(TranslationManager::instance()->get("ocr.language", "Text Recognition Language:"), this);
    cmbOcrLanguage = new QComboBox(this);
    StyleManager::useHighlightOnlyPopup(cmbOcrLanguage);
    cmbOcrLanguage->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ch_en", "Chinese + English"), "ch_en");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.en", "English"), "en");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ja", "Japanese"), "ja");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ko", "Korean"), "ko");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.multi", "Multilingual"), "multi");
    connect(cmbOcrLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsGeneralTab::onOcrLanguageChanged);

    ocrLayout->addWidget(lblOcrLanguage);
    ocrLayout->addWidget(cmbOcrLanguage);

    // GPU Acceleration
    gpuAccelCheck = new QCheckBox(TranslationManager::instance()->get("ocr.gpuAccel", "GPU Acceleration"), this);
    gpuAccelCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(gpuAccelCheck, &QCheckBox::checkStateChanged, this, &SettingsGeneralTab::onGpuAccelChanged);
#ifdef ENABLE_OCR
    gpuAccelCheck->setVisible(true);
#else
    gpuAccelCheck->setVisible(false);
#endif

    ocrLayout->addWidget(gpuAccelCheck);
    ocrLayout->addStretch();

    layout->addLayout(ocrLayout);

    saveGroup = new QGroupBox(this);
    saveGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    auto *saveLayout = new QGridLayout(saveGroup);
    saveLayout->setColumnStretch(1, 1);

    lblCaptureSaveDir = new QLabel(TranslationManager::instance()->get("capture.saveDir", "Screenshot Save:"), saveGroup);
    editCaptureSaveDir = new QLineEdit(saveGroup);
    editCaptureSaveDir->setReadOnly(true);
    editCaptureSaveDir->setStyleSheet(StyleManager::getPathEditStyle());
    btnCaptureChooseDir = new QPushButton(TranslationManager::instance()->get("capture.chooseDir", "Choose"), saveGroup);
    btnCaptureChooseDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnCaptureChooseDir, &QPushButton::clicked, this, &SettingsGeneralTab::onChooseCaptureSaveDir);
    
    btnCaptureResetDir = new QPushButton(TranslationManager::instance()->get("capture.resetDir", "Reset"), saveGroup);
    btnCaptureResetDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnCaptureResetDir, &QPushButton::clicked, this, &SettingsGeneralTab::onResetCaptureSaveDir);

    lblRecordSaveDir = new QLabel(TranslationManager::instance()->get("record.saveDir", "Record Save:"), saveGroup);
    editRecordSaveDir = new QLineEdit(saveGroup);
    editRecordSaveDir->setReadOnly(true);
    editRecordSaveDir->setStyleSheet(StyleManager::getPathEditStyle());
    btnRecordChooseDir = new QPushButton(TranslationManager::instance()->get("record.chooseDir", "Choose"), saveGroup);
    btnRecordChooseDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnRecordChooseDir, &QPushButton::clicked, this, &SettingsGeneralTab::onChooseRecordSaveDir);
    
    btnRecordResetDir = new QPushButton(TranslationManager::instance()->get("record.resetDir", "Reset"), saveGroup);
    btnRecordResetDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnRecordResetDir, &QPushButton::clicked, this, &SettingsGeneralTab::onResetRecordSaveDir);

    saveLayout->addWidget(lblCaptureSaveDir, 0, 0);
    saveLayout->addWidget(editCaptureSaveDir, 0, 1);
    saveLayout->addWidget(btnCaptureChooseDir, 0, 2);
    saveLayout->addWidget(btnCaptureResetDir, 0, 3);

    saveLayout->addWidget(lblRecordSaveDir, 1, 0);
    saveLayout->addWidget(editRecordSaveDir, 1, 1);
    saveLayout->addWidget(btnRecordChooseDir, 1, 2);
    saveLayout->addWidget(btnRecordResetDir, 1, 3);

    // 调整元素高度，使其保持一致（使用 QLineEdit 的标准高度，与快捷键选项卡一致）
    int inputHeight = editCaptureSaveDir->sizeHint().height();
    lblLanguage->setFixedHeight(inputHeight);
    langCombo->setFixedHeight(inputHeight);
    autoStartCheck->setFixedHeight(inputHeight);
    logPrintCheck->setFixedHeight(inputHeight);
    lblOcrLanguage->setFixedHeight(inputHeight);
    cmbOcrLanguage->setFixedHeight(inputHeight);
    gpuAccelCheck->setFixedHeight(inputHeight);
    lblCaptureSaveDir->setFixedHeight(inputHeight);
    editCaptureSaveDir->setFixedHeight(inputHeight);
    btnCaptureChooseDir->setFixedHeight(inputHeight);
    btnCaptureResetDir->setFixedHeight(inputHeight);
    lblRecordSaveDir->setFixedHeight(inputHeight);
    editRecordSaveDir->setFixedHeight(inputHeight);
    btnRecordChooseDir->setFixedHeight(inputHeight);
    btnRecordResetDir->setFixedHeight(inputHeight);
    btnOpenLogFile->setFixedHeight(inputHeight);
    btnClearLogFile->setFixedHeight(inputHeight);

    // 设置标签宽度一致，下拉框宽度一致（抽取到 updateGeneralControlWidths，
    // 便于 DPI 变化时重新计算，避免文字被截断）
    updateControlWidths();

    layout->addWidget(saveGroup);

    // Config File Management
    configGroup = new QGroupBox(this);
    configGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    auto *configLayout = new QGridLayout(configGroup);
    configLayout->setColumnStretch(1, 1);

    // 标题标签
    lblConfigTitle = new QLabel(TranslationManager::instance()->get("config.title", "Config File:"), this);

    // 路径显示文本框
    lblConfigFileName = new QLineEdit(this);
    lblConfigFileName->setReadOnly(true);
    lblConfigFileName->setStyleSheet(StyleManager::getPathEditStyle());

    // 按钮
    btnOpenConfigDir = new QPushButton(TranslationManager::instance()->get("config.openLocation", "Open"), this);
    btnOpenConfigDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnOpenConfigDir, &QPushButton::clicked, this, &SettingsGeneralTab::onOpenConfigFileLocation);

    btnChangeConfig = new QPushButton(TranslationManager::instance()->get("config.change", "Change"), this);
    btnChangeConfig->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnChangeConfig, &QPushButton::clicked, this, &SettingsGeneralTab::onChangeConfigFile);

    // 设置元素高度（在创建对象之后）
    lblConfigTitle->setFixedHeight(inputHeight);
    lblConfigFileName->setFixedHeight(inputHeight);
    btnOpenConfigDir->setFixedHeight(inputHeight);
    btnChangeConfig->setFixedHeight(inputHeight);

    // 布局：第一行是标题+路径+按钮（同一行）
    configLayout->addWidget(lblConfigTitle, 0, 0);
    configLayout->addWidget(lblConfigFileName, 0, 1);
    configLayout->addWidget(btnOpenConfigDir, 0, 2);
    configLayout->addWidget(btnChangeConfig, 0, 3);
    configLayout->setColumnStretch(0, 0);
    configLayout->setColumnStretch(1, 1);
    configLayout->setColumnStretch(2, 0);
    configLayout->setColumnStretch(3, 0);

    // 初始化 ConfigManager
    m_configManager = ConfigManager::instance();

    // 连接 ConfigManager 信号
    connect(m_configManager, &ConfigManager::configLoaded, this, &SettingsGeneralTab::onConfigChanged);
    connect(m_configManager, &ConfigManager::configSaved, this, &SettingsGeneralTab::onConfigChanged);
    connect(m_configManager, &ConfigManager::configPathChanged, this, &SettingsGeneralTab::onConfigPathChanged);

    // 更新配置文件名称显示
    if (m_configManager->isConfigFileExists()) {
        lblConfigFileName->setText(m_configManager->currentConfigFilePath());
    } else {
        lblConfigFileName->setText("Config file not found");
    }

    layout->addWidget(configGroup);

    layout->addStretch();
    
}

// ============================================================
// 配置加载（General 部分；样式部分由宿主 loadStyleSettings 负责）
// ============================================================

void SettingsGeneralTab::loadSettings() {
    LOG_INFO("SettingsWindow: loading settings");
    QSettings *settings = ConfigManager::instance()->getSettings();
    
    // Language
    QString lang = settings->value("language", "zh_CN").toString();
    int index = langCombo->findData(lang);
    if (index != -1) {
        langCombo->blockSignals(true);
        langCombo->setCurrentIndex(index);
        langCombo->blockSignals(false);
    }
    
    // 我们不在这里加载语言，而是让main.cpp统一处理
    // TranslationManager::instance()->loadLanguage(lang);
    
    // Auto Start (Check Registry)
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    bool isAutoStart = bootSettings.contains("QuickShot");
    autoStartCheck->setChecked(isAutoStart);
#endif

    // Log Print
    bool isLogPrintEnabled = settings->value("logPrintEnabled", true).toBool();
    logPrintCheck->blockSignals(true);
    logPrintCheck->setChecked(isLogPrintEnabled);
    logPrintCheck->blockSignals(false);
    Logger::instance()->setLogEnabled(isLogPrintEnabled);

    // OCR Language
    QString ocrLang = settings->value("ocr/language", "ch_en").toString();
    int ocrIndex = cmbOcrLanguage->findData(ocrLang);
    if (ocrIndex != -1) {
        cmbOcrLanguage->blockSignals(true);
        cmbOcrLanguage->setCurrentIndex(ocrIndex);
        cmbOcrLanguage->blockSignals(false);
    }

    // GPU Acceleration
    bool useGpu = settings->value("ocr/useGpu", false).toBool();
    gpuAccelCheck->blockSignals(true);
    gpuAccelCheck->setChecked(useGpu);
    gpuAccelCheck->blockSignals(false);


    const QString defaultCaptureDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString captureDir = settings->value("capture/saveDir", defaultCaptureDir).toString();
    editCaptureSaveDir->setText(captureDir);
    editCaptureSaveDir->setToolTip(captureDir);

    const QString defaultRecordDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString recordDir = settings->value("record/saveDir", defaultRecordDir).toString();
    editRecordSaveDir->setText(recordDir);
    editRecordSaveDir->setToolTip(recordDir);
    
}

// ============================================================
// 语言切换
// ============================================================

void SettingsGeneralTab::onLanguageChanged(int index) {
    QString langCode = langCombo->itemData(index).toString();
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->setValue("language", langCode);
    settings->sync();
    
    // 使用TranslationManager加载语言；全窗口重翻译由宿主在收到
    // languageChanged 信号后统一执行
    bool success = TranslationManager::instance()->loadLanguage(langCode);
    if (!success) {
        // 语言加载失败，显示错误消息
        MessageBox::warning(this,
            TranslationManager::instance()->get("error", "Error"),
            TranslationManager::instance()->get("language.loadFailed", "Failed to load language file."));
        return;
    }
    emit languageChanged(langCode);
}

// ============================================================
// 语言切换
// ============================================================

void SettingsGeneralTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    lblLanguage->setText(tm->get("language", "Language:"));
    autoStartCheck->setText(tm->get("autoStart", "Run on Startup"));
    if (logPrintCheck) logPrintCheck->setText(tm->get("logPrint.checkbox", "Enable Log Printing"));
    if (btnOpenLogFile) btnOpenLogFile->setText(tm->get("logPrint.openLogFile", "Open Log File"));
    if (btnClearLogFile) btnClearLogFile->setText(tm->get("logPrint.clearLogFile", "Clear Logs"));

    // OCR Language
    if (lblOcrLanguage) lblOcrLanguage->setText(tm->get("ocr.language", "Text Recognition Language:"));
    if (gpuAccelCheck) gpuAccelCheck->setText(tm->get("ocr.gpuAccel", "GPU Acceleration"));
    if (cmbOcrLanguage) {
        int ocrCurrentIndex = cmbOcrLanguage->currentIndex();
        cmbOcrLanguage->blockSignals(true);
        cmbOcrLanguage->clear();
        cmbOcrLanguage->addItem(tm->get("ocr.lang.ch_en", "Chinese + English"), "ch_en");
        cmbOcrLanguage->addItem(tm->get("ocr.lang.en", "English"), "en");
        cmbOcrLanguage->addItem(tm->get("ocr.lang.ja", "Japanese"), "ja");
        cmbOcrLanguage->addItem(tm->get("ocr.lang.ko", "Korean"), "ko");
        cmbOcrLanguage->addItem(tm->get("ocr.lang.multi", "Multilingual"), "multi");
        cmbOcrLanguage->setCurrentIndex(ocrCurrentIndex);
        cmbOcrLanguage->blockSignals(false);
    }

    // Config File
    if (lblConfigTitle) lblConfigTitle->setText(tm->get("config.title", "Config File:"));
    if (btnOpenConfigDir) btnOpenConfigDir->setText(tm->get("config.openLocation", "Open"));
    if (btnChangeConfig) btnChangeConfig->setText(tm->get("config.change", "Change"));

    if (lblCaptureSaveDir) lblCaptureSaveDir->setText(tm->get("capture.saveDir", "Screenshot Save:"));
    if (btnCaptureChooseDir) btnCaptureChooseDir->setText(tm->get("capture.chooseDir", "Choose"));
    if (btnCaptureResetDir) btnCaptureResetDir->setText(tm->get("capture.resetDir", "Reset"));
    if (lblRecordSaveDir) lblRecordSaveDir->setText(tm->get("record.saveDir", "Save:"));
    if (btnRecordChooseDir) btnRecordChooseDir->setText(tm->get("record.chooseDir", "Choose"));
    if (btnRecordResetDir) btnRecordResetDir->setText(tm->get("record.resetDir", "Reset"));

    // 配置文件管理
    if (lblConfigTitle) lblConfigTitle->setText(tm->get("config.title", "Config File:"));
    if (btnOpenConfigDir) btnOpenConfigDir->setText(tm->get("config.openLocation", "Open"));
    if (btnChangeConfig) btnChangeConfig->setText(tm->get("config.change", "Change"));
}

// ============================================================
// 样式与布局
// ============================================================

void SettingsGeneralTab::refreshStyles() {
    const QString style = StyleManager::getGroupBoxStyle();
    if (saveGroup) saveGroup->setStyleSheet(style);
    if (configGroup) configGroup->setStyleSheet(style);

    // 输入控件配色随主题刷新（主题切换时经 SettingsWindow::refreshAllStyles 调用）
    const QString comboStyle = StyleManager::getSettingsComboBoxStyle();
    if (langCombo) langCombo->setStyleSheet(comboStyle);
    if (cmbOcrLanguage) cmbOcrLanguage->setStyleSheet(comboStyle);
    const QString checkStyle = StyleManager::getSettingsCheckBoxStyle();
    if (autoStartCheck) autoStartCheck->setStyleSheet(checkStyle);
    if (logPrintCheck) logPrintCheck->setStyleSheet(checkStyle);
    if (gpuAccelCheck) gpuAccelCheck->setStyleSheet(checkStyle);
    const QString editStyle = StyleManager::getPathEditStyle();
    if (editCaptureSaveDir) editCaptureSaveDir->setStyleSheet(editStyle);
    if (editRecordSaveDir) editRecordSaveDir->setStyleSheet(editStyle);
    if (lblConfigFileName) lblConfigFileName->setStyleSheet(editStyle);
}

void SettingsGeneralTab::applyTabBackground(const QColor &color) {
    setStyleSheet(QString("#generalTabBg { background-color: %1; }").arg(color.name()));
}

void SettingsGeneralTab::updateControlWidths()
{
    if (!lblLanguage || !lblOcrLanguage || !langCombo || !cmbOcrLanguage) {
        return;
    }

    // 设置标签宽度一致，下拉框宽度一致
    int labelWidth = qMax(lblLanguage->sizeHint().width(), lblOcrLanguage->sizeHint().width());
    lblLanguage->setFixedWidth(labelWidth);
    lblOcrLanguage->setFixedWidth(labelWidth);

    QFontMetrics fm(langCombo->font());
    int langMaxTextWidth = 0;
    for (int i = 0; i < langCombo->count(); ++i) {
        langMaxTextWidth = qMax(langMaxTextWidth, fm.horizontalAdvance(langCombo->itemText(i)));
    }
    int ocrMaxTextWidth = 0;
    for (int i = 0; i < cmbOcrLanguage->count(); ++i) {
        ocrMaxTextWidth = qMax(ocrMaxTextWidth, fm.horizontalAdvance(cmbOcrLanguage->itemText(i)));
    }
    int comboMinWidth = qMax(langMaxTextWidth, ocrMaxTextWidth) + 40;
    langCombo->setMinimumWidth(comboMinWidth);
    cmbOcrLanguage->setMinimumWidth(comboMinWidth);
}

// ============================================================
// 交互槽
// ============================================================

void SettingsGeneralTab::onChooseCaptureSaveDir() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString currentDir = settings->value("capture/saveDir", defaultDir).toString();
    const QString dir = QFileDialog::getExistingDirectory(this, TranslationManager::instance()->get("capture.chooseDir", "Choose"), currentDir);
    if (dir.isEmpty()) return;
    settings->setValue("capture/saveDir", dir);
    settings->sync();
    editCaptureSaveDir->setText(dir);
    editCaptureSaveDir->setToolTip(dir);
}

/**
 * @brief 选择录制保存目录
 * @author chiangyang
 */
void SettingsGeneralTab::onChooseRecordSaveDir() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString currentDir = settings->value("record/saveDir", defaultDir).toString();
    const QString dir = QFileDialog::getExistingDirectory(this, TranslationManager::instance()->get("record.chooseDir", "Choose"), currentDir);
    if (dir.isEmpty()) return;
    settings->setValue("record/saveDir", dir);
    settings->sync();
    editRecordSaveDir->setText(dir);
    editRecordSaveDir->setToolTip(dir);
}

/**
 * @brief 恢复截屏保存目录为默认
 * @author chiangyang
 */
void SettingsGeneralTab::onResetCaptureSaveDir() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    settings->setValue("capture/saveDir", defaultDir);
    settings->sync();
    editCaptureSaveDir->setText(defaultDir);
    editCaptureSaveDir->setToolTip(defaultDir);
}

/**
 * @brief 恢复录屏保存目录为默认
 * @author chiangyang
 */
void SettingsGeneralTab::onResetRecordSaveDir() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    settings->setValue("record/saveDir", defaultDir);
    settings->sync();
    editRecordSaveDir->setText(defaultDir);
    editRecordSaveDir->setToolTip(defaultDir);
}
void SettingsGeneralTab::onOpenConfigFileLocation() {
    if (!m_configManager) {
        return;
    }

    if (!m_configManager->isConfigFileExists()) {
        MessageBox::warning(this,
            TranslationManager::instance()->get("config.errorTitle", "Error"),
            TranslationManager::instance()->get("config.fileNotFound", "Config file not found"));
        return;
    }

    if (!m_configManager->openConfigFileLocation()) {
        MessageBox::warning(this,
            TranslationManager::instance()->get("config.errorTitle", "Error"),
            TranslationManager::instance()->get("config.openFailed", "Failed to open config file location"));
    }
}
void SettingsGeneralTab::onChangeConfigFile() {
    if (!m_configManager) {
        return;
    }

    // 打开文件选择对话框
    QString selectedFile = QFileDialog::getOpenFileName(
        this,
        TranslationManager::instance()->get("config.selectFile", "Select Config File"),
        m_configManager->getConfigDirectory(),
        "INI Files (*.ini);;All Files (*)"
    );

    if (selectedFile.isEmpty()) {
        // 用户取消了选择
        return;
    }

    // 尝试更改配置文件
    if (m_configManager->changeConfigFile(selectedFile, this)) {
        // 配置更改成功，UI 更新会在 onConfigPathChanged 槽函数中处理
    }
    // 错误处理会在信号中处理
}

void SettingsGeneralTab::onConfigChanged(bool success, const QString &message) {
    if (!success) {
        MessageBox::warning(this,
            TranslationManager::instance()->get("config.errorTitle", "Error"),
            message);
    }
}
void SettingsGeneralTab::onConfigPathChanged(const QString &newPath) {
    // 更新配置文件名称显示
    if (lblConfigFileName) {
        lblConfigFileName->setText(newPath);
    }

    // 显示成功消息
    MessageBox::information(this,
        TranslationManager::instance()->get("config.successTitle", "Success"),
        TranslationManager::instance()->get("config.changeSuccess", "Config file changed successfully"));
}

void SettingsGeneralTab::onAutoStartChanged(Qt::CheckState state) {
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (state == Qt::Checked) {
        bootSettings.setValue("QuickShot", appPath);
    } else {
        bootSettings.remove("QuickShot");
    }
#endif
}
void SettingsGeneralTab::onLogPrintChanged(Qt::CheckState state) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    bool enabled = (state == Qt::Checked);
    settings->setValue("logPrintEnabled", enabled);
    settings->sync();
    Logger::instance()->setLogEnabled(enabled);
}
void SettingsGeneralTab::onOcrLanguageChanged(int index) {
    if (index < 0) return;
    QString langKey = cmbOcrLanguage->itemData(index).toString();
    if (langKey.isEmpty()) return;

    ConfigManager::instance()->setValue("ocr/language", langKey);
    ConfigManager::instance()->sync();

#ifdef ENABLE_OCR
    // 释放 OCR 引擎资源，下次识别时重新加载新语言模型
    OcrEngine::instance()->release();
#endif

    LOG_INFO(QString("SettingsWindow: OCR language changed to %1").arg(langKey));
}
void SettingsGeneralTab::onGpuAccelChanged(Qt::CheckState state) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    bool enabled = (state == Qt::Checked);
    settings->setValue("ocr/useGpu", enabled);
    settings->sync();

#ifdef ENABLE_OCR
    // 释放 OCR 引擎资源，下次识别时重新加载（应用新的 GPU 设置）
    OcrEngine::instance()->release();
#endif

    LOG_INFO(QString("SettingsWindow: GPU acceleration %1").arg(enabled ? "enabled" : "disabled"));
}
void SettingsGeneralTab::onOpenLogFile() {
    QString logDirPath = Logger::instance()->getLogDirPath();
    QDir logDir(logDirPath);
    if (!logDir.exists()) {
        MessageBox::information(this,
            TranslationManager::instance()->get("info", "Info"),
            TranslationManager::instance()->get("logPrint.dirNotExist", "Log directory does not exist."));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(logDirPath));
}
void SettingsGeneralTab::onClearLogFile() {
    QString logDirPath = Logger::instance()->getLogDirPath();
    QDir logDir(logDirPath);
    TranslationManager *tm = TranslationManager::instance();

    if (!logDir.exists()) {
        MessageBox::information(this,
            tm->get("info", "Info"),
            tm->get("logPrint.dirNotExist", "Log directory does not exist."));
        return;
    }

    QFileInfoList fileList = logDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    if (MessageBox::question(this,
            tm->get("logPrint.confirmClear", "Confirm Clear Logs"),
            tm->get("logPrint.confirmClearMsg", "Are you sure you want to delete all log files?"))) {
        // 先关闭日志文件，释放文件锁
        Logger::instance()->closeLogFile();

        int deletedCount = 0;
        QFileInfoList filesToDelete = logDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &fileInfo : filesToDelete) {
            if (logDir.remove(fileInfo.fileName())) {
                deletedCount++;
            }
        }

        if (deletedCount > 0) {
            MessageBox::information(this,
                tm->get("success", "Success"),
                tm->get("logPrint.deletedLogs", "Deleted %1 log file(s).").arg(deletedCount));
        } else {
            MessageBox::information(this,
                tm->get("info", "Info"),
                tm->get("logPrint.noLogs", "No log files to delete."));
        }
    }
}