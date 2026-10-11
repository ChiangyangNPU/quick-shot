#include "SettingsHistoryTab.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../history/HistoryManager.h"
#include "../MessageBox.h"

#include <QGridLayout>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsHistoryTab::SettingsHistoryTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsHistoryTab::setupUi() {
    TranslationManager *tm = TranslationManager::instance();

    // 本控件即选项卡根容器：
    // 用 #objectName 选择器设背景色，只匹配自身，不影响子控件原生渲染
    setObjectName("historyTabBg");
    setStyleSheet(QString("#historyTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *tabLayout = new QVBoxLayout(this);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

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
    StyleManager::useHighlightOnlyPopup(cmbRetentionDays);
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
    StyleManager::useHighlightOnlyPopup(cmbMaxItems);
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
    updateComboWidths();
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

    tabLayout->addWidget(historyScrollArea);

    updateHistoryStats();
}

// ============================================================
// 语言切换
// ============================================================

void SettingsHistoryTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    if (historyRecordGroup) historyRecordGroup->setTitle(tm->get("history.recordSettings", "Record Settings"));
    if (m_screenshotHistoryCheck) m_screenshotHistoryCheck->setText(tm->get("history.enableScreenshot", "Record Screenshot History"));
    if (m_clipboardHistoryCheck) m_clipboardHistoryCheck->setText(tm->get("history.enableClipboard", "Record Clipboard History"));
    if (historyStorageGroup) historyStorageGroup->setTitle(tm->get("history.storage.title", "Storage Settings"));
    if (lblRetentionDays) lblRetentionDays->setText(tm->get("history.storage.retentionDays", "Retention Period") + ":");
    if (cmbRetentionDays) {
        // 保留当前选中项，重建条目以应用新语言文案（blockSignals 防触发保存）
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
    // 语言切换后重新计算下拉框宽度
    updateComboWidths();
    if (historyDataGroup) historyDataGroup->setTitle(tm->get("history.dataManagement.title", "Data Management"));
    if (btnCleanHistory) btnCleanHistory->setText(tm->get("history.dataManagement.cleanExpired", "Clean Expired Records"));
    if (btnClearHistory) btnClearHistory->setText(tm->get("history.dataManagement.clearAll", "Clear All History"));
    if (historyStatsGroup) historyStatsGroup->setTitle(tm->get("history.stats.title", "Statistics"));
    updateHistoryStats();
}

// ============================================================
// 样式与布局
// ============================================================

void SettingsHistoryTab::refreshStyles() {
    const QString style = StyleManager::getGroupBoxStyle();
    if (historyRecordGroup) historyRecordGroup->setStyleSheet(style);
    if (historyStorageGroup) historyStorageGroup->setStyleSheet(style);
    if (historyDataGroup) historyDataGroup->setStyleSheet(style);
    if (historyStatsGroup) historyStatsGroup->setStyleSheet(style);
}

void SettingsHistoryTab::applyTabBackground(const QColor &color) {
    // 根容器背景（#historyTabBg 选择器）+ 滚动区两处 QPalette 背景同步刷新
    setStyleSheet(QString("#historyTabBg { background-color: %1; }").arg(color.name()));
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

void SettingsHistoryTab::updateComboWidths() {
    if (!cmbRetentionDays || !cmbMaxItems) {
        return;
    }

    int w1 = cmbRetentionDays->sizeHint().width();
    int w2 = cmbMaxItems->sizeHint().width();
    int maxW = qMax(w1, w2);
    cmbRetentionDays->setFixedWidth(maxW);
    cmbMaxItems->setFixedWidth(maxW);
}

// ============================================================
// 统计信息
// ============================================================

void SettingsHistoryTab::updateHistoryStats() {
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
