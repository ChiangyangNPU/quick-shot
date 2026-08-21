#include "SettingsWindow.h"
#include "../core/StyleManager.h"
#include "../core/ConfigManager.h"
#include "../core/TranslationManager.h"
#include "../shortcut/ShortcutTypes.h"
#include "../history/HistoryManager.h"
#include "../translate/TranslateService.h"
#ifdef ENABLE_OCR
#include "../ocr/OcrEngine.h"
#endif
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include "MessageBox.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QStandardPaths>
#include <QFileDialog>
#include <QColorDialog>
#include <QTimer>
#include <QDebug>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScreen>
#include <QGuiApplication>

#include "../update/UpdateManager.h"
#include <QProgressBar>

#include "Logger.h"

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
SettingsWindow::SettingsWindow(QWidget *parent) : QWidget(parent), heightAnimation(nullptr), m_animatedHeight(0), styleScrollArea(nullptr), historyScrollArea(nullptr), m_configManager(nullptr), lblConfigTitle(nullptr), lblConfigFileName(nullptr), btnOpenConfigDir(nullptr), btnChangeConfig(nullptr), m_updateGroup(nullptr) {
    LOG_INFO("SettingsWindow instance created");
    setWindowTitle("Settings");
    
    // 使用QSvgRenderer设置SVG图标
    QSvgRenderer svgRenderer(QString(":/icons/app.svg"));
    if (svgRenderer.isValid()) {
        QPixmap svgPixmap(32, 32);
        svgPixmap.fill(Qt::transparent);
        QPainter painter(&svgPixmap);
        svgRenderer.render(&painter);
        QIcon svgIcon(svgPixmap);
        if (!svgIcon.isNull()) {
            setWindowIcon(svgIcon);
        } else {
            // 如果SVG图标渲染失败，使用PNG图标作为备用
            setWindowIcon(QIcon(":/icons/app.png"));
        }
    } else {
        // 如果SVG渲染器无效，使用PNG图标
        setWindowIcon(QIcon(":/icons/app.png"));
    }
    
    setupUi();
    loadSettings();
    
    // 安装事件过滤器以处理滚轮事件
    this->installEventFilter(this);

    // 为子控件安装事件过滤器以支持拖动和滚轮事件
    installEventFilterOnChildren();
    
    // 基于窗口所在屏逻辑 DPI 计算窗口宽度，保证不同缩放下窗口物理尺寸接近
    m_dpiScaledWidth = calculateDpiScaledWidth();

    // 固定宽度，让内容高度可确定
    this->setMinimumWidth(m_dpiScaledWidth);
    this->setMaximumWidth(m_dpiScaledWidth);

    // 移除高度限制，让 onTabChanged 来设置自适应高度
    this->setMinimumHeight(0);
    this->setMaximumHeight(16777215);  // QWIDGETSIZE_MAX

    // 监听屏幕逻辑 DPI 变化（系统缩放比例调整时重新计算窗口宽度）
    // 配合 QT_ENABLE_HIGHDPI_SCALING=0 的手动 DPI 适配策略
    // 使用 UniqueConnection 避免窗口多次显示时重复连接
    if (QScreen *screen = this->screen()) {
        connect(screen, &QScreen::logicalDotsPerInchChanged,
                this, &SettingsWindow::onDpiChanged, Qt::UniqueConnection);
    }

    // 延迟调用 onTabChanged 确保布局完成后再调整高度
    QTimer::singleShot(100, this, [this]() {
        onTabChanged(tabWidget->currentIndex());
    });
}

/**
 * @brief 窗口显示事件
 *
 * 窗口显示时重新绑定当前屏幕的 DPI 变化信号，覆盖窗口在不同屏幕间
 * 移动后旧信号失效的情况，确保缩放比例调整时窗口宽度能随之变化。
 * @param event 显示事件
 * @author chiangyang
 */
void SettingsWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // 重新绑定当前屏幕的 DPI 变化信号（窗口可能已移动到其他屏幕）
    if (QScreen *screen = this->screen()) {
        connect(screen, &QScreen::logicalDotsPerInchChanged,
                this, &SettingsWindow::onDpiChanged, Qt::UniqueConnection);
    }
}

/**
 * @brief 屏幕 DPI 变化槽函数
 *
 * 系统缩放比例调整时触发，重新计算窗口宽度并适配当前选项卡高度。
 * @author chiangyang
 */
void SettingsWindow::onDpiChanged() {
    updateDpiScaledWidth();
}

/**
 * @brief 根据当前屏幕逻辑 DPI 重新计算并应用窗口宽度
 *
 * 系统缩放比例变化时调用。基于当前屏幕的 logicalDotsPerInch 重新计算
 * 窗口宽度，更新最小/最大宽度约束与固定尺寸，并重新触发选项卡高度适配，
 * 确保窗口宽度和高度均随缩放比例正确变化。
 * @author chiangyang
 */
void SettingsWindow::updateDpiScaledWidth() {
    int newWidth = calculateDpiScaledWidth();

    // 宽度未变化则跳过，避免不必要的布局重算
    if (newWidth == m_dpiScaledWidth) {
        return;
    }

    LOG_INFO(QString("SettingsWindow: DPI changed, window width %1 -> %2 (logicalDpi=%3)")
             .arg(m_dpiScaledWidth)
             .arg(newWidth)
             .arg(this->screen() ? this->screen()->logicalDotsPerInch() : 96));

    m_dpiScaledWidth = newWidth;
    setMinimumWidth(m_dpiScaledWidth);
    setMaximumWidth(m_dpiScaledWidth);

    // 保持当前高度，仅更新宽度锁定
    int currentHeight = this->height();
    setFixedSize(m_dpiScaledWidth, currentHeight);

    // 延迟到下一事件循环，让 Qt 先完成字体/布局的 DPI 更新，
    // 再重新计算控件固定宽度（避免文字截断）与选项卡高度（宽度变化后内容重排）
    QTimer::singleShot(0, this, [this]() {
        // 重新应用全局 qss：qss 只在 setStyleSheet() 调用时解析一次，
        // DPI 变化后 pt（字体）和 em（控件尺寸）不会自动更新，需重新应用
        // 让 pt 按新 logicalDotsPerInch 重新换算像素，em 基于新字体重新计算
        StyleManager::reapplyGlobalStyleSheet();
        updateControlWidths();
        onTabChanged(tabWidget->currentIndex());
    });
}

/**
 * @brief 根据当前屏幕逻辑 DPI 计算窗口宽度
 *
 * 96 DPI（100% 缩放）为基准，对应 625 像素；按比例放大并限制在
 * 合理范围（625~1500）。注意：qss 用 em 单位管控件尺寸，但管不了
 * 窗口整体宽度，这里仍需手动按 DPI 算。
 * 各档位参考：100%→625、125%→781、150%→938、175%→1093、
 * 200%→1250、225%→1406、250%→1500（封顶）。
 * @return 基于 DPI 缩放后的窗口宽度
 * @author chiangyang
 */
int SettingsWindow::calculateDpiScaledWidth() const {
    const int baseWidth = 625;
    const int baseDpi = 96;
    QScreen *screen = this->screen();
    int logicalDpi = screen ? screen->logicalDotsPerInch() : baseDpi;
    return qBound(baseWidth, baseWidth * logicalDpi / baseDpi, 1500);
}

/**
 * @brief 重新计算通用选项卡中控件的固定宽度
 *
 * 基于当前字体（随 DPI 缩放）重新计算标签固定宽度与下拉框最小宽度，
 * 确保 DPI 变化后控件宽度能容纳放大后的文本，避免文字被截断。
 * 在 setupGeneralTab 构造时与 DPI 变化时均会调用。
 * @author chiangyang
 */
void SettingsWindow::updateGeneralControlWidths() {
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

/**
 * @brief 重新计算历史记录选项卡下拉框的固定宽度
 *
 * 取保留天数与最大记录数两个下拉框的最大内容宽度作为统一宽度。
 * 在 setupHistoryTab 构造时、语言切换时与 DPI 变化时均会调用，
 * 消除原先 setupHistoryTab 与 retranslateUi 中的重复代码。
 * @author chiangyang
 */
void SettingsWindow::updateHistoryComboWidth() {
    if (!cmbRetentionDays || !cmbMaxItems) {
        return;
    }

    int w1 = cmbRetentionDays->sizeHint().width();
    int w2 = cmbMaxItems->sizeHint().width();
    int maxW = qMax(w1, w2);
    cmbRetentionDays->setFixedWidth(maxW);
    cmbMaxItems->setFixedWidth(maxW);
}

/**
 * @brief 重新计算所有需要固定宽度的控件
 *
 * DPI 变化时调用，依次更新通用选项卡、历史记录选项卡、翻译选项卡
 * 中控件的固定/最小宽度，确保文字不被截断。
 * @author chiangyang
 */
void SettingsWindow::updateControlWidths() {
    updateGeneralControlWidths();
    updateHistoryComboWidth();
    updateTranslateComboWidth();
}

/**
 * @brief 设置UI
 * @author chiangyang
 */
void SettingsWindow::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    tabWidget = new QTabWidget(this);
    
    // 应用选项卡样式
    tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
    
    setupGeneralTab();
    setupShortcutsTab();
    setupStyleTab();
    setupTranslateTab();
    setupHistoryTab();
    setupAboutTab();
    
    mainLayout->addWidget(tabWidget);
    
    // 连接选项卡切换信号，用于自适应调整窗口高度
    connect(tabWidget, &QTabWidget::currentChanged, this, &SettingsWindow::onTabChanged);
}

/**
 * @brief 设置通用选项卡
 * @author chiangyang
 */
void SettingsWindow::setupGeneralTab() {
    m_generalTab = new QWidget();
    m_generalTab->setObjectName("generalTabBg");
    m_generalTab->setStyleSheet(QString("#generalTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *layout = new QVBoxLayout(m_generalTab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    // Language
    QHBoxLayout *langLayout = new QHBoxLayout();
    lblLanguage = new QLabel(TranslationManager::instance()->get("language", "Language:"), m_generalTab);
    langCombo = new QComboBox(m_generalTab);
    langCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    langCombo->addItem("简体中文", "zh_CN");
    langCombo->addItem("繁體中文（香港）", "zh_HK");
    langCombo->addItem("繁體中文（台灣）", "zh_TW");
    langCombo->addItem("English", "en_US");
    langCombo->addItem("日本語", "ja_JP");
    langCombo->addItem("한국어", "ko_KR");
    connect(langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsWindow::onLanguageChanged);
    
    langLayout->addWidget(lblLanguage);
    langLayout->addWidget(langCombo);
    langLayout->addStretch();
    
    // Auto Start
    autoStartCheck = new QCheckBox(TranslationManager::instance()->get("autoStart", "Run on Startup"), m_generalTab);
    autoStartCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(autoStartCheck, &QCheckBox::checkStateChanged, this, &SettingsWindow::onAutoStartChanged);

    // Log Print
    logPrintCheck = new QCheckBox(TranslationManager::instance()->get("logPrint.checkbox", "Enable Log Printing"), m_generalTab);
    logPrintCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(logPrintCheck, &QCheckBox::checkStateChanged, this, &SettingsWindow::onLogPrintChanged);

    btnOpenLogFile = new QPushButton(TranslationManager::instance()->get("logPrint.openLogFile", "Open Log File"), m_generalTab);
    btnOpenLogFile->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnOpenLogFile, &QPushButton::clicked, this, &SettingsWindow::onOpenLogFile);

    btnClearLogFile = new QPushButton(TranslationManager::instance()->get("logPrint.clearLogFile", "Clear Logs"), m_generalTab);
    btnClearLogFile->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnClearLogFile, &QPushButton::clicked, this, &SettingsWindow::onClearLogFile);

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
    lblOcrLanguage = new QLabel(TranslationManager::instance()->get("ocr.language", "OCR Language:"), m_generalTab);
    cmbOcrLanguage = new QComboBox(m_generalTab);
    cmbOcrLanguage->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ch_en", "Chinese + English"), "ch_en");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.en", "English"), "en");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ja", "Japanese"), "ja");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.ko", "Korean"), "ko");
    cmbOcrLanguage->addItem(TranslationManager::instance()->get("ocr.lang.multi", "Multilingual"), "multi");
    connect(cmbOcrLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsWindow::onOcrLanguageChanged);

    ocrLayout->addWidget(lblOcrLanguage);
    ocrLayout->addWidget(cmbOcrLanguage);

    // GPU Acceleration
    gpuAccelCheck = new QCheckBox(TranslationManager::instance()->get("ocr.gpuAccel", "GPU Acceleration"), m_generalTab);
    gpuAccelCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    connect(gpuAccelCheck, &QCheckBox::checkStateChanged, this, &SettingsWindow::onGpuAccelChanged);
#ifdef ENABLE_OCR
    gpuAccelCheck->setVisible(true);
#else
    gpuAccelCheck->setVisible(false);
#endif

    ocrLayout->addWidget(gpuAccelCheck);
    ocrLayout->addStretch();

    layout->addLayout(ocrLayout);

    saveGroup = new QGroupBox(m_generalTab);
    saveGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    auto *saveLayout = new QGridLayout(saveGroup);
    saveLayout->setColumnStretch(1, 1);

    lblCaptureSaveDir = new QLabel(TranslationManager::instance()->get("capture.saveDir", "Screenshot Save:"), saveGroup);
    editCaptureSaveDir = new QLineEdit(saveGroup);
    editCaptureSaveDir->setReadOnly(true);
    editCaptureSaveDir->setStyleSheet(StyleManager::getPathEditStyle());
    btnCaptureChooseDir = new QPushButton(TranslationManager::instance()->get("capture.chooseDir", "Choose"), saveGroup);
    btnCaptureChooseDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnCaptureChooseDir, &QPushButton::clicked, this, &SettingsWindow::onChooseCaptureSaveDir);
    
    btnCaptureResetDir = new QPushButton(TranslationManager::instance()->get("capture.resetDir", "Reset"), saveGroup);
    btnCaptureResetDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnCaptureResetDir, &QPushButton::clicked, this, &SettingsWindow::onResetCaptureSaveDir);

    lblRecordSaveDir = new QLabel(TranslationManager::instance()->get("record.saveDir", "Record Save:"), saveGroup);
    editRecordSaveDir = new QLineEdit(saveGroup);
    editRecordSaveDir->setReadOnly(true);
    editRecordSaveDir->setStyleSheet(StyleManager::getPathEditStyle());
    btnRecordChooseDir = new QPushButton(TranslationManager::instance()->get("record.chooseDir", "Choose"), saveGroup);
    btnRecordChooseDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnRecordChooseDir, &QPushButton::clicked, this, &SettingsWindow::onChooseRecordSaveDir);
    
    btnRecordResetDir = new QPushButton(TranslationManager::instance()->get("record.resetDir", "Reset"), saveGroup);
    btnRecordResetDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnRecordResetDir, &QPushButton::clicked, this, &SettingsWindow::onResetRecordSaveDir);

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
    updateGeneralControlWidths();

    layout->addWidget(saveGroup);

    // Config File Management
    configGroup = new QGroupBox(m_generalTab);
    configGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    auto *configLayout = new QGridLayout(configGroup);
    configLayout->setColumnStretch(1, 1);

    // 标题标签
    lblConfigTitle = new QLabel(TranslationManager::instance()->get("config.title", "Config File:"), m_generalTab);

    // 路径显示文本框
    lblConfigFileName = new QLineEdit(m_generalTab);
    lblConfigFileName->setReadOnly(true);
    lblConfigFileName->setStyleSheet(StyleManager::getPathEditStyle());

    // 按钮
    btnOpenConfigDir = new QPushButton(TranslationManager::instance()->get("config.openLocation", "Open"), m_generalTab);
    btnOpenConfigDir->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnOpenConfigDir, &QPushButton::clicked, this, &SettingsWindow::onOpenConfigFileLocation);

    btnChangeConfig = new QPushButton(TranslationManager::instance()->get("config.change", "Change"), m_generalTab);
    btnChangeConfig->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnChangeConfig, &QPushButton::clicked, this, &SettingsWindow::onChangeConfigFile);

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
    connect(m_configManager, &ConfigManager::configLoaded, this, &SettingsWindow::onConfigChanged);
    connect(m_configManager, &ConfigManager::configSaved, this, &SettingsWindow::onConfigChanged);
    connect(m_configManager, &ConfigManager::configPathChanged, this, &SettingsWindow::onConfigPathChanged);

    // 更新配置文件名称显示
    if (m_configManager->isConfigFileExists()) {
        lblConfigFileName->setText(m_configManager->currentConfigFilePath());
    } else {
        lblConfigFileName->setText("Config file not found");
    }

    layout->addWidget(configGroup);

    layout->addStretch();
    
    tabWidget->addTab(m_generalTab, "General");
}

/**
 * @brief 设置快捷键选项卡
 * @author chiangyang
 */
void SettingsWindow::setupShortcutsTab() {
    QWidget *shortcutsTab = new QWidget();
    shortcutsTab->setObjectName("shortcutsTabBg");
    shortcutsTab->setStyleSheet(QString("#shortcutsTabBg { background-color: %1; }")
                                .arg(StyleManager::getTabWidgetBgColor().name()));
    // 布局必须创建在 shortcutsTab 上，作为该选项卡根容器的布局；
    // 此前误写为 m_shortcutGroupGlobal（此时为 nullptr，且要到下方才 new），
    // 导致 shortcutsTab 无布局、滚动区无法挂载，整个选项卡空白不可见。
    QVBoxLayout *shortcutsTabLayout = new QVBoxLayout(shortcutsTab);
    shortcutsTabLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsTabLayout->setSpacing(0);

    // 滚动容器：承载三个分类分组（参考样式/历史记录选项卡的 QScrollArea 模式）
    QWidget *scrollContainer = new QWidget();
    scrollContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContainer);
    scrollLayout->setContentsMargins(10, 10, 10, 10);
    scrollLayout->setSpacing(10);

    // ===== 分类一：全局热键（可配置）=====
    m_shortcutGroupGlobal = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.global", "Global Hotkeys (Configurable)"), scrollContainer);
    m_shortcutGroupGlobal->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *globalLayout = new QGridLayout(m_shortcutGroupGlobal);
    globalLayout->setContentsMargins(10, 10, 10, 10);
    globalLayout->setSpacing(10);

    // 可配置全局热键：截图/录屏/历史/贴图/全屏/活动窗口/录屏暂停/录屏停止/隐藏贴图（行 0-8）
    // 全部通过 addShortcutRow 数据驱动创建，默认值与 ShortcutTypes.h 的 kShortcutConfigs 数据表保持一致
    addShortcutRow(globalLayout, 0, "snip",         "shortcut_snip",
                   "Alt+Q",        "shortcut.snip.label",        "Snip");
    addShortcutRow(globalLayout, 1, "record",       "shortcut_record",
                   "Alt+S",        "shortcut.record.label",      "Record");
    addShortcutRow(globalLayout, 2, "history",      "shortcut_history",
                   "Alt+H",        "shortcut.history.label",     "History");
    addShortcutRow(globalLayout, 3, "pin",          "shortcut_pin",
                   "Alt+P",        "shortcut.pin.label",         "Pin Clipboard");
    addShortcutRow(globalLayout, 4, "fullscreen",   "shortcut_fullscreen",
                   "Alt+Shift+F",  "shortcut.fullscreen.label",  "Fullscreen Capture");
    addShortcutRow(globalLayout, 5, "activewindow", "shortcut_activewindow",
                   "Alt+Shift+W",  "shortcut.activewindow.label","Active Window Capture");
    addShortcutRow(globalLayout, 6, "recordpause",  "shortcut_recordpause",
                   "Alt+Shift+S",  "shortcut.recordpause.label", "Record Pause/Resume");
    addShortcutRow(globalLayout, 7, "recordstop",   "shortcut_recordstop",
                   "Alt+Shift+Q",  "shortcut.recordstop.label",  "Record Stop");
    addShortcutRow(globalLayout, 8, "togglepins",   "shortcut_togglepins",
                   "Alt+Shift+P",  "shortcut.togglepins.label",  "Toggle All Pins");

    // ===== 分类二：标注工具（1-8 切换工具，固定）=====
    m_shortcutGroupTools = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.tools", "Annotation Tools (Fixed)"), scrollContainer);
    m_shortcutGroupTools->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *toolsLayout = new QGridLayout(m_shortcutGroupTools);
    toolsLayout->setContentsMargins(10, 10, 10, 10);
    toolsLayout->setSpacing(8);
    addFixedShortcutRow(toolsLayout, 0, "shortcut.fixed.rect",            "Rectangle",              "1");
    addFixedShortcutRow(toolsLayout, 1, "shortcut.fixed.ellipse",         "Ellipse",                "2");
    addFixedShortcutRow(toolsLayout, 2, "shortcut.fixed.arrow",           "Arrow",                  "3");
    addFixedShortcutRow(toolsLayout, 3, "shortcut.fixed.pen",             "Pen",                    "4");
    addFixedShortcutRow(toolsLayout, 4, "shortcut.fixed.line",            "Line",                   "5");
    addFixedShortcutRow(toolsLayout, 5, "shortcut.fixed.text",            "Text",                   "6");
    addFixedShortcutRow(toolsLayout, 6, "shortcut.fixed.mosaic",          "Mosaic",                 "7");
    addFixedShortcutRow(toolsLayout, 7, "shortcut.fixed.eraser",          "Eraser",                 "8");

    // ===== 分类三：标注操作（复制/撤销/保存/约束等，固定）=====
    m_shortcutGroupAnnotation = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.annotation", "Annotation Operations (Fixed)"), scrollContainer);
    m_shortcutGroupAnnotation->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *annotationLayout = new QGridLayout(m_shortcutGroupAnnotation);
    annotationLayout->setContentsMargins(10, 10, 10, 10);
    annotationLayout->setSpacing(8);
    addFixedShortcutRow(annotationLayout, 0,  "shortcut.fixed.copyExit",        "Copy & Exit",            "Enter / Ctrl+C");
    addFixedShortcutRow(annotationLayout, 1,  "shortcut.fixed.cancel",          "Cancel",                 "Esc");
    addFixedShortcutRow(annotationLayout, 2,  "shortcut.fixed.save",            "Save to File",           "Ctrl+S");
    addFixedShortcutRow(annotationLayout, 3,  "shortcut.fixed.undo",            "Undo",                   "Ctrl+Z");
    addFixedShortcutRow(annotationLayout, 4,  "shortcut.fixed.redo",            "Redo",                   "Ctrl+Y / Ctrl+Shift+Z");
    addFixedShortcutRow(annotationLayout, 5,  "shortcut.fixed.refresh",         "Refresh",                "F5");
    addFixedShortcutRow(annotationLayout, 6,  "shortcut.fixed.penWidthDec",     "Pen Width -",            "[");
    addFixedShortcutRow(annotationLayout, 7,  "shortcut.fixed.penWidthInc",     "Pen Width +",            "]");
    addFixedShortcutRow(annotationLayout, 8,  "shortcut.fixed.cycleColor",      "Cycle Color",            "Tab");
    addFixedShortcutRow(annotationLayout, 9,  "shortcut.fixed.clear",           "Clear All",              "Delete / Backspace");
    addFixedShortcutRow(annotationLayout, 10, "shortcut.fixed.shiftConstraint",  "Shift Constraint (hold)","Shift");
    addFixedShortcutRow(annotationLayout, 11, "shortcut.fixed.altCenter",       "Center Draw (hold)",     "Alt");

    // ===== 分类四：PinWindow 快捷键（固定，不可配置）=====
    m_shortcutGroupPinWindow = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.pinwindow", "Pin Shortcuts (Fixed)"), scrollContainer);
    m_shortcutGroupPinWindow->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *pinWindowLayout = new QGridLayout(m_shortcutGroupPinWindow);
    pinWindowLayout->setContentsMargins(10, 10, 10, 10);
    pinWindowLayout->setSpacing(8);
    addFixedShortcutRow(pinWindowLayout, 0, "shortcut.fixed.pinCopy",     "Copy Image",          "Ctrl+C");
    addFixedShortcutRow(pinWindowLayout, 1, "shortcut.fixed.pinSave",     "Save Image",          "Ctrl+S");
    addFixedShortcutRow(pinWindowLayout, 2, "shortcut.fixed.pinUndo",     "Undo",                "Ctrl+Z");
    addFixedShortcutRow(pinWindowLayout, 3, "shortcut.fixed.pinRedo",     "Redo",                "Ctrl+Y / Ctrl+Shift+Z");
    addFixedShortcutRow(pinWindowLayout, 4, "shortcut.fixed.pinTool",     "Switch Tool",         "1-8");
    addFixedShortcutRow(pinWindowLayout, 5, "shortcut.fixed.cycleColor",  "Cycle Color",         "Tab");
    addFixedShortcutRow(pinWindowLayout, 6, "shortcut.fixed.penWidthDec", "Pen Width -",         "[");
    addFixedShortcutRow(pinWindowLayout, 7, "shortcut.fixed.penWidthInc", "Pen Width +",         "]");
    addFixedShortcutRow(pinWindowLayout, 8, "shortcut.fixed.clear",       "Clear All",           "Delete / Backspace");
    addFixedShortcutRow(pinWindowLayout, 9, "shortcut.fixed.pinMove",     "Move Window",         "Arrow Keys");
    addFixedShortcutRow(pinWindowLayout, 10, "shortcut.fixed.pinMoveFast", "Move Fast",           "Ctrl+Arrow Keys");
    addFixedShortcutRow(pinWindowLayout, 11, "shortcut.fixed.pinEsc",      "Exit Annotate/Close", "Esc");

    // 组装滚动区域：四个分组纵向排列，末尾拉伸吸收多余空间
    scrollLayout->addWidget(m_shortcutGroupGlobal);
    scrollLayout->addWidget(m_shortcutGroupTools);
    scrollLayout->addWidget(m_shortcutGroupAnnotation);
    scrollLayout->addWidget(m_shortcutGroupPinWindow);
    scrollLayout->addStretch();

    m_shortcutsScrollArea = new QScrollArea();
    m_shortcutsScrollArea->setWidget(scrollContainer);
    m_shortcutsScrollArea->setWidgetResizable(true);
    m_shortcutsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_shortcutsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_shortcutsScrollArea->setFrameShape(QFrame::NoFrame);
    // 不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供
    // （与历史记录选项卡保持一致，避免 setStyleSheet 影响子控件原生渲染）

    shortcutsTabLayout->addWidget(m_shortcutsScrollArea);

    this->setFocus();
    this->installEventFilter(this);
    
    tabWidget->addTab(shortcutsTab, "Shortcuts");
}

/**
 * @brief 数据驱动地添加一行快捷键配置
 *
 * 创建 Label + QKeySequenceEdit + 确定/取消/恢复 三按钮，套用与现有 snip/record 行
 * 一致的样式与固定高度，连接 OK/Cancel/Reset 信号槽读写配置并发射 shortcutChanged。
 * 创建的行信息存入 m_extraShortcutRows，供 retranslateUi 更新文案。
 * @param layout 快捷键选项卡的网格布局
 * @param row 该行在网格中的行号
 * @param action 动作标识
 * @param configKey QSettings 键
 * @param defaultKey 默认快捷键
 * @param trKey 翻译键
 * @param fallback 翻译回退文案
 * @author chiangyang
 */
void SettingsWindow::addShortcutRow(QGridLayout *layout, int row, const QString &action,
                                    const QString &configKey, const QString &defaultKey,
                                    const QString &trKey, const QString &fallback) {
    TranslationManager *tm = TranslationManager::instance();
    QWidget *parent = layout->parentWidget();

    ShortcutRow r;
    r.action = action;
    r.configKey = configKey;
    r.defaultKey = defaultKey;
    r.trKey = trKey;
    r.fallback = fallback;

    r.label = new QLabel(tm->get(trKey, fallback) + ":", parent);
    r.edit = new QKeySequenceEdit(parent);
    r.edit->setMaximumSequenceLength(1);
    r.edit->setStyleSheet(StyleManager::getKeySequenceEditStyle());
    r.edit->installEventFilter(this);

    QString buttonStyle = StyleManager::getSettingsButtonStyle();
    r.btnCancel = new QPushButton(tm->get("cancel", "Cancel"), parent);
    r.btnOk = new QPushButton(tm->get("ok", "OK"), parent);
    r.btnReset = new QPushButton(tm->get("reset", "Reset"), parent);
    r.btnCancel->setStyleSheet(buttonStyle);
    r.btnOk->setStyleSheet(buttonStyle);
    r.btnReset->setStyleSheet(buttonStyle);

    // 与现有行保持一致的固定高度
    int inputHeight = r.edit->sizeHint().height();
    r.edit->setFixedHeight(inputHeight);
    r.label->setFixedHeight(inputHeight);
    r.btnCancel->setFixedHeight(inputHeight);
    r.btnOk->setFixedHeight(inputHeight);
    r.btnReset->setFixedHeight(inputHeight);

    // 加载当前配置值（统一通过 ShortcutTypes::getShortcutSequence 读取，
    // 确保 history 等旧键名向后兼容；未知类型回退到直接读取 configKey）
    QString currentKey = defaultKey;
    auto type = shortcutTypeFromString(action);
    if (type.has_value()) {
        const ShortcutConfigItem *cfg = getShortcutConfig(*type);
        if (cfg) {
            currentKey = getShortcutSequence(*cfg).toString();
        }
    } else {
        currentKey = ConfigManager::instance()->getSettings()->value(configKey, defaultKey).toString();
    }
    r.edit->setKeySequence(QKeySequence(currentKey));

    // 布局：标签 | 输入框 | 取消 | 确定 | 恢复
    layout->addWidget(r.label, row, 0);
    layout->addWidget(r.edit, row, 1);
    layout->addWidget(r.btnCancel, row, 2);
    layout->addWidget(r.btnOk, row, 3);
    layout->addWidget(r.btnReset, row, 4);

    // 确定：保存配置并通知外部更新热键
    connect(r.btnOk, &QPushButton::clicked, this, [this, r]() {
        QKeySequence seq = r.edit->keySequence();
        ConfigManager::instance()->getSettings()->setValue(r.configKey, seq.toString());
        ConfigManager::instance()->getSettings()->sync();
        emit shortcutChanged(r.action, seq);
    });
    // 取消：从配置读回当前值
    connect(r.btnCancel, &QPushButton::clicked, this, [this, r]() {
        QString key = ConfigManager::instance()->getSettings()->value(r.configKey, r.defaultKey).toString();
        r.edit->setKeySequence(QKeySequence(key));
    });
    // 恢复：重置为默认值并通知
    connect(r.btnReset, &QPushButton::clicked, this, [this, r]() {
        r.edit->setKeySequence(QKeySequence(r.defaultKey));
        ConfigManager::instance()->getSettings()->setValue(r.configKey, r.defaultKey);
        ConfigManager::instance()->getSettings()->sync();
        emit shortcutChanged(r.action, QKeySequence(r.defaultKey));
    });

    m_extraShortcutRows.append(r);
}

/**
 * @brief 数据驱动地添加一行固定快捷键展示
 *
 * 创建 Label（功能名）+ QLineEdit（只读禁用，展示键位文本），套用与可配置行
 * 一致的输入框样式与固定高度。键位输入框 setReadOnly(true)+setEnabled(false)
 * 置灰，明确传达不可编辑。创建的行信息存入 m_fixedShortcutRows，供 retranslateUi
 * 更新功能名文案。键位文本 keys 硬编码无需翻译。
 * @param layout 所属分组的网格布局
 * @param row 该行在网格中的行号
 * @param trKey 功能名翻译键
 * @param fallback 翻译回退文案
 * @param keys 键位文本（硬编码，如 "Ctrl+S" / "1" / "Ctrl+Y / Ctrl+Shift+Z"）
 * @author chiangyang
 */
void SettingsWindow::addFixedShortcutRow(QGridLayout *layout, int row,
        const QString &trKey, const QString &fallback, const QString &keys) {
    TranslationManager *tm = TranslationManager::instance();
    QWidget *parent = layout->parentWidget();

    FixedShortcutRow r;
    r.trKey = trKey;
    r.fallback = fallback;
    r.keys = keys;

    r.label = new QLabel(tm->get(trKey, fallback) + ":", parent);
    r.edit = new QLineEdit(keys, parent);
    r.edit->setReadOnly(true);
    r.edit->setEnabled(false);  // 置灰，明确不可编辑
    r.edit->setStyleSheet(StyleManager::getKeySequenceEditStyle());
    r.edit->setAlignment(Qt::AlignCenter);

    // 固定高度与可配置行一致（复用 QLineEdit 的 sizeHint 高度）
    int inputHeight = r.edit->sizeHint().height();
    r.label->setFixedHeight(inputHeight);
    r.edit->setFixedHeight(inputHeight);

    // 布局：功能名 | 键位（跨 4 列填满，与可配置行的 输入框+按钮 列对齐，视觉整齐）
    layout->addWidget(r.label, row, 0);
    layout->addWidget(r.edit, row, 1, 1, 4);

    m_fixedShortcutRows.append(r);
}

/**
 * @brief 设置翻译选项卡
 * @author chiangyang
 */
void SettingsWindow::setupTranslateTab() {
    TranslationManager *tm = TranslationManager::instance();
    QSettings *settings = ConfigManager::instance()->getSettings();

    m_translateTab = new QWidget();
    m_translateTab->setObjectName("translateTabBg");
    m_translateTab->setStyleSheet(QString("#translateTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *layout = new QVBoxLayout(m_translateTab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // ===== 翻译引擎组 =====
    QGroupBox *engineGroup = new QGroupBox(tm->get("translate.engine", "Translation Engine"), m_translateTab);
    m_translateEngineGroup = engineGroup;
    engineGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *engineLayout = new QVBoxLayout(engineGroup);
    engineLayout->setContentsMargins(10, 10, 10, 10);
    engineLayout->setSpacing(8);

    // 引擎选择行
    QHBoxLayout *engineRow = new QHBoxLayout();
    lblTranslateEngine = new QLabel(tm->get("translate.engine", "Translation Engine") + ":", engineGroup);
    m_translateEngineCombo = new QComboBox(engineGroup);
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
    m_translateOptionsGroup = new QGroupBox(tm->get("translate.options", "Translation Options"), m_translateTab);
    m_translateOptionsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *optionsLayout = new QVBoxLayout(m_translateOptionsGroup);
    optionsLayout->setContentsMargins(10, 10, 10, 10);
    optionsLayout->setSpacing(8);

    // 目标语言行
    QHBoxLayout *langRow = new QHBoxLayout();
    lblTranslateLang = new QLabel(tm->get("translate.targetLang", "Target Language") + ":", m_translateOptionsGroup);
    m_translateLangCombo = new QComboBox(m_translateOptionsGroup);
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
    updateTranslateComboWidth();

    tabWidget->addTab(m_translateTab, "Translate");

    // 从配置加载当前值
    QString engineName = settings->value("translate/engine", "mymemory").toString();
    int eIdx = m_translateEngineCombo->findData(engineName);
    if (eIdx >= 0) {
        m_translateEngineCombo->setCurrentIndex(eIdx);
    }
    m_mymemoryEmailEdit->setText(settings->value("translate/mymemoryEmail", "").toString());
    m_baiduAppIdEdit->setText(settings->value("translate/baiduAppId", "").toString());
    m_baiduKeyEdit->setText(settings->value("translate/baiduKey", "").toString());
    m_deeplKeyEdit->setText(settings->value("translate/deeplKey", "").toString());
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
            this, &SettingsWindow::onTranslateEngineChanged);
    connect(m_translateEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_translateLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_mymemoryEmailEdit, &QLineEdit::textChanged, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_baiduAppIdEdit, &QLineEdit::textChanged, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_baiduKeyEdit, &QLineEdit::textChanged, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_deeplKeyEdit, &QLineEdit::textChanged, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_libreUrlEdit, &QLineEdit::textChanged, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_translateEnabledCheck, &QCheckBox::toggled, this, &SettingsWindow::onTranslateSettingChanged);
    connect(m_translatePrivacyCheck, &QCheckBox::toggled, this, &SettingsWindow::onTranslateSettingChanged);
}

/**
 * @brief 翻译引擎切换槽函数，显隐对应引擎配置行
 * @param index 引擎下拉框索引
 * @author chiangyang
 */
void SettingsWindow::onTranslateEngineChanged(int index) {
    Q_UNUSED(index);
    QString name = m_translateEngineCombo->currentData().toString();
    m_mymemoryConfigWidget->setVisible(name == "mymemory");
    m_baiduConfigWidget->setVisible(name == "baidu");
    m_deeplConfigWidget->setVisible(name == "deepl");
    m_libreConfigWidget->setVisible(name == "libretranslate");
}

/**
 * @brief 翻译设置改变槽函数（保存配置并重载翻译引擎）
 * @author chiangyang
 */
void SettingsWindow::onTranslateSettingChanged() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->setValue("translate/engine", m_translateEngineCombo->currentData().toString());
    settings->setValue("translate/targetLang", m_translateLangCombo->currentData().toString());
    settings->setValue("translate/mymemoryEmail", m_mymemoryEmailEdit->text());
    settings->setValue("translate/baiduAppId", m_baiduAppIdEdit->text());
    settings->setValue("translate/baiduKey", m_baiduKeyEdit->text());
    settings->setValue("translate/deeplKey", m_deeplKeyEdit->text());
    settings->setValue("translate/libreUrl", m_libreUrlEdit->text());
    settings->setValue("translate/enabled", m_translateEnabledCheck->isChecked());
    settings->setValue("translate/showPrivacyWarning", m_translatePrivacyCheck->isChecked());
    settings->sync();

    // 重新加载翻译引擎配置
    TranslateService::instance()->loadConfig(settings);
    LOG_INFO("SettingsWindow: translate settings saved and reloaded");
}

/**
 * @brief 更新翻译选项卡两个下拉框宽度
 *
 * 取翻译引擎下拉框与目标语言下拉框中所有条目文本最宽者，
 * 统一设置二者的最小宽度，保证两框宽度一致（谁长选谁）。
 * @author chiangyang
 */
void SettingsWindow::updateTranslateComboWidth() {
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

/**
 * @brief 设置关于选项卡
 * @author chiangyang
 */
void SettingsWindow::setupAboutTab() {
    QWidget *aboutTab = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(aboutTab);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // Top Row: Info Left, Qt Right
    QHBoxLayout *topLayout = new QHBoxLayout();
    
    // Left Info
    QVBoxLayout *infoLayout = new QVBoxLayout();
    appNameLabel = new QLabel("QuickShot", aboutTab);
    appNameLabel->setStyleSheet(StyleManager::getAppNameLabelStyle());
    versionLabel = new QLabel(QString("Version %1").arg(qApp->applicationVersion()), aboutTab);
    copyrightLabel = new QLabel("Copyright © 2026 QuickShot Inc.", aboutTab);
    
    infoLayout->addWidget(appNameLabel);
    infoLayout->addWidget(versionLabel);
    infoLayout->addWidget(copyrightLabel);
    
    // Right Link
    qtLinkLabel = new QLabel("<a href='https://www.qt.io'>Build With QT</a>", aboutTab);
    qtLinkLabel->setOpenExternalLinks(true);
    qtLinkLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    
    topLayout->addLayout(infoLayout);
    topLayout->addStretch();
    topLayout->addWidget(qtLinkLabel);
    
    // 仓库链接
    githubLabel = new QLabel("<a href='https://github.com/chiangyangNPU/quick-shot'>GitHub: github.com/chiangyangNPU/quick-shot</a>", aboutTab);
    githubLabel->setOpenExternalLinks(true);
    giteeLabel = new QLabel("<a href='https://gitee.com/chiangyangNPU/quick-shot'>Gitee: gitee.com/chiangyangNPU/quick-shot</a>", aboutTab);
    giteeLabel->setOpenExternalLinks(true);
    emailLabel = new QLabel("Email: <a href='mailto:chiangyangnpu@163.com'>chiangyangnpu@163.com</a>", aboutTab);
    emailLabel->setOpenExternalLinks(true);

    mainLayout->addLayout(topLayout);
    mainLayout->addSpacing(20);
    mainLayout->addWidget(githubLabel);
    mainLayout->addSpacing(5);
    mainLayout->addWidget(giteeLabel);
    mainLayout->addSpacing(5);
    mainLayout->addWidget(emailLabel);
    mainLayout->addSpacing(15);
    
    // Update Group
    m_updateGroup = new QGroupBox(aboutTab);
    m_updateGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    m_updateGroup->setTitle(TranslationManager::instance()->get("update.groupTitle", "Software Update"));
    QVBoxLayout *updateLayout = new QVBoxLayout(m_updateGroup);
    updateLayout->setSpacing(8);
    
    lblUpdateStatus = new ElidedLabel(aboutTab);
    lblUpdateStatus->setWordWrap(false);
    lblUpdateStatus->setAlignment(Qt::AlignCenter);
    updateLayout->addWidget(lblUpdateStatus);
    
    btnCheckUpdate = new QPushButton(aboutTab);
    btnCheckUpdate->setObjectName("primaryButton");
    btnCheckUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    // 防止检查/下载过程中状态文字与渠道提示出现时把按钮高度压扁：
    // 关于页高度固定，QSS height 只是首选高度（可被布局压缩），
    // 以当前样式下的 sizeHint 作为硬性最小高度，布局便无法再压缩它。
    btnCheckUpdate->setMinimumHeight(btnCheckUpdate->sizeHint().height());
    updateLayout->addWidget(btnCheckUpdate);
    
    btnDownloadUpdate = new QPushButton(aboutTab);
    btnDownloadUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnDownloadUpdate->setMinimumHeight(btnDownloadUpdate->sizeHint().height());
    btnDownloadUpdate->hide();
    updateLayout->addWidget(btnDownloadUpdate);
    
    m_updateProgressBar = new QProgressBar(aboutTab);
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setStyleSheet(StyleManager::getProgressBarStyle());
    m_updateProgressBar->hide();
    updateLayout->addWidget(m_updateProgressBar);
    
    btnCancelUpdate = new QPushButton(aboutTab);
    btnCancelUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnCancelUpdate->setMinimumHeight(btnCancelUpdate->sizeHint().height());
    btnCancelUpdate->hide();
    updateLayout->addWidget(btnCancelUpdate);
    
    btnInstallUpdate = new QPushButton(aboutTab);
    btnInstallUpdate->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnInstallUpdate->setMinimumHeight(btnInstallUpdate->sizeHint().height());
    btnInstallUpdate->hide();
    updateLayout->addWidget(btnInstallUpdate);
    
    lblUpdateChannel = new QLabel(aboutTab);
    lblUpdateChannel->setAlignment(Qt::AlignCenter);
    lblUpdateChannel->hide();
    updateLayout->addWidget(lblUpdateChannel);
    
    mainLayout->addWidget(m_updateGroup);
    mainLayout->addStretch();
    
    // Update manager
    m_updateManager = new UpdateManager(this);
    
    connect(btnCheckUpdate, &QPushButton::clicked, this, &SettingsWindow::onCheckForUpdate);
    connect(btnDownloadUpdate, &QPushButton::clicked, this, &SettingsWindow::onDownloadUpdate);
    connect(btnInstallUpdate, &QPushButton::clicked, this, &SettingsWindow::onInstallUpdate);
    connect(btnCancelUpdate, &QPushButton::clicked, this, &SettingsWindow::onCancelUpdate);
    
    connect(m_updateManager, &UpdateManager::checkFinished, this, &SettingsWindow::onUpdateCheckFinished);
    connect(m_updateManager, &UpdateManager::downloadProgress, this, &SettingsWindow::onUpdateDownloadProgress);
    connect(m_updateManager, &UpdateManager::downloadFinished, this, &SettingsWindow::onUpdateDownloadFinished);
    connect(m_updateManager, &UpdateManager::installFinished, this, &SettingsWindow::onUpdateInstallFinished);
    connect(m_updateManager, &UpdateManager::channelSwitched, this,
        [this](UpdateManager::Channel, UpdateManager::Channel to, const QString &reason) {
            TranslationManager *tm = TranslationManager::instance();
            QString channelName = to == UpdateManager::Channel::GitHub ? "GitHub" :
                to == UpdateManager::Channel::Gitee ? "Gitee" :
                tm->get("update.channelOfficial", "Official Website");
            lblUpdateChannel->setText(tm->get("update.channelSwitch", "Switched to %1 (%2)").arg(channelName, reason));
            lblUpdateChannel->show();
            fitWindowHeight();
        });

    tabWidget->addTab(aboutTab, TranslationManager::instance()->get("tabAbout", "About"));
}

/**
 * @brief 加载设置
 * @author chiangyang
 */
void SettingsWindow::loadSettings() {
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

    // 快捷键配置由 addShortcutRow 在创建时从 ConfigManager 加载，无需在此重复读取

    const QString defaultCaptureDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString captureDir = settings->value("capture/saveDir", defaultCaptureDir).toString();
    editCaptureSaveDir->setText(captureDir);
    editCaptureSaveDir->setToolTip(captureDir);

    const QString defaultRecordDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString recordDir = settings->value("record/saveDir", defaultRecordDir).toString();
    editRecordSaveDir->setText(recordDir);
    editRecordSaveDir->setToolTip(recordDir);
    
    // Style
    loadStyleSettings();
    
    retranslateUi();
}

/**
 * @brief 语言改变槽函数
 * @param index 语言下拉框索引
 * @author chiangyang
 */
void SettingsWindow::onLanguageChanged(int index) {
    QString langCode = langCombo->itemData(index).toString();
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->setValue("language", langCode);
    settings->sync();
    
    // 使用TranslationManager加载语言
    bool success = TranslationManager::instance()->loadLanguage(langCode);
    if (success) {
        retranslateUi();
    } else {
        // 语言加载失败，显示错误消息
        MessageBox::warning(this,
            TranslationManager::instance()->get("error", "Error"),
            TranslationManager::instance()->get("language.loadFailed", "Failed to load language file."));
    }
}

/**
 * @brief 重新翻译UI
 * @author chiangyang
 */
void SettingsWindow::retranslateUi() {
    TranslationManager* tm = TranslationManager::instance();
    
    setWindowTitle(tm->get("windowTitle", "Settings"));
    tabWidget->setTabText(0, tm->get("tabGeneral", "General"));
    tabWidget->setTabText(1, tm->get("tabShortcuts", "Shortcuts"));
    tabWidget->setTabText(2, tm->get("tabStyle", "Style"));
    tabWidget->setTabText(3, tm->get("tabTranslate", "Translate"));
    tabWidget->setTabText(4, tm->get("history.tabName", "History"));
    tabWidget->setTabText(5, tm->get("tabAbout", "About"));

    // Update GroupBox title
    if (m_updateGroup) {
        m_updateGroup->setTitle(tm->get("update.groupTitle", "Software Update"));
    }

    // Update translations
    if (btnCheckUpdate) {
        btnCheckUpdate->setText(tm->get("update.check", "Check for Updates"));
    }
    if (btnDownloadUpdate) {
#ifdef Q_OS_WIN
        btnDownloadUpdate->setText(tm->get("update.download", "Download Update"));
#else
        // macOS：dmg 分发 + ad-hoc 签名，自动替换 .app 会被 Gatekeeper 拦截
        // 改为引导用户到浏览器下载 dmg 手动安装；点击后退出程序避免覆盖正在运行的 .app
        btnDownloadUpdate->setText(tm->get("update.exitAndDownload", "Exit and Download"));
#endif
    }
    if (btnInstallUpdate) {
        btnInstallUpdate->setText(tm->get("update.install", "Install and Restart"));
    }
    if (btnCancelUpdate) {
        btnCancelUpdate->setText(tm->get("update.cancel", "Cancel"));
    }
    // 更新状态文字（检查中/失败/下载完成等）按当前缓存键重新渲染
    renderUpdateStatusText();

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
        updateTranslateComboWidth();
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
    
    lblLanguage->setText(tm->get("language", "Language:"));
    autoStartCheck->setText(tm->get("autoStart", "Run on Startup"));
    if (logPrintCheck) logPrintCheck->setText(tm->get("logPrint.checkbox", "Enable Log Printing"));
    if (btnOpenLogFile) btnOpenLogFile->setText(tm->get("logPrint.openLogFile", "Open Log File"));
    if (btnClearLogFile) btnClearLogFile->setText(tm->get("logPrint.clearLogFile", "Clear Logs"));

    // OCR Language
    if (lblOcrLanguage) lblOcrLanguage->setText(tm->get("ocr.language", "OCR Language:"));
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

    // 可配置全局热键行的文案（截图/录屏/历史/贴图等全部由 m_extraShortcutRows 统一管理）
    for (const ShortcutRow &r : m_extraShortcutRows) {
        if (r.label) r.label->setText(tm->get(r.trKey, r.fallback) + ":");
        if (r.btnOk) r.btnOk->setText(tm->get("ok", "OK"));
        if (r.btnCancel) r.btnCancel->setText(tm->get("cancel", "Cancel"));
        if (r.btnReset) r.btnReset->setText(tm->get("reset", "Reset"));
    }

    // 快捷键选项卡分类标题
    if (m_shortcutGroupGlobal) m_shortcutGroupGlobal->setTitle(tm->get("shortcut.section.global", "Global Hotkeys (Configurable)"));
    if (m_shortcutGroupTools) m_shortcutGroupTools->setTitle(tm->get("shortcut.section.tools", "Annotation Tools (Fixed)"));
    if (m_shortcutGroupAnnotation) m_shortcutGroupAnnotation->setTitle(tm->get("shortcut.section.annotation", "Annotation Operations (Fixed)"));
    if (m_shortcutGroupPinWindow) m_shortcutGroupPinWindow->setTitle(tm->get("shortcut.section.pinwindow", "Pin Shortcuts (Fixed)"));
    // 固定快捷键行功能名文案（键位文本 keys 硬编码无需翻译）
    for (const FixedShortcutRow &fr : m_fixedShortcutRows) {
        if (fr.label) fr.label->setText(tm->get(fr.trKey, fr.fallback) + ":");
    }

    // Style
    if (borderGroup) borderGroup->setTitle(tm->get("style.borderColor", "Border Colors"));
    if (toolbarGroup) toolbarGroup->setTitle(tm->get("style.toolbarStyle", "Toolbar Style"));
    // Tab Style Group
    QGroupBox *tabGroup = findChild<QGroupBox*>("tabGroup");
    if (tabGroup) tabGroup->setTitle(tm->get("style.tabStyle", "Tab Style"));

    // 通过数据驱动表循环更新所有颜色标签的翻译文本（表归 StyleManager 所有）
    {
        const auto& table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            if (m_colorLabels[i] && table[i].defaultText[0] != '\0') {
                m_colorLabels[i]->setText(tm->get(table[i].translationKey, table[i].defaultText));
            }
        }
    }

    if (btnResetStyle) btnResetStyle->setText(tm->get("style.resetStyle", "Reset to Default"));
    // 标注工具默认值组
    if (m_annotationDefaultsGroup) m_annotationDefaultsGroup->setTitle(tm->get("style.annotationDefaults", "Annotation Defaults"));
    if (lblDefaultPenWidth) lblDefaultPenWidth->setText(tm->get("style.defaultPenWidth", "Default Pen Width:"));
    if (lblDefaultFontSize) lblDefaultFontSize->setText(tm->get("style.defaultFontSize", "Default Font Size:"));
    if (lblDefaultEraserWidth) lblDefaultEraserWidth->setText(tm->get("style.defaultEraserWidth", "Default Eraser Width:"));
    if (lblDefaultMosaicSize) lblDefaultMosaicSize->setText(tm->get("style.defaultMosaicSize", "Default Mosaic Size:"));
    if (lblToolbarButtonStyle) lblToolbarButtonStyle->setText(tm->get("style.toolbarButtonStyle", "Button Style:"));
    if (cmbToolbarButtonStyle) {
        int currentIndex = cmbToolbarButtonStyle->currentIndex();
        cmbToolbarButtonStyle->clear();
        cmbToolbarButtonStyle->addItem(tm->get("style.buttonStyle.text", "Text"), "text");
        cmbToolbarButtonStyle->addItem(tm->get("style.buttonStyle.icon", "Icon"), "icon");
        cmbToolbarButtonStyle->setCurrentIndex(currentIndex);
    }

    // History
    if (historyRecordGroup) historyRecordGroup->setTitle(tm->get("history.recordSettings", "Record Settings"));
    if (m_screenshotHistoryCheck) m_screenshotHistoryCheck->setText(tm->get("history.enableScreenshot", "Record Screenshot History"));
    if (m_clipboardHistoryCheck) m_clipboardHistoryCheck->setText(tm->get("history.enableClipboard", "Record Clipboard History"));
    if (historyStorageGroup) historyStorageGroup->setTitle(tm->get("history.storage.title", "Storage Settings"));
    if (lblRetentionDays) lblRetentionDays->setText(tm->get("history.storage.retentionDays", "Retention Period") + ":");
    if (cmbRetentionDays) {
        int retentionIdx = cmbRetentionDays->currentIndex();
        cmbRetentionDays->blockSignals(true);
        cmbRetentionDays->clear();
        cmbRetentionDays->addItem(tm->get("history.storage.days7", "7 days"), 7);
        cmbRetentionDays->addItem(tm->get("history.storage.days30", "30 days"), 30);
        cmbRetentionDays->addItem(tm->get("history.storage.days90", "90 days"), 90);
        cmbRetentionDays->addItem(tm->get("history.storage.days180", "180 days"), 180);
        cmbRetentionDays->addItem(tm->get("history.storage.days365", "365 days"), 365);
        cmbRetentionDays->setCurrentIndex(retentionIdx);
        cmbRetentionDays->blockSignals(false);
    }
    if (lblMaxItems) lblMaxItems->setText(tm->get("history.storage.maxItems", "Max Records") + ":");
    // 语言切换后重新计算下拉框宽度（复用 updateHistoryComboWidth，消除重复）
    updateHistoryComboWidth();
    if (historyDataGroup) historyDataGroup->setTitle(tm->get("history.dataManagement.title", "Data Management"));
    if (btnCleanHistory) btnCleanHistory->setText(tm->get("history.dataManagement.cleanExpired", "Clean Expired Records"));
    if (btnClearHistory) btnClearHistory->setText(tm->get("history.dataManagement.clearAll", "Clear All History"));
    if (historyStatsGroup) historyStatsGroup->setTitle(tm->get("history.stats.title", "Statistics"));
    updateHistoryStats();

    // Update About info if needed (static mostly)
    if (versionLabel) {
        versionLabel->setText(tm->get("versionFormat", "Version %1")
            .arg(qApp->applicationVersion()));
    }
}

/**
 * @brief 选择截图保存目录
 * @author chiangyang
 */
void SettingsWindow::onChooseCaptureSaveDir() {
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
void SettingsWindow::onChooseRecordSaveDir() {
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
void SettingsWindow::onResetCaptureSaveDir() {
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
void SettingsWindow::onResetRecordSaveDir() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    settings->setValue("record/saveDir", defaultDir);
    settings->sync();
    editRecordSaveDir->setText(defaultDir);
    editRecordSaveDir->setToolTip(defaultDir);
}

/**
 * @brief 打开配置文件所在文件夹
 * @author chiangyang
 */
void SettingsWindow::onOpenConfigFileLocation() {
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

/**
 * @brief 更改配置文件
 * @author chiangyang
 */
void SettingsWindow::onChangeConfigFile() {
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

/**
 * @brief 配置文件更改完成槽函数
 * @param success 是否成功
 * @param message 消息
 * @author chiangyang
 */
void SettingsWindow::onConfigChanged(bool success, const QString &message) {
    if (!success) {
        MessageBox::warning(this,
            TranslationManager::instance()->get("config.errorTitle", "Error"),
            message);
    }
}

/**
 * @brief 配置文件路径更改槽函数
 * @param newPath 新路径
 * @author chiangyang
 */
void SettingsWindow::onConfigPathChanged(const QString &newPath) {
    // 更新配置文件名称显示
    if (lblConfigFileName) {
        lblConfigFileName->setText(newPath);
    }

    // 显示成功消息
    MessageBox::information(this,
        TranslationManager::instance()->get("config.successTitle", "Success"),
        TranslationManager::instance()->get("config.changeSuccess", "Config file changed successfully"));
}

/**
 * @brief 自动启动改变槽函数
 * @param state 复选框状态
 * @author chiangyang
 */
void SettingsWindow::onAutoStartChanged(Qt::CheckState state) {
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

/**
 * @brief 日志打印改变槽函数
 * @param state 复选框状态
 * @author chiangyang
 */
void SettingsWindow::onLogPrintChanged(Qt::CheckState state) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    bool enabled = (state == Qt::Checked);
    settings->setValue("logPrintEnabled", enabled);
    settings->sync();
    Logger::instance()->setLogEnabled(enabled);
}

/**
 * @brief OCR 语言改变槽函数
 * @param index 下拉框索引
 * @author chiangyang
 */
void SettingsWindow::onOcrLanguageChanged(int index) {
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

/**
 * @brief GPU 加速改变槽函数
 * @param state 复选框状态
 * @author chiangyang
 */
void SettingsWindow::onGpuAccelChanged(Qt::CheckState state) {
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

/**
 * @brief 打开日志文件夹槽函数
 * @author chiangyang
 */
void SettingsWindow::onOpenLogFile() {
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

/**
 * @brief 清除日志文件槽函数
 * @author chiangyang
 */
void SettingsWindow::onClearLogFile() {
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

/**
 * @brief 设置样式选项卡
 * @author chiangyang
 */
void SettingsWindow::setupStyleTab() {
    QWidget *styleTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(styleTab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    // 边框颜色设置
    auto *tm = TranslationManager::instance();
    borderGroup = new QGroupBox(tm->get("style.borderColor", "Border Colors"), styleTab);
    borderGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *borderLayout = new QVBoxLayout(borderGroup);
    borderLayout->setContentsMargins(10, 10, 10, 10);
    borderLayout->setSpacing(8);

    // 工具栏样式设置
    toolbarGroup = new QGroupBox(TranslationManager::instance()->get("style.toolbarStyle", "Toolbar Style"), styleTab);
    toolbarGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *toolbarLayout = new QVBoxLayout(toolbarGroup);
    toolbarLayout->setContentsMargins(10, 10, 10, 10);
    toolbarLayout->setSpacing(8);

    // 选项卡样式设置
    tabGroup = new QGroupBox(TranslationManager::instance()->get("style.tabStyle", "Tab Style"), styleTab);
    tabGroup->setObjectName("tabGroup");
    tabGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *tabLayout = new QVBoxLayout(tabGroup);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    tabLayout->setSpacing(8);

    // 通过数据驱动表循环创建所有颜色行（元数据表归 StyleManager 所有，此处只消费）
    const auto& table = StyleManager::colorSettingTable();
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& s = table[i];

        // 根据分类决定加入哪个 GroupBox
        QGroupBox* group = nullptr;
        switch (s.category) {
            case StyleManager::StyleColorCategory::Border:     group = borderGroup;  break;
            case StyleManager::StyleColorCategory::Toolbar:    group = toolbarGroup; break;
            case StyleManager::StyleColorCategory::TabButton:
            case StyleManager::StyleColorCategory::TabWidgetBg: group = tabGroup;    break;
        }
        if (!group || s.defaultText[0] == '\0') {
            continue;  // defaultText 为空的行（SettingButton* / 角手柄）无 UI
        }

        QPushButton* btn = nullptr;
        QLabel* lbl = nullptr;
        QHBoxLayout* row = createColorRow(
            group, tm->get(s.translationKey, s.defaultText),
            s.getter(), btn, lbl);

        m_colorButtons[i] = btn;
        m_colorLabels[i] = lbl;

        // 用 lambda 捕获 StyleColorId 调用 applyColorChange
        connect(btn, &QPushButton::clicked, this, [this, id = s.id]() {
            applyColorChange(id);
        });

        QVBoxLayout* grpLayout = qobject_cast<QVBoxLayout*>(group->layout());
        if (grpLayout) {
            grpLayout->addLayout(row);
        }
    }

    // 工具栏按钮样式（非颜色行，单独处理）
    QHBoxLayout *toolbarButtonStyleLayout = new QHBoxLayout();
    lblToolbarButtonStyle = new QLabel(TranslationManager::instance()->get("style.toolbarButtonStyle", "Button Style:"), toolbarGroup);
    cmbToolbarButtonStyle = new QComboBox(toolbarGroup);
    cmbToolbarButtonStyle->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbToolbarButtonStyle->addItem(TranslationManager::instance()->get("style.buttonStyle.text", "Text"), "text");
    cmbToolbarButtonStyle->addItem(TranslationManager::instance()->get("style.buttonStyle.icon", "Icon"), "icon");
    connect(cmbToolbarButtonStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsWindow::onToolbarButtonStyleChanged);
    toolbarButtonStyleLayout->addWidget(lblToolbarButtonStyle);
    toolbarButtonStyleLayout->addStretch();
    toolbarButtonStyleLayout->addWidget(cmbToolbarButtonStyle);
    toolbarLayout->addLayout(toolbarButtonStyleLayout);

    // 标注工具默认值设置
    m_annotationDefaultsGroup = new QGroupBox(tm->get("style.annotationDefaults", "Annotation Defaults"), styleTab);
    m_annotationDefaultsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *annotationDefaultsLayout = new QVBoxLayout(m_annotationDefaultsGroup);
    annotationDefaultsLayout->setContentsMargins(10, 10, 10, 10);
    annotationDefaultsLayout->setSpacing(8);

    // 画笔默认粗细
    QHBoxLayout *penWidthLayout = new QHBoxLayout();
    lblDefaultPenWidth = new QLabel(tm->get("style.defaultPenWidth", "Default Pen Width:"), m_annotationDefaultsGroup);
    spnDefaultPenWidth = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultPenWidth->setRange(1, 20);
    spnDefaultPenWidth->setSuffix(" px");
    spnDefaultPenWidth->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
    penWidthLayout->addWidget(lblDefaultPenWidth);
    penWidthLayout->addStretch();
    penWidthLayout->addWidget(spnDefaultPenWidth);
    annotationDefaultsLayout->addLayout(penWidthLayout);

    // 文本默认字号
    QHBoxLayout *fontSizeLayout = new QHBoxLayout();
    lblDefaultFontSize = new QLabel(tm->get("style.defaultFontSize", "Default Font Size:"), m_annotationDefaultsGroup);
    spnDefaultFontSize = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultFontSize->setRange(8, 48);
    spnDefaultFontSize->setSuffix(" px");
    spnDefaultFontSize->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
    fontSizeLayout->addWidget(lblDefaultFontSize);
    fontSizeLayout->addStretch();
    fontSizeLayout->addWidget(spnDefaultFontSize);
    annotationDefaultsLayout->addLayout(fontSizeLayout);

    // 橡皮擦默认粗细
    QHBoxLayout *eraserWidthLayout = new QHBoxLayout();
    lblDefaultEraserWidth = new QLabel(tm->get("style.defaultEraserWidth", "Default Eraser Width:"), m_annotationDefaultsGroup);
    spnDefaultEraserWidth = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultEraserWidth->setRange(1, 20);
    spnDefaultEraserWidth->setSuffix(" px");
    spnDefaultEraserWidth->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
    eraserWidthLayout->addWidget(lblDefaultEraserWidth);
    eraserWidthLayout->addStretch();
    eraserWidthLayout->addWidget(spnDefaultEraserWidth);
    annotationDefaultsLayout->addLayout(eraserWidthLayout);

    // 马赛克默认大小
    QHBoxLayout *mosaicSizeLayout = new QHBoxLayout();
    lblDefaultMosaicSize = new QLabel(tm->get("style.defaultMosaicSize", "Default Mosaic Size:"), m_annotationDefaultsGroup);
    spnDefaultMosaicSize = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultMosaicSize->setRange(1, 20);
    spnDefaultMosaicSize->setSuffix(" px");
    spnDefaultMosaicSize->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
    mosaicSizeLayout->addWidget(lblDefaultMosaicSize);
    mosaicSizeLayout->addStretch();
    mosaicSizeLayout->addWidget(spnDefaultMosaicSize);
    annotationDefaultsLayout->addLayout(mosaicSizeLayout);

    // 连接信号
    connect(spnDefaultPenWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultPenWidth", value);
        StyleManager::setDefaultPenWidth(value);
        emit defaultPenWidthChanged(value);
    });
    connect(spnDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultFontSize", value);
        StyleManager::setDefaultFontSize(value);
        emit defaultFontSizeChanged(value);
    });
    connect(spnDefaultEraserWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultEraserWidth", value);
        StyleManager::setDefaultEraserWidth(value);
        emit defaultEraserWidthChanged(value);
    });
    connect(spnDefaultMosaicSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultMosaicSize", value);
        StyleManager::setDefaultMosaicSize(value);
        emit defaultMosaicSizeChanged(value);
    });

    layout->addWidget(tabGroup);
    layout->addStretch();
    
    // 创建样式选项卡容器（包含滚动区域 + 按钮）
    styleTabContainer = new QWidget();
    styleTabContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *styleTabLayout = new QVBoxLayout(styleTabContainer);
    styleTabLayout->setContentsMargins(0, 0, 0, 10);
    styleTabLayout->setSpacing(0);
    
    // 创建一个容器来包含需要滚动的内容
    scrollContainer = new QWidget();
    scrollContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContainer);
    scrollLayout->setContentsMargins(10, 10, 10, 10);
    scrollLayout->setSpacing(10);
    scrollLayout->addWidget(tabGroup);
    scrollLayout->addWidget(m_annotationDefaultsGroup);
    scrollLayout->addWidget(borderGroup);
    scrollLayout->addWidget(toolbarGroup);
    scrollLayout->addStretch();
    
    // 创建滚动区域
    styleScrollArea = new QScrollArea();
    styleScrollArea->setWidget(scrollContainer);
    styleScrollArea->setWidgetResizable(true);
    styleScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    styleScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    styleScrollArea->setFrameShape(QFrame::NoFrame);
    // 不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供
    // （与历史记录选项卡保持一致，避免 setStyleSheet 影响子控件原生渲染）
    
    // 重置按钮 - 放在滚动区域下面，固定显示
    btnResetStyle = new QPushButton(TranslationManager::instance()->get("style.resetStyle", "Reset to Default"), styleTabContainer);
    btnResetStyle->setObjectName("primaryButton");
    btnResetStyle->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnResetStyle, &QPushButton::clicked, this, &SettingsWindow::onResetStyle);
    
    // 按钮居中
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(btnResetStyle);
    buttonLayout->addStretch();
    
    // 添加到容器布局
    styleTabLayout->addWidget(styleScrollArea);
    styleTabLayout->addLayout(buttonLayout);
    
    tabWidget->addTab(styleTabContainer, TranslationManager::instance()->get("tabStyle", "Style"));
}

/**
 * @brief 通用颜色变更处理
 *
 * 统一替代原先 17 个 onXxxColorChanged 槽函数的重复逻辑：
 * 1. 弹出颜色对话框
 * 2. 若用户选择了新颜色：更新 StyleManager、刷新按钮显示、保存设置
 * 3. 执行分类对应的样式联动
 * 4. 发射信号（若有：仅 tabWidgetBgColorChanged 有订阅者）
 * @param id 颜色 ID（StyleManager::StyleColorId，兼作按钮数组索引）
 * @author chiangyang
 */
void SettingsWindow::applyColorChange(StyleManager::StyleColorId id) {
    const auto& s = StyleManager::colorSettingTable()[static_cast<size_t>(id)];

    QColor currentColor = s.getter();
    QColor newColor = showColorDialog(currentColor);
    if (newColor == currentColor) {
        return;
    }

    s.setter(newColor);
    QPushButton* btn = m_colorButtons[static_cast<size_t>(id)];
    if (btn) {
        updateColorButton(btn, newColor);
    }
    saveStyleSettings();

    applyColorPostUpdate(s.category, newColor);
    // 发射信号（若有：仅 tabWidgetBgColorChanged 有订阅者，通知 HistoryWindow）
    if (s.signalId == StyleManager::StyleColorSignal::TabWidgetBg) {
        emit tabWidgetBgColorChanged();
    }
}

/**
 * @brief 颜色变更后的样式联动
 *
 * 不同分类的颜色变更会触发不同的样式刷新：
 * - Border/Toolbar：无后处理
 * - TabButton：仅刷新 tabWidget 样式表
 * - TabWidgetBg：刷新 tabWidget + 容器 + GroupBox + 三个 tab 的背景 + 历史 tab Palette
 *
 * @param category 颜色分类（StyleManager::StyleColorCategory）
 * @param newColor 新颜色（仅 TabWidgetBg 类用到）
 * @author chiangyang
 */
void SettingsWindow::applyColorPostUpdate(StyleManager::StyleColorCategory category, const QColor& newColor) {
    switch (category) {
        case StyleManager::StyleColorCategory::Border:
        case StyleManager::StyleColorCategory::Toolbar:
            break;
        case StyleManager::StyleColorCategory::TabButton:
            tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
            break;
        case StyleManager::StyleColorCategory::TabWidgetBg:
            tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
            if (styleTabContainer) {
                styleTabContainer->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
            }
            if (scrollContainer) {
                scrollContainer->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
            }
            updateAllGroupBoxStyles();
            updateAllTabsPalette(newColor);
            updateHistoryTabPalette(newColor);
            break;
    }
}

/**
 * @brief 显示颜色选择器
 * @param currentColor 当前颜色
 * @return 选择的颜色
 * @author chiangyang
 */
QColor SettingsWindow::showColorDialog(const QColor &currentColor) {
    QColorDialog colorDialog(currentColor, this);
    colorDialog.setWindowTitle(TranslationManager::instance()->get("style.selectColor", "Select Color"));
    if (colorDialog.exec() == QColorDialog::Accepted) {
        return colorDialog.currentColor();
    }
    return currentColor;
}

/**
 * @brief 创建一行颜色选择控件（标签 + 弹性间距 + 色块按钮）
 *
 * 统一封装样式选项卡中重复的颜色行创建逻辑。色块按钮使用 #settingColorButton
 * 对象名，尺寸/边框/圆角由全局 qss 管理，此处只设置 background-color。
 * @param parent 父控件（通常为所属 QGroupBox）
 * @param labelText 标签文本（已翻译）
 * @param color 初始颜色
 * @param outBtn 输出：创建的色块按钮指针（供调用方保存为成员变量）
 * @param outLabel 输出：创建的标签指针（供 retranslateUi 更新文本）
 * @return 包含标签和按钮的水平布局（已含 addStretch）
 * @author chiangyang
 */
QHBoxLayout *SettingsWindow::createColorRow(QWidget *parent, const QString &labelText,
                                             const QColor &color, QPushButton *&outBtn, QLabel *&outLabel) {
    QLabel *label = new QLabel(labelText, parent);
    QPushButton *btn = new QPushButton(parent);
    btn->setObjectName("settingColorButton");
    updateColorButton(btn, color);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(label);
    layout->addStretch();
    layout->addWidget(btn);

    outBtn = btn;
    outLabel = label;
    return layout;
}

/**
 * @brief 更新颜色按钮的显示颜色
 *
 * 仅设置 background-color，尺寸/边框/圆角仍由全局 qss 的 #settingColorButton 管理，
 * 保持 DPI 自适应缩放。
 * @param btn 颜色按钮
 * @param color 新颜色
 * @author chiangyang
 */
void SettingsWindow::updateColorButton(QPushButton *btn, const QColor &color) {
    if (btn) {
        btn->setStyleSheet(QString("background-color: %1;").arg(color.name()));
    }
}

/**
 * @brief 重置样式槽函数
 * @author chiangyang
 */
void SettingsWindow::onResetStyle() {
    if (MessageBox::question(this,
            TranslationManager::instance()->get("style.resetStyle", "Reset to Default"),
            TranslationManager::instance()->get("style.confirmReset", "Are you sure you want to reset all styles to default values?"))) {
        // 重置为默认值
        StyleManager::resetToDefaults();

        // 通过数据驱动表循环刷新所有颜色按钮（表归 StyleManager 所有）
        const auto& table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            if (m_colorButtons[i]) {
                updateColorButton(m_colorButtons[i], table[i].getter());
            }
        }

        // 重置标注工具默认值的 SpinBox
        if (spnDefaultPenWidth) {
            spnDefaultPenWidth->blockSignals(true);
            spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
            spnDefaultPenWidth->blockSignals(false);
        }
        if (spnDefaultFontSize) {
            spnDefaultFontSize->blockSignals(true);
            spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
            spnDefaultFontSize->blockSignals(false);
        }

        // 重置橡皮擦和马赛克默认值的 SpinBox
        if (spnDefaultEraserWidth) {
            spnDefaultEraserWidth->blockSignals(true);
            spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
            spnDefaultEraserWidth->blockSignals(false);
        }
        if (spnDefaultMosaicSize) {
            spnDefaultMosaicSize->blockSignals(true);
            spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
            spnDefaultMosaicSize->blockSignals(false);
        }

        // 选项卡背景相关的样式联动
        QColor defaultTabWidgetBgColor = StyleManager::getTabWidgetBgColor();
        applyColorPostUpdate(StyleManager::StyleColorCategory::TabWidgetBg, defaultTabWidgetBgColor);

        // 保存默认值到QSettings
        saveStyleSettings();

        // 发送颜色变更信号（仅选项卡背景有订阅者：HistoryWindow）
        emit tabWidgetBgColorChanged();

        // 发送标注工具默认值变更信号（通知工具栏更新滑块）
        emit defaultPenWidthChanged(StyleManager::getDefaultPenWidth());
        emit defaultFontSizeChanged(StyleManager::getDefaultFontSize());
        emit defaultEraserWidthChanged(StyleManager::getDefaultEraserWidth());
        emit defaultMosaicSizeChanged(StyleManager::getDefaultMosaicSize());

        // 重新应用样式到所有UI组件
        retranslateUi();
    }
}

/**
 * @brief 工具栏画笔粗细变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节画笔粗细时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param width 新的画笔粗细值
 * @author chiangyang
 */
void SettingsWindow::onToolPenWidthChanged(int width) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultPenWidth) {
        spnDefaultPenWidth->blockSignals(true);
        spnDefaultPenWidth->setValue(width);
        spnDefaultPenWidth->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultPenWidth", width);
    StyleManager::setDefaultPenWidth(width);
    LOG_INFO(QString("SettingsWindow: Tool pen width changed to %1 (persisted)").arg(width));
}

/**
 * @brief 工具栏字号变更槽函数
 *
 * 当用户在工具栏滑块调节字号时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param size 新的字号值
 * @author chiangyang
 */
void SettingsWindow::onToolFontSizeChanged(int size) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultFontSize) {
        spnDefaultFontSize->blockSignals(true);
        spnDefaultFontSize->setValue(size);
        spnDefaultFontSize->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultFontSize", size);
    StyleManager::setDefaultFontSize(size);
    LOG_INFO(QString("SettingsWindow: Tool font size changed to %1 (persisted)").arg(size));
}

/**
 * @brief 工具栏橡皮擦粗细变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节橡皮擦粗细时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param width 新的橡皮擦粗细值
 * @author chiangyang
 */
void SettingsWindow::onToolEraserWidthChanged(int width) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultEraserWidth) {
        spnDefaultEraserWidth->blockSignals(true);
        spnDefaultEraserWidth->setValue(width);
        spnDefaultEraserWidth->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultEraserWidth", width);
    StyleManager::setDefaultEraserWidth(width);
    LOG_INFO(QString("SettingsWindow: Tool eraser width changed to %1 (persisted)").arg(width));
}

/**
 * @brief 工具栏马赛克大小变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节马赛克大小时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param size 新的马赛克大小值
 * @author chiangyang
 */
void SettingsWindow::onToolMosaicSizeChanged(int size) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultMosaicSize) {
        spnDefaultMosaicSize->blockSignals(true);
        spnDefaultMosaicSize->setValue(size);
        spnDefaultMosaicSize->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultMosaicSize", size);
    StyleManager::setDefaultMosaicSize(size);
    LOG_INFO(QString("SettingsWindow: Tool mosaic size changed to %1 (persisted)").arg(size));
}

/**
 * @brief 工具栏按钮样式改变槽函数
 * @param index 下拉框索引
 * @author chiangyang
 */
void SettingsWindow::onToolbarButtonStyleChanged(int index) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    QString style = cmbToolbarButtonStyle->itemData(index).toString();
    settings->setValue("style/toolbarButtonStyle", style);
    settings->sync();

    // 同步更新 StyleManager，确保后续创建的工具栏使用最新样式
    StyleManager::setToolbarButtonStyle(style);

    // 发送信号通知已有的工具栏和控制栏更新样式
    emit toolbarButtonStyleChanged(style);
}

/**
 * @brief 保存样式设置
 * @author chiangyang
 */
void SettingsWindow::saveStyleSettings() {
    QSettings *settings = ConfigManager::instance()->getSettings();

    // 通过数据驱动表循环保存所有颜色（表归 StyleManager 所有）
    const auto& table = StyleManager::colorSettingTable();
    for (const auto& s : table) {
        settings->setValue(QString("style/%1").arg(s.settingsKey), s.getter().name());
    }
    settings->setValue("style/toolbarButtonStyle", cmbToolbarButtonStyle->currentData().toString());

    // 保存标注工具默认值
    settings->setValue("style/defaultPenWidth", qBound(1, StyleManager::getDefaultPenWidth(), 20));
    settings->setValue("style/defaultFontSize", qBound(8, StyleManager::getDefaultFontSize(), 48));
    // 保存橡皮擦和马赛克默认值
    settings->setValue("style/defaultEraserWidth", qBound(1, StyleManager::getDefaultEraserWidth(), 20));
    settings->setValue("style/defaultMosaicSize", qBound(1, StyleManager::getDefaultMosaicSize(), 20));

    // 同步保存
    settings->sync();
}

/**
 * @brief 加载样式设置
 *
 * StyleManager 已在启动时由 initFromConfig 从持久化恢复全部样式值，
 * 此处仅把 StyleManager 已初始化的值同步到设置页 UI，
 * 不再直接读 QSettings（避免设置页持有第二套读取逻辑）。
 * @author chiangyang
 */
void SettingsWindow::loadStyleSettings() {
    // 通过数据驱动表循环把当前颜色同步到色块按钮
    const auto& table = StyleManager::colorSettingTable();
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& s = table[i];
        if (m_colorButtons[i]) {
            updateColorButton(m_colorButtons[i], s.getter());
        }
    }

    // 选项卡背景相关的样式联动
    QColor tabWidgetBgColor = StyleManager::getTabWidgetBgColor();
    applyColorPostUpdate(StyleManager::StyleColorCategory::TabWidgetBg, tabWidgetBgColor);

    // 工具栏按钮样式（已由 initFromConfig 恢复）
    QString toolbarButtonStyle = StyleManager::getToolbarButtonStyle();
    int styleIndex = cmbToolbarButtonStyle->findData(toolbarButtonStyle);
    if (styleIndex >= 0) {
        cmbToolbarButtonStyle->setCurrentIndex(styleIndex);
    }

    // 标注工具默认值（已由 initFromConfig 恢复，同步到 SpinBox）
    if (spnDefaultPenWidth) {
        spnDefaultPenWidth->blockSignals(true);
        spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
        spnDefaultPenWidth->blockSignals(false);
    }
    if (spnDefaultFontSize) {
        spnDefaultFontSize->blockSignals(true);
        spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
        spnDefaultFontSize->blockSignals(false);
    }
    if (spnDefaultEraserWidth) {
        spnDefaultEraserWidth->blockSignals(true);
        spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
        spnDefaultEraserWidth->blockSignals(false);
    }
    if (spnDefaultMosaicSize) {
        spnDefaultMosaicSize->blockSignals(true);
        spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
        spnDefaultMosaicSize->blockSignals(false);
    }
}

/**
 * @brief 选项卡切换槽函数，用于自适应调整窗口高度
 * @param index 选项卡索引
 * @author chiangyang
 */
void SettingsWindow::onTabChanged(int index) {
    LOG_INFO(QString("SettingsWindow: tab changed to index=%1").arg(index));
    QWidget *currentTab = tabWidget->widget(index);

    // 切换到快捷键选项卡时，清除 QKeySequenceEdit 的焦点，避免自动进入录制状态（蓝色下划线）
    // 用 QTimer::singleShot(0) 延迟到 Qt 完成选项卡焦点转移之后执行，
    // 否则 clearFocus() 会被 Qt 后续的焦点逻辑覆盖
    // 快捷键选项卡索引为 1，其中所有 QKeySequenceEdit 由 addShortcutRow 数据驱动创建
    if (currentTab && index == 1) {
        QTimer::singleShot(0, this, [this]() {
            const auto edits = findChildren<QKeySequenceEdit*>();
            for (auto *edit : edits) {
                edit->clearFocus();
            }
            this->setFocus();
        });
    }

    fitWindowHeight();
}

/**
 * @brief 按当前选项卡内容重新自适应窗口高度
 *
 * 先让当前页缩放回其 sizeHint 自然高度（adjustSize()），
 * 再读取内容真实所需高度，把窗口高度动画到对应值。
 * @author chiangyang
 */
void SettingsWindow::fitWindowHeight() {
    QWidget *currentTab = tabWidget->currentWidget();
    if (!currentTab) return;

    // adjustSize() 会把页面缩到其 sizeHint 的自然高度，
    // 然后 size().height() 就是内容真实需要的高度
    if (currentTab->layout()) {
        currentTab->layout()->update();
        currentTab->adjustSize();
    }

    int tabHeight = currentTab->size().height();
    // tab 栏高度补偿：固定像素值，用于 tab 栏自身高度之外的多余空间
    // （tab 栏 padding 已由全局 qss 用 em 单位随字体/DPI 缩放）
    int tabBarExtra = 50;
    int targetHeight = tabHeight + tabBarExtra;

    if (heightAnimation && heightAnimation->state() == QPropertyAnimation::Running) {
        heightAnimation->stop();
    }

    if (!heightAnimation) {
        heightAnimation = new QPropertyAnimation(this, "animatedHeight", this);
        heightAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    }

    int currentHeight = this->height();
    heightAnimation->setStartValue(currentHeight);
    heightAnimation->setEndValue(targetHeight);
    heightAnimation->setDuration(400);
    heightAnimation->start();
}

/**
 * @brief 设置固定高度（带动画效果）
 * @param height 目标高度
 * @author chiangyang
 */
void SettingsWindow::setFixedHeightWithAnimation(int height) {
    // setFixedSize 覆盖布局最小高度约束，允许窗口缩到内容高度以下
    this->setFixedSize(m_dpiScaledWidth, height);
    
    // 停止之前的动画
    if (heightAnimation && heightAnimation->state() == QPropertyAnimation::Running) {
        heightAnimation->stop();
    }
    
    // 创建动画
    if (!heightAnimation) {
        heightAnimation = new QPropertyAnimation(this, "animatedHeight", this);
        heightAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    }
    
    // 设置动画参数
    int currentHeight = this->height();
    
    heightAnimation->setStartValue(currentHeight);
    heightAnimation->setEndValue(height);
    heightAnimation->setDuration(300);
    
    // 启动动画
    heightAnimation->start();
}

/**
 * @brief 设置动画高度
 * @param height 新的高度值
 * @author chiangyang
 */
void SettingsWindow::setAnimatedHeight(int height) {
    m_animatedHeight = height;
    // setFixedSize 覆盖布局最小高度约束，允许窗口缩到内容高度以下
    this->setFixedSize(m_dpiScaledWidth, height);
}

/**
 * @brief 设置历史记录选项卡
 * @author chiangyang
 */
void SettingsWindow::setupHistoryTab() {
    TranslationManager *tm = TranslationManager::instance();

    // 创建滚动内容容器
    // 注意：用 QPalette 设置背景色，不要用 setStyleSheet。
    // QScrollArea 的 viewport 不会自动继承 QTabWidget::pane 的背景色，
    // 需要显式设置。若用 setStyleSheet 会禁用子控件（如 QCheckBox）的
    // 原生主题绘制，导致复选框指示器空白、对勾不显示。
    QWidget *scrollContent = new QWidget();
    m_historyScrollContent = scrollContent;
    scrollContent->setAutoFillBackground(true);
    QPalette scrollPal = scrollContent->palette();
    scrollPal.setColor(QPalette::Window, StyleManager::getTabWidgetBgColor());
    scrollContent->setPalette(scrollPal);
    QVBoxLayout *layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    // ===== 记录开关组 =====
    historyRecordGroup = new QGroupBox(tm->get("history.recordSettings", "Record Settings"), scrollContent);
    historyRecordGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *recordLayout = new QVBoxLayout(historyRecordGroup);

    m_screenshotHistoryCheck = new QCheckBox(tm->get("history.enableScreenshot", "Record Screenshot History"), historyRecordGroup);
    m_screenshotHistoryCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_clipboardHistoryCheck = new QCheckBox(tm->get("history.enableClipboard", "Record Clipboard History"), historyRecordGroup);
    m_clipboardHistoryCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());

    // 从 HistoryManager 加载当前设置
    m_screenshotHistoryCheck->setChecked(HistoryManager::instance()->isScreenshotEnabled());
    m_clipboardHistoryCheck->setChecked(HistoryManager::instance()->isClipboardEnabled());

    recordLayout->addWidget(m_screenshotHistoryCheck);
    recordLayout->addWidget(m_clipboardHistoryCheck);

    connect(m_screenshotHistoryCheck, &QCheckBox::toggled, this, [](bool checked) {
        HistoryManager::instance()->setScreenshotEnabled(checked);
    });
    connect(m_clipboardHistoryCheck, &QCheckBox::toggled, this, [](bool checked) {
        HistoryManager::instance()->setClipboardEnabled(checked);
    });

    layout->addWidget(historyRecordGroup);

    // ===== 存储设置组 =====
    historyStorageGroup = new QGroupBox(tm->get("history.storage.title", "Storage Settings"), scrollContent);
    historyStorageGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *storageLayout = new QGridLayout(historyStorageGroup);

    lblRetentionDays = new QLabel(tm->get("history.storage.retentionDays", "Retention Period") + ":", historyStorageGroup);
    cmbRetentionDays = new QComboBox(historyStorageGroup);
    cmbRetentionDays->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbRetentionDays->addItem(tm->get("history.storage.days7", "7 days"), 7);
    cmbRetentionDays->addItem(tm->get("history.storage.days30", "30 days"), 30);
    cmbRetentionDays->addItem(tm->get("history.storage.days90", "90 days"), 90);
    cmbRetentionDays->addItem(tm->get("history.storage.days180", "180 days"), 180);
    cmbRetentionDays->addItem(tm->get("history.storage.days365", "365 days"), 365);

    int retentionDays = HistoryManager::instance()->retentionDays();
    int retIdx = cmbRetentionDays->findData(retentionDays);
    if (retIdx >= 0) {
        cmbRetentionDays->setCurrentIndex(retIdx);
    }

    lblMaxItems = new QLabel(tm->get("history.storage.maxItems", "Max Records") + ":", historyStorageGroup);
    cmbMaxItems = new QComboBox(historyStorageGroup);
    cmbMaxItems->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbMaxItems->addItem("500", 500);
    cmbMaxItems->addItem("1000", 1000);
    cmbMaxItems->addItem("2000", 2000);
    cmbMaxItems->addItem("5000", 5000);
    cmbMaxItems->addItem("10000", 10000);

    int maxItems = HistoryManager::instance()->maxItems();
    int maxIdx = cmbMaxItems->findData(maxItems);
    if (maxIdx >= 0) {
        cmbMaxItems->setCurrentIndex(maxIdx);
    }

    storageLayout->addWidget(lblRetentionDays, 0, 0);
    storageLayout->addWidget(cmbRetentionDays, 0, 1);
    storageLayout->addWidget(lblMaxItems, 1, 0);
    storageLayout->addWidget(cmbMaxItems, 1, 1);

    // 设置下拉框宽度自适应内容（sizeAdjustPolicy 仅构造时设置一次）
    cmbRetentionDays->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    cmbMaxItems->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // 取两个下拉框的最大内容宽度作为统一宽度（抽取到 updateHistoryComboWidth，
    // 便于语言切换与 DPI 变化时重新计算）
    updateHistoryComboWidth();
    // 第0列（标签）拉伸，第1列（下拉框）不拉伸
    storageLayout->setColumnStretch(0, 1);
    storageLayout->setColumnStretch(1, 0);

    connect(cmbRetentionDays, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int days = cmbRetentionDays->itemData(index).toInt();
        HistoryManager::instance()->setRetentionDays(days);
    });
    connect(cmbMaxItems, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int count = cmbMaxItems->itemData(index).toInt();
        HistoryManager::instance()->setMaxItems(count);
    });

    layout->addWidget(historyStorageGroup);

    // ===== 数据管理组 =====
    historyDataGroup = new QGroupBox(tm->get("history.dataManagement.title", "Data Management"), scrollContent);
    historyDataGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QHBoxLayout *dataLayout = new QHBoxLayout(historyDataGroup);

    btnCleanHistory = new QPushButton(tm->get("history.dataManagement.cleanExpired", "Clean Expired Records"), historyDataGroup);
    btnClearHistory = new QPushButton(tm->get("history.dataManagement.clearAll", "Clear All History"), historyDataGroup);

    // 主操作按钮样式：高度更大、视觉更突出（尺寸由全局 qss #primaryButton 管理）
    btnCleanHistory->setObjectName("primaryButton");
    btnClearHistory->setObjectName("primaryButton");
    btnCleanHistory->setStyleSheet(StyleManager::getSettingsButtonStyle());
    btnClearHistory->setStyleSheet(StyleManager::getSettingsButtonStyle());

    connect(btnCleanHistory, &QPushButton::clicked, this, []() {
        HistoryManager::instance()->cleanupExpired();
    });
    connect(btnClearHistory, &QPushButton::clicked, this, [this]() {
        TranslationManager *tm = TranslationManager::instance();
        if (MessageBox::question(this,
                tm->get("history.dataManagement.confirmClearTitle", "Confirm Clear"),
                tm->get("history.dataManagement.confirmClearMsg",
                        "Are you sure you want to clear all history? This action cannot be undone."))) {
            HistoryManager::instance()->clearAll();
            updateHistoryStats();
        }
    });

    dataLayout->addWidget(btnCleanHistory);
    dataLayout->addWidget(btnClearHistory);

    layout->addWidget(historyDataGroup);

    // ===== 统计信息 =====
    historyStatsGroup = new QGroupBox(tm->get("history.stats.title", "Statistics"), scrollContent);
    historyStatsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *statsLayout = new QVBoxLayout(historyStatsGroup);

    lblHistoryStats = new QLabel(historyStatsGroup);
    statsLayout->addWidget(lblHistoryStats);

    layout->addWidget(historyStatsGroup);
    layout->addStretch();

    // 创建滚动区域
    // 注意：不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供。
    // 若用 setStyleSheet 会禁用子控件（如 QCheckBox）的原生主题绘制，
    // 导致复选框指示器空白、对勾不显示。
    historyScrollArea = new QScrollArea();
    historyScrollArea->setWidget(scrollContent);
    historyScrollArea->setWidgetResizable(true);
    historyScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    historyScrollArea->setFrameShape(QFrame::NoFrame);
    // 用 QPalette 设置 viewport 背景色，避免默认背景色与选项卡背景不一致
    QPalette viewportPal = historyScrollArea->palette();
    viewportPal.setColor(QPalette::Window, StyleManager::getTabWidgetBgColor());
    historyScrollArea->setPalette(viewportPal);
    historyScrollArea->setAutoFillBackground(true);

    // 创建历史记录选项卡容器
    // 用 #objectName 选择器设背景色，只匹配 tab 根容器自身，不影响子控件原生渲染
    m_historyTab = new QWidget();
    m_historyTab->setObjectName("historyTabBg");
    m_historyTab->setStyleSheet(QString("#historyTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *historyTabLayout = new QVBoxLayout(m_historyTab);
    historyTabLayout->setContentsMargins(0, 0, 0, 0);
    historyTabLayout->setSpacing(0);
    historyTabLayout->addWidget(historyScrollArea);

    tabWidget->addTab(m_historyTab, tm->get("history.tabName", "History"));

    updateHistoryStats();
}

/**
 * @brief 刷新所有 QGroupBox 的背景样式（绑定到 StyleManager 的 s_tabWidgetBgColor）
 *
 * 在 applyColorChange() 改色和 onResetStyle() 重置时统一调用，
 * 覆盖通用/翻译/样式/历史记录 4 个选项卡的全部 QGroupBox，
 * 避免每个 GroupBox 在每个改色路径下都重复写 if 判断。
 * @author chiangyang
 */
void SettingsWindow::updateAllGroupBoxStyles() {
    const QString style = StyleManager::getGroupBoxStyle();

    // General 通用选项卡
    if (saveGroup) saveGroup->setStyleSheet(style);
    if (configGroup) configGroup->setStyleSheet(style);

    // Translate 翻译选项卡
    if (m_translateEngineGroup) m_translateEngineGroup->setStyleSheet(style);
    if (m_translateOptionsGroup) m_translateOptionsGroup->setStyleSheet(style);

    // Style 样式选项卡
    if (tabGroup) tabGroup->setStyleSheet(style);
    if (borderGroup) borderGroup->setStyleSheet(style);
    if (toolbarGroup) toolbarGroup->setStyleSheet(style);
    if (m_annotationDefaultsGroup) m_annotationDefaultsGroup->setStyleSheet(style);

    // Shortcuts 快捷键选项卡
    if (m_shortcutGroupGlobal) m_shortcutGroupGlobal->setStyleSheet(style);
    if (m_shortcutGroupTools) m_shortcutGroupTools->setStyleSheet(style);
    if (m_shortcutGroupAnnotation) m_shortcutGroupAnnotation->setStyleSheet(style);
    if (m_shortcutGroupPinWindow) m_shortcutGroupPinWindow->setStyleSheet(style);

    // History 历史记录选项卡
    if (historyRecordGroup) historyRecordGroup->setStyleSheet(style);
    if (historyStorageGroup) historyStorageGroup->setStyleSheet(style);
    if (historyDataGroup) historyDataGroup->setStyleSheet(style);
    if (historyStatsGroup) historyStatsGroup->setStyleSheet(style);

    // About 关于选项卡
    if (m_updateGroup) m_updateGroup->setStyleSheet(style);
}

/**
 * @brief 刷新历史记录选项卡滚动区的 QPalette 背景色
 * @param color 新的背景色（通常是 StyleManager::getTabWidgetBgColor()）
 *
 * 历史记录选项卡用 QPalette 设背景（避免 setStyleSheet 禁用子控件原生渲染），
 * 每次改颜色时需重新设置 m_historyScrollContent 和 historyScrollArea->viewport() 的调色板。
 * @author chiangyang
 */
void SettingsWindow::updateHistoryTabPalette(const QColor &color) {
    if (m_historyScrollContent) {
        QPalette pal = m_historyScrollContent->palette();
        pal.setColor(QPalette::Window, color);
        m_historyScrollContent->setPalette(pal);
    }
    if (historyScrollArea) {
        QPalette vpPal = historyScrollArea->palette();
        vpPal.setColor(QPalette::Window, color);
        historyScrollArea->setPalette(vpPal);
    }
}

/**
 * @brief 刷新常规/翻译/历史记录三个选项卡根容器的背景色
 * @param color 新的背景色（通常是 StyleManager::getTabWidgetBgColor()）
 *
 * 这三个选项卡的根 QWidget 用 #objectName 选择器设 setStyleSheet 背景色，
 * 只匹配自身不影响子控件原生渲染（QCheckBox 等保持原生主题）。
 * QTabWidget::pane 的 QSS 背景色在 Windows 上不一定透过到 tab 页面，
 * 因此需显式设背景色。
 * @author chiangyang
 */
void SettingsWindow::updateAllTabsPalette(const QColor &color) {
    QString style = QString("background-color: %1;").arg(color.name());
    if (m_generalTab) {
        m_generalTab->setStyleSheet(QString("#generalTabBg { %1 }").arg(style));
    }
    if (m_translateTab) {
        m_translateTab->setStyleSheet(QString("#translateTabBg { %1 }").arg(style));
    }
    if (m_historyTab) {
        m_historyTab->setStyleSheet(QString("#historyTabBg { %1 }").arg(style));
    }
    // 快捷键选项卡：外层容器 + 滚动内容容器同步更新
    if (m_shortcutsScrollArea) {
        // m_shortcutsScrollArea 的父窗口即为 shortcutsTab（通过 addWidget 挂载）
        QWidget *shortcutsTab = m_shortcutsScrollArea->parentWidget();
        if (shortcutsTab && shortcutsTab->objectName() == "shortcutsTabBg") {
            shortcutsTab->setStyleSheet(QString("#shortcutsTabBg { %1 }").arg(style));
        }
        if (m_shortcutsScrollArea->widget()) {
            m_shortcutsScrollArea->widget()->setStyleSheet(style);
        }
    }
}

/**
 * @brief 更新历史统计信息
 * @author chiangyang
 */
void SettingsWindow::updateHistoryStats() {
    if (!lblHistoryStats) return;

    HistoryManager *manager = HistoryManager::instance();
    TranslationManager *tm = TranslationManager::instance();

    int screenshotCount = manager->getItemCount(HistoryType::Screenshot);
    int textCount = manager->getItemCount(HistoryType::ClipboardText);
    qint64 storageSize = manager->getStorageSize();

    QString sizeStr;
    if (storageSize < 1024) {
        sizeStr = QString("%1 B").arg(storageSize);
    } else if (storageSize < 1024 * 1024) {
        sizeStr = QString("%1 KB").arg(storageSize / 1024.0, 0, 'f', 1);
    } else {
        sizeStr = QString("%1 MB").arg(storageSize / (1024.0 * 1024.0), 0, 'f', 1);
    }

    lblHistoryStats->setText(QString("%1: %2 %3\n%4: %5 %3\n%6: %7")
                                 .arg(tm->get("history.stats.screenshots", "Screenshots"))
                                 .arg(screenshotCount)
                                 .arg(tm->get("history.stats.items", "items"))
                                 .arg(tm->get("history.stats.texts", "Texts"))
                                 .arg(textCount)
                                 .arg(tm->get("history.stats.storage", "Storage Used"))
                                 .arg(sizeStr));
}

bool SettingsWindow::eventFilter(QObject *obj, QEvent *event) {
    // 处理拖动相关事件
    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent*>(event);

        // 如果正在拖动，继续处理
        if (m_isDragging && (mouseEvent->buttons() & Qt::LeftButton)) {
            QPoint delta = mouseEvent->globalPosition().toPoint() - m_dragStartPos;
            move(m_widgetStartPos + delta);
            return true; // 拦截事件
        }
    } else if (event->type() == QEvent::Leave && !m_isDragging) {
        setCursor(Qt::ArrowCursor);
    } else if (event->type() == QEvent::MouseButtonRelease) {
        m_isDragging = false;
    }

    if (event->type() == QEvent::Wheel) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        QWidget *focusWidget = this->focusWidget();
        
        if (focusWidget && (focusWidget->parent() == tabWidget || qobject_cast<QComboBox *>(focusWidget) || 
            qobject_cast<QLineEdit *>(focusWidget) || qobject_cast<QKeySequenceEdit *>(focusWidget))) {
            return QWidget::eventFilter(obj, event);
        }
        
        // 在快捷键选项卡（index == 1）、样式选项卡（index == 2）和历史记录选项卡（index == 4）中处理滚轮滚动
        int currentIndex = tabWidget->currentIndex();
        QScrollArea *scrollArea = nullptr;
        if (currentIndex == 1 && m_shortcutsScrollArea) {
            scrollArea = m_shortcutsScrollArea;
        } else if (currentIndex == 2 && styleScrollArea) {
            scrollArea = styleScrollArea;
        } else if (currentIndex == 4 && historyScrollArea) {
            scrollArea = historyScrollArea;
        }

        if (scrollArea) {
            QScrollBar *vScrollBar = scrollArea->verticalScrollBar();
            if (vScrollBar) {
                int delta = wheelEvent->angleDelta().y();
                int scrollAmount = qAbs(delta) / 8;
                scrollAmount = qMax(scrollAmount, 3);
                scrollAmount = qMin(scrollAmount, 50);
                
                if (delta > 0) {
                    vScrollBar->setValue(vScrollBar->value() - scrollAmount);
                } else if (delta < 0) {
                    vScrollBar->setValue(vScrollBar->value() + scrollAmount);
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

/**
 * @brief 为所有子控件安装事件过滤器
 * @author chiangyang
 */
void SettingsWindow::installEventFilterOnChildren() {
    const auto children = findChildren<QWidget*>();
    for (auto *child : children) {
        child->installEventFilter(this);
        if (child->inherits("QScrollArea")) {
            // 为 QScrollArea 的 viewport 也安装
            if (auto *scrollArea = qobject_cast<QScrollArea*>(child)) {
                scrollArea->viewport()->installEventFilter(this);
            }
        }
    }
}

/**
 * @brief 鼠标按下事件，用于开始拖动窗口
 * @param event 鼠标事件
 * @author chiangyang
 */
void SettingsWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // 开始拖动窗口
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_widgetStartPos = frameGeometry().topLeft();
    }
}

/**
 * @brief 鼠标移动事件，用于拖动窗口
 * @param event 鼠标事件
 * @author chiangyang
 */
void SettingsWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_widgetStartPos + delta);
    }
}

/**
 * @brief 鼠标释放事件，结束拖动
 * @param event 鼠标事件
 * @author chiangyang
 */
void SettingsWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
}





/**
 * @brief 检查更新按钮点击
 * @author chiangyang
 */
void SettingsWindow::onCheckForUpdate() {
    if (!m_updateManager) return;
    
    btnCheckUpdate->setEnabled(false);
    setUpdateStatusText("update.checking", "Checking for updates...");
    lblUpdateChannel->clear();
    lblUpdateChannel->hide();
    fitWindowHeight();

    m_updateManager->checkForUpdate(qApp->applicationVersion());
}

/**
 * @brief 下载更新按钮点击
 * @author chiangyang
 */
void SettingsWindow::onDownloadUpdate() {
    if (!m_updateManager) return;

#ifndef Q_OS_WIN
    // macOS：dmg 分发 + ad-hoc 签名，无法自动替换 .app
    // 打开 release 下载页让用户手动下载 dmg，并退出当前程序避免覆盖正在运行的 .app
    QDesktopServices::openUrl(QUrl("https://gitee.com/chiangyangNPU/quick-shot/releases"));
    QCoreApplication::quit();
#else
    btnDownloadUpdate->hide();
    btnCheckUpdate->hide();
    m_updateProgressBar->show();
    m_updateProgressBar->setValue(0);
    btnCancelUpdate->show();
    setUpdateStatusText("update.downloading", "Downloading update...");

    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/QuickShot-update";
    m_updateManager->downloadUpdate(m_updateManager->latestVersion(), downloadDir);
    fitWindowHeight();
#endif
}

/**
 * @brief 安装更新按钮点击
 * @author chiangyang
 */
void SettingsWindow::onInstallUpdate() {
    if (!m_updateManager) return;
    
    QString installDir = QCoreApplication::applicationDirPath();
    QString zipPath = btnInstallUpdate->property("filePath").toString();
    
    if (zipPath.isEmpty()) {
        setUpdateStatusText("update.installError", "Unable to get update file path");
        fitWindowHeight();
        return;
    }

    setUpdateStatusText("update.installing", "Installing update, the program will restart...");
    fitWindowHeight();

    m_updateManager->installUpdate(zipPath, installDir);
}

/**
 * @brief 取消更新按钮点击
 * @author chiangyang
 */
void SettingsWindow::onCancelUpdate() {
    if (!m_updateManager) return;
    
    m_updateManager->cancel();
    resetUpdateUI();
}

/**
 * @brief 更新检查完成
 * @author chiangyang
 */
void SettingsWindow::onUpdateCheckFinished(bool hasUpdate,
                                           const UpdateManager::VersionInfo &info,
                                           const UpdateManager::ErrorInfo &error) {
    Q_UNUSED(error);
    
    if (hasUpdate) {
        setUpdateStatusText("update.found", "New version v%1 found\n%2", {info.version, info.releaseNotes});
        btnDownloadUpdate->show();
    } else {
        if (!info.version.isEmpty()) {
            setUpdateStatusText("update.upToDate", "Already up to date v%1", {info.version});
        } else {
            setUpdateStatusText("update.checkFailed", "Check update failed, please try again later");
        }
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
        btnDownloadUpdate->hide();
    }
    fitWindowHeight();
}

/**
 * @brief 更新下载进度
 * @author chiangyang
 */
void SettingsWindow::onUpdateDownloadProgress(qint64 received, qint64 total, int percent) {
    Q_UNUSED(received);
    Q_UNUSED(total);
    if (m_updateProgressBar) {
        m_updateProgressBar->setValue(percent);
    }
}

/**
 * @brief 更新下载完成
 * @author chiangyang
 */
void SettingsWindow::onUpdateDownloadFinished(bool success,
                                               const QString &filePath,
                                               const UpdateManager::ErrorInfo &error) {
    Q_UNUSED(error);
    
    if (success) {
        setUpdateStatusText("update.downloaded", "Download complete, click to install update");
        m_updateProgressBar->hide();
        btnCancelUpdate->hide();
        btnInstallUpdate->show();
        btnInstallUpdate->setProperty("filePath", filePath);
    } else {
        setUpdateStatusText("update.downloadFailed", "Download failed: %1", {error.message});
        m_updateProgressBar->hide();
        btnCancelUpdate->hide();
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    fitWindowHeight();
}

/**
 * @brief 更新安装完成
 * @param success 是否成功
 * @param message 结果描述（成功/失败原因）
 * @author chiangyang
 */
void SettingsWindow::onUpdateInstallFinished(bool success, const QString &message) {
    setUpdateStatusText(QString(), message);

    if (success) {
        // 安装成功后程序即将退出并自动重启，界面提示即可
        btnInstallUpdate->setEnabled(false);
    } else {
        // 安装失败：隐藏安装按钮，恢复“重新检查”入口
        btnInstallUpdate->hide();
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    fitWindowHeight();
}

/**
 * @brief 重置更新相关UI
 * @author chiangyang
 */
void SettingsWindow::resetUpdateUI() {
    if (btnCheckUpdate) {
        btnCheckUpdate->setEnabled(true);
        btnCheckUpdate->show();
    }
    if (btnDownloadUpdate) btnDownloadUpdate->hide();
    if (btnInstallUpdate) btnInstallUpdate->hide();
    if (btnCancelUpdate) btnCancelUpdate->hide();
    if (m_updateProgressBar) {
        m_updateProgressBar->hide();
        m_updateProgressBar->setValue(0);
    }
    if (lblUpdateStatus) {
        lblUpdateStatus->clear();
    }
    if (lblUpdateChannel) {
        lblUpdateChannel->clear();
        lblUpdateChannel->hide();
    }
    // 重置状态文字缓存，避免语言切换时渲染出陈旧文案
    m_updateStatusKey.clear();
    m_updateStatusFallback.clear();
    m_updateStatusArgs.clear();
    fitWindowHeight();
}

/**
 * @brief 设置更新状态文字并缓存翻译键
 * @author chiangyang
 */
void SettingsWindow::setUpdateStatusText(const QString &key, const QString &fallback, const QStringList &args) {
    m_updateStatusKey = key;
    m_updateStatusFallback = fallback;
    m_updateStatusArgs = args;
    renderUpdateStatusText();
}

/**
 * @brief 按缓存翻译键重新渲染更新状态文字
 * @author chiangyang
 */
void SettingsWindow::renderUpdateStatusText() {
    if (!lblUpdateStatus) return;
    // 无翻译键（如安装结果消息）时文字不可重建，原样保留 fallback
    QString text = m_updateStatusKey.isEmpty()
        ? m_updateStatusFallback
        : TranslationManager::instance()->get(m_updateStatusKey, m_updateStatusFallback);
    for (int i = 0; i < m_updateStatusArgs.size(); ++i) {
        text.replace("%" + QString::number(i + 1), m_updateStatusArgs.at(i));
    }
    lblUpdateStatus->setText(text);
}
