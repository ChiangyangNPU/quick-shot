#include "HistoryWindow.h"
#include "HistoryManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../core/StyleManager.h"
#include "../utils/Utils.h"

#include <QVBoxLayout>
#include <QFileInfo>
#include "MessageBox.h"
#include <QStyle>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
HistoryWindow::HistoryWindow(QWidget *parent)
    : QWidget(parent)
    , m_tabWidget(nullptr)
    , m_searchEdit(nullptr)
    , m_searchLabel(nullptr)
    , m_searchBtn(nullptr)
    , m_filterLabel(nullptr)
    , m_filterCombo(nullptr)
    , m_listWidget(nullptr)
    , m_copyBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_loadMoreBtn(nullptr)
    , m_countLabel(nullptr)
    , m_toolbarContainer(nullptr)
    , m_listContainer(nullptr)
    , m_bottomContainer(nullptr)
    , m_currentPage(0)
    , m_pageSize(50)
    , m_isLoading(false)
    , m_refreshTimer(nullptr)
{
    LOG_INFO("HistoryWindow instance created");

    setupUi();

    TranslationManager *tm = TranslationManager::instance();
    setWindowTitle(tm->get("history.window.title", "History"));
    setWindowIcon(StyleManager::loadAppIcon());
    // 初始尺寸按 DPI 缩放（100% 下为 1000x720，加大以容纳更多内容）
    resize(calculateDpiScaledSize());
    setWindowFlags(windowFlags() | Qt::Window);

    // 连接语言切换信号，实现实时翻译
    connect(tm, &TranslationManager::languageChanged, this, [this](const QString &) {
        retranslateUi();
    });

    // 连接历史记录新增信号，实现实时刷新
    connect(HistoryManager::instance(), &HistoryManager::itemAdded,
            this, [this](const HistoryItem &) {
        // 延迟刷新，避免短时间内多次添加导致频繁刷新
        if (!m_refreshTimer) {
            m_refreshTimer = new QTimer(this);
            m_refreshTimer->setSingleShot(true);
            m_refreshTimer->setInterval(300);
            connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
                if (isVisible()) {
                    refreshItems();
                }
            });
        }
        m_refreshTimer->start();
    });

    // 监听屏幕逻辑 DPI 变化（系统缩放比例调整时重新应用 qss 并重算控件尺寸）
    // 配合 QT_ENABLE_HIGHDPI_SCALING=0 的手动 DPI 适配策略
    // 使用 UniqueConnection 避免窗口多次显示时重复连接
    if (QScreen *screen = this->screen()) {
        connect(screen, &QScreen::logicalDotsPerInchChanged,
                this, &HistoryWindow::onDpiChanged, Qt::UniqueConnection);
    }

    // 加载初始数据
    loadItems(HistoryType::All, true);
}

/**
 * @brief 屏幕 DPI 变化槽函数
 *
 * 系统缩放比例调整时触发，重新应用全局 qss 并重新计算控件尺寸。
 * @author chiangyang
 */
void HistoryWindow::onDpiChanged() {
    // 重新应用全局 qss：qss 只在 setStyleSheet() 调用时解析一次，
    // DPI 变化后 pt（字体）和 em（控件尺寸）不会自动更新，需重新应用
    StyleManager::reapplyGlobalStyleSheet();

    // 窗口尺寸按新 DPI 重新设置（历史窗口不锁定尺寸，用 resize 即可）
    resize(calculateDpiScaledSize());

    // 延迟到下一事件循环，让 Qt 先完成字体/布局的 DPI 更新，
    // 再重新计算控件尺寸（基于新字体的 sizeHint）
    QTimer::singleShot(0, this, [this]() {
        updateControlSizes();
    });
}

/**
 * @brief 窗口显示事件
 * @param event 事件
 * @author chiangyang
 */
void HistoryWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 每次显示时同步最新背景色（用户可能在 SettingsWindow 改过选项卡背景色）
    updateWindowColors();
    refreshItems();

    // 重新绑定当前屏幕的 DPI 变化信号（窗口可能已移动到其他屏幕）
    if (QScreen *screen = this->screen()) {
        connect(screen, &QScreen::logicalDotsPerInchChanged,
                this, &HistoryWindow::onDpiChanged, Qt::UniqueConnection);
    }
}

/**
 * @brief 更新窗口及各容器背景色
 *
 * 从 StyleManager 读取最新的选项卡背景色，同步刷新窗口及顶部工具栏、
 * 列表区、底部操作栏容器的背景，保证与 SettingsWindow 视觉一致。
 * @author chiangyang
 */
void HistoryWindow::updateWindowColors()
{
    QColor bgColor = StyleManager::getTabWidgetBgColor();
    setStyleSheet(QString("background-color: %1;").arg(bgColor.name()));
    if (m_toolbarContainer) {
        m_toolbarContainer->setStyleSheet(QString("background-color: %1;").arg(bgColor.name()));
    }
    if (m_listContainer) {
        m_listContainer->setStyleSheet(QString("background-color: %1;").arg(bgColor.name()));
    }
    if (m_bottomContainer) {
        m_bottomContainer->setStyleSheet(QString("background-color: %1;").arg(bgColor.name()));
    }
    // 选项卡标签颜色（含 pane 背景）一并刷新
    if (m_tabWidget) {
        m_tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
    }
}

/**
 * @brief 重新计算并应用控件尺寸
 *
 * DPI 变化时调用。基于当前字体（随 DPI 缩放）重新计算所有控件高度
 * （搜索框、按钮、下拉框等）与列表图标大小，确保 DPI 变化后控件
 * 尺寸与字体协调，避免内容截断或图标偏小。
 *
 * 高度策略：所有按钮（搜索/复制/保存/删除/清空/加载更多）统一用搜索框
 * sizeHint 高度，保持视觉一致
 * @author chiangyang
 */
void HistoryWindow::updateControlSizes() {
    // 搜索框、下拉框、所有按钮统一用搜索框 sizeHint 高度
    // qss 重新应用后 sizeHint 反映新字体，高度随之更新
    if (m_searchEdit) {
        int inputHeight = m_searchEdit->sizeHint().height();
        m_searchEdit->setFixedHeight(inputHeight);
        if (m_filterCombo) m_filterCombo->setFixedHeight(inputHeight);
        if (m_searchBtn) m_searchBtn->setFixedHeight(inputHeight);
        if (m_loadMoreBtn) m_loadMoreBtn->setFixedHeight(inputHeight);
        if (m_copyBtn) m_copyBtn->setFixedHeight(inputHeight);
        if (m_saveBtn) m_saveBtn->setFixedHeight(inputHeight);
        if (m_deleteBtn) m_deleteBtn->setFixedHeight(inputHeight);
        if (m_clearBtn) m_clearBtn->setFixedHeight(inputHeight);
    }

    // 列表图标大小与间距：按屏幕逻辑 DPI 缩放
    // 100% DPI（96）为基准，对应 200x150 图标和 10px 间距
    if (m_listWidget) {
        QScreen *screen = this->screen();
        int logicalDpi = screen ? screen->logicalDotsPerInch() : 96;
        int iconW = 200 * logicalDpi / 96;
        int iconH = 150 * logicalDpi / 96;
        m_listWidget->setIconSize(QSize(iconW, iconH));
        m_listWidget->setSpacing(10 * logicalDpi / 96);
    }
}

/**
 * @brief 根据当前屏幕逻辑 DPI 计算窗口初始尺寸
 *
 * 96 DPI（100% 缩放）为基准，对应 1000×720；按比例放大。
 * 用于构造时设置初始尺寸，以及 DPI 变化时更新窗口尺寸。
 * 注意：历史记录窗口允许用户拖拽改变大小，此处只设初始/建议尺寸，
 * 不像 SettingsWindow 那样锁定。
 * @return 基于 DPI 缩放后的窗口尺寸
 * @author chiangyang
 */
QSize HistoryWindow::calculateDpiScaledSize() const {
    const int baseWidth = 1000;
    const int baseHeight = 720;
    const int baseDpi = 96;
    QScreen *screen = this->screen();
    int logicalDpi = screen ? screen->logicalDotsPerInch() : baseDpi;
    int width = baseWidth * logicalDpi / baseDpi;
    int height = baseHeight * logicalDpi / baseDpi;
    return QSize(width, height);
}

/**
 * @brief 初始化 UI
 * @author chiangyang
 */
void HistoryWindow::setupUi()
{
    TranslationManager *tm = TranslationManager::instance();

    // 窗口背景设为选项卡背景色，与 SettingsWindow 保持一致
    // （SettingsWindow 由 tabWidget::pane 填充整个窗口，此处用窗口背景实现一致效果）
    setStyleSheet(QString("background-color: %1;")
                      .arg(StyleManager::getTabWidgetBgColor().name()));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 顶部工具栏 ==========
    // 顶部工具栏容器：背景色与选项卡一致
    m_toolbarContainer = new QWidget(this);
    m_toolbarContainer->setStyleSheet(
        QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QHBoxLayout *toolbarLayout = new QHBoxLayout(m_toolbarContainer);
    toolbarLayout->setContentsMargins(10, 10, 10, 5);
    toolbarLayout->setSpacing(10);

    // 搜索框
    m_searchEdit = new QLineEdit(m_toolbarContainer);
    m_searchEdit->setStyleSheet(StyleManager::getPathEditStyle());
    m_searchEdit->setPlaceholderText(tm->get("history.window.searchPlaceholder", "Search keywords..."));
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &HistoryWindow::onSearchReturnPressed);

    // 搜索按钮：与复制/保存/删除按钮保持一致的常规样式
    m_searchBtn = new QPushButton(tm->get("history.window.search", "Search"), m_toolbarContainer);
    m_searchBtn->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(m_searchBtn, &QPushButton::clicked, this, &HistoryWindow::onSearchClicked);

    // 时间筛选
    m_filterCombo = new QComboBox(m_toolbarContainer);
    m_filterCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    m_filterCombo->addItem(tm->get("history.window.timeRanges.all", "All Time"), 0);
    m_filterCombo->addItem(tm->get("history.window.timeRanges.today", "Today"), 1);
    m_filterCombo->addItem(tm->get("history.window.timeRanges.week", "Last 7 Days"), 7);
    m_filterCombo->addItem(tm->get("history.window.timeRanges.month", "Last 30 Days"), 30);

    m_searchLabel = new QLabel(tm->get("history.window.search", "Search") + ":", m_toolbarContainer);
    m_filterLabel = new QLabel(tm->get("history.window.filter", "Filter") + ":", m_toolbarContainer);
    toolbarLayout->addWidget(m_searchLabel);
    toolbarLayout->addWidget(m_searchEdit, 1);
    toolbarLayout->addWidget(m_searchBtn);
    toolbarLayout->addSpacing(15);
    toolbarLayout->addWidget(m_filterLabel);
    toolbarLayout->addWidget(m_filterCombo);
    toolbarLayout->addStretch();

    // 工具栏控件高度在 updateControlSizes() 中统一设置（DPI 变化时也会重新计算）

    mainLayout->addWidget(m_toolbarContainer);

    // ========== 选项卡 ==========
    // 应用与设置窗口一致的选项卡样式（常规/快捷键等选项卡风格）
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
    m_tabWidget->addTab(new QWidget(), tm->get("history.window.tabAll", "All"));
    m_tabWidget->addTab(new QWidget(), tm->get("history.window.tabScreenshots", "Screenshots"));
    m_tabWidget->addTab(new QWidget(), tm->get("history.window.tabTexts", "Texts"));
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &HistoryWindow::onTabChanged);

    mainLayout->addWidget(m_tabWidget);

    // ========== 列表控件 ==========
    // 列表容器：带外边距，避免内容贴边
    m_listContainer = new QWidget(this);
    m_listContainer->setStyleSheet(
        QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *listLayout = new QVBoxLayout(m_listContainer);
    listLayout->setContentsMargins(10, 5, 10, 5);
    listLayout->setSpacing(10);

    m_listWidget = new QListWidget(m_listContainer);
    m_listWidget->setViewMode(QListWidget::IconMode);
    // 图标大小和间距在 updateControlSizes() 中按 DPI 设置（DPI 变化时也会重新计算）
    m_listWidget->setResizeMode(QListWidget::Adjust);
    m_listWidget->setMovement(QListWidget::Static);
    m_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setWordWrap(true);

    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &HistoryWindow::onItemDoubleClicked);
    // 使用 itemSelectionChanged 替代 itemClicked，使框选时也能更新按钮状态
    connect(m_listWidget, &QListWidget::itemSelectionChanged,
            this, &HistoryWindow::onItemSelectionChanged);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &HistoryWindow::onCustomContextMenu);

    listLayout->addWidget(m_listWidget, 1);

    // ========== 加载更多按钮 ==========
    m_loadMoreBtn = new QPushButton(tm->get("history.window.loadMore", "Load More"), m_listContainer);
    m_loadMoreBtn->setStyleSheet(StyleManager::getSettingsButtonStyle());
    // 按钮高度在 updateControlSizes() 中统一设置
    connect(m_loadMoreBtn, &QPushButton::clicked, this, &HistoryWindow::onLoadMore);
    listLayout->addWidget(m_loadMoreBtn);

    mainLayout->addWidget(m_listContainer, 1);

    // ========== 底部操作栏 ==========
    m_bottomContainer = new QWidget(this);
    m_bottomContainer->setStyleSheet(
        QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomContainer);
    bottomLayout->setContentsMargins(10, 5, 10, 10);
    bottomLayout->setSpacing(10);

    // 主操作按钮（清空历史）用 #primaryButton，其余常规按钮保持默认高度
    m_copyBtn = new QPushButton(tm->get("history.window.actions.copy", "Copy"), m_bottomContainer);
    m_saveBtn = new QPushButton(tm->get("history.window.actions.save", "Save"), m_bottomContainer);
    m_deleteBtn = new QPushButton(tm->get("history.window.actions.delete", "Delete"), m_bottomContainer);
    m_clearBtn = new QPushButton(tm->get("history.window.actions.clear", "Clear History"), m_bottomContainer);

    m_copyBtn->setEnabled(false);
    m_saveBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    // 底部按钮高度在 updateControlSizes() 中统一设置（与搜索框等高）
    // 清空历史按钮与复制/保存/删除保持一致的常规样式

    QString buttonStyle = StyleManager::getSettingsButtonStyle();
    m_copyBtn->setStyleSheet(buttonStyle);
    m_saveBtn->setStyleSheet(buttonStyle);
    m_deleteBtn->setStyleSheet(buttonStyle);
    m_clearBtn->setStyleSheet(buttonStyle);

    connect(m_copyBtn, &QPushButton::clicked, this, &HistoryWindow::onCopyText);
    connect(m_saveBtn, &QPushButton::clicked, this, &HistoryWindow::onSaveScreenshot);
    connect(m_deleteBtn, &QPushButton::clicked, this, &HistoryWindow::onDeleteItem);
    connect(m_clearBtn, &QPushButton::clicked, this, &HistoryWindow::onClearAll);

    m_countLabel = new QLabel(m_bottomContainer);

    bottomLayout->addWidget(m_copyBtn);
    bottomLayout->addWidget(m_saveBtn);
    bottomLayout->addWidget(m_deleteBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_countLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_clearBtn);

    mainLayout->addWidget(m_bottomContainer);

    updateButtonStates();
    // 统一设置所有控件高度和图标尺寸（DPI 变化时也会重新调用）
    updateControlSizes();
}

/**
 * @brief 重新翻译 UI 文本
 *
 * 在语言切换后更新窗口中所有界面文本，包括标题、按钮、标签、
 * 选项卡、下拉框选项等，并重新加载列表项以更新 tooltip 翻译。
 * @author chiangyang
 */
void HistoryWindow::retranslateUi()
{
    TranslationManager *tm = TranslationManager::instance();

    // 窗口标题
    setWindowTitle(tm->get("history.window.title", "History"));

    // 搜索框
    if (m_searchEdit) {
        m_searchEdit->setPlaceholderText(tm->get("history.window.searchPlaceholder", "Search keywords..."));
    }

    // 搜索和筛选标签
    if (m_searchLabel) {
        m_searchLabel->setText(tm->get("history.window.search", "Search") + ":");
    }
    if (m_filterLabel) {
        m_filterLabel->setText(tm->get("history.window.filter", "Filter") + ":");
    }

    // 搜索按钮
    if (m_searchBtn) {
        m_searchBtn->setText(tm->get("history.window.search", "Search"));
    }

    // 时间筛选下拉框（保持当前选中项）
    if (m_filterCombo) {
        int currentIndex = m_filterCombo->currentIndex();
        m_filterCombo->setItemText(0, tm->get("history.window.timeRanges.all", "All Time"));
        m_filterCombo->setItemText(1, tm->get("history.window.timeRanges.today", "Today"));
        m_filterCombo->setItemText(2, tm->get("history.window.timeRanges.week", "Last 7 Days"));
        m_filterCombo->setItemText(3, tm->get("history.window.timeRanges.month", "Last 30 Days"));
        m_filterCombo->setCurrentIndex(currentIndex);
    }

    // 选项卡标题
    if (m_tabWidget) {
        m_tabWidget->setTabText(0, tm->get("history.window.tabAll", "All"));
        m_tabWidget->setTabText(1, tm->get("history.window.tabScreenshots", "Screenshots"));
        m_tabWidget->setTabText(2, tm->get("history.window.tabTexts", "Texts"));
    }

    // 加载更多按钮
    if (m_loadMoreBtn) {
        m_loadMoreBtn->setText(tm->get("history.window.loadMore", "Load More"));
    }

    // 底部操作按钮
    if (m_copyBtn) {
        m_copyBtn->setText(tm->get("history.window.actions.copy", "Copy"));
    }
    if (m_saveBtn) {
        m_saveBtn->setText(tm->get("history.window.actions.save", "Save"));
    }
    if (m_deleteBtn) {
        m_deleteBtn->setText(tm->get("history.window.actions.delete", "Delete"));
    }
    if (m_clearBtn) {
        m_clearBtn->setText(tm->get("history.window.actions.clear", "Clear History"));
    }

    // 重新加载列表项以更新 tooltip 翻译
    refreshItems();

    LOG_INFO("HistoryWindow retranslated UI");
}

/**
 * @brief 刷新历史记录列表
 * @author chiangyang
 */
void HistoryWindow::refreshItems()
{
    loadItems(currentType(), true);
}

/**
 * @brief 加载历史记录列表
 * @param type 记录类型筛选
 * @param reset 是否重置分页
 * @author chiangyang
 */
void HistoryWindow::loadItems(HistoryType type, bool reset)
{
    if (m_isLoading) {
        return;
    }

    m_isLoading = true;

    if (reset) {
        m_currentPage = 0;
        m_listWidget->clear();
    }

    HistoryManager *manager = HistoryManager::instance();

    QList<HistoryItem> items;
    if (!m_currentSearch.isEmpty()) {
        items = manager->searchItems(m_currentSearch, type);
    } else {
        items = manager->getItems(type, m_currentPage, m_pageSize);
    }

    for (const HistoryItem &item : items) {
        QListWidgetItem *listItem = nullptr;
        if (item.isScreenshot()) {
            listItem = createScreenshotItem(item);
        } else {
            listItem = createTextItem(item);
        }
        if (listItem) {
            m_listWidget->addItem(listItem);
        }
    }

    m_currentPage++;

    // 更新加载更多按钮状态：没有更多数据时隐藏按钮
    int totalCount = manager->getItemCount(type);
    int loadedCount = m_listWidget->count();
    bool hasMore = loadedCount < totalCount;
    m_loadMoreBtn->setVisible(hasMore);
    m_loadMoreBtn->setEnabled(hasMore);

    updateItemCount();
    m_isLoading = false;

    LOG_INFO(QString("History items loaded: type=%1, page=%2, loaded=%3, total=%4")
             .arg(static_cast<int>(type)).arg(m_currentPage).arg(loadedCount).arg(totalCount));
}

/**
 * @brief 创建截图列表项
 * @param item 历史记录项
 * @return 列表项
 * @author chiangyang
 */
QListWidgetItem* HistoryWindow::createScreenshotItem(const HistoryItem &item)
{
    QListWidgetItem *listItem = new QListWidgetItem();
    TranslationManager *tm = TranslationManager::instance();

    listItem->setData(Qt::UserRole, item.id);
    listItem->setData(Qt::UserRole + 1, static_cast<int>(item.type));

    // 设置缩略图
    QIcon icon;
    if (!item.thumbnailPath.isEmpty() && QFile::exists(item.thumbnailPath)) {
        QPixmap pixmap(item.thumbnailPath);
        icon = QIcon(pixmap);
    }
    listItem->setIcon(icon);

    // 设置文本：窗口标题 + 时间
    QString displayText = item.windowTitle.isEmpty()
                              ? QFileInfo(item.content).fileName()
                              : item.windowTitle;
    QString timeStr = item.timestamp.toString("yyyy-MM-dd HH:mm");
    listItem->setText(QString("%1\n%2").arg(displayText).arg(timeStr));
    listItem->setToolTip(QString("%1 %2\n%3 %4x%5\n%6 %7")
                             .arg(tm->get("history.window.tooltip.file", "File:"))
                             .arg(item.content)
                             .arg(tm->get("history.window.tooltip.size", "Size:"))
                             .arg(item.imageSize.width())
                             .arg(item.imageSize.height())
                             .arg(tm->get("history.window.tooltip.time", "Time:"))
                             .arg(timeStr));

    return listItem;
}

/**
 * @brief 创建文本列表项
 * @param item 历史记录项
 * @return 列表项
 * @author chiangyang
 */
QListWidgetItem* HistoryWindow::createTextItem(const HistoryItem &item)
{
    QListWidgetItem *listItem = new QListWidgetItem();
    TranslationManager *tm = TranslationManager::instance();

    listItem->setData(Qt::UserRole, item.id);
    listItem->setData(Qt::UserRole + 1, static_cast<int>(item.type));

    // 使用标准文件图标
    QIcon icon = style()->standardIcon(QStyle::SP_FileIcon);
    listItem->setIcon(icon);

    // 设置文本：内容预览 + 来源 + 时间
    QString preview = item.content.left(50);
    if (item.content.length() > 50) {
        preview += "...";
    }
    QString source = item.sourceApp.isEmpty()
                         ? tm->get("history.window.unknown", "Unknown")
                         : item.sourceApp;
    QString timeStr = item.timestamp.toString("yyyy-MM-dd HH:mm");
    listItem->setText(QString("%1\n[%2] %3").arg(preview).arg(source).arg(timeStr));
    listItem->setToolTip(QString("%1 %2\n%3 %4\n%5 %6")
                             .arg(tm->get("history.window.tooltip.content", "Content:"))
                             .arg(item.content)
                             .arg(tm->get("history.window.tooltip.source", "Source:"))
                             .arg(source)
                             .arg(tm->get("history.window.tooltip.time", "Time:"))
                             .arg(timeStr));

    return listItem;
}

/**
 * @brief 选项卡切换槽函数
 * @author chiangyang
 */
void HistoryWindow::onTabChanged(int index)
{
    HistoryType type = currentType();
    m_currentSearch.clear();
    if (m_searchEdit) {
        m_searchEdit->clear();
    }
    loadItems(type, true);
}

/**
 * @brief 搜索按钮点击槽函数
 * @author chiangyang
 */
void HistoryWindow::onSearchClicked()
{
    m_currentSearch = m_searchEdit->text().trimmed();
    loadItems(currentType(), true);
}

/**
 * @brief 搜索框回车槽函数
 * @author chiangyang
 */
void HistoryWindow::onSearchReturnPressed()
{
    onSearchClicked();
}

/**
 * @brief 列表项双击槽函数
 * @param item 被双击的列表项
 * @author chiangyang
 */
void HistoryWindow::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    qint64 id = item->data(Qt::UserRole).toLongLong();
    HistoryItem historyItem = HistoryManager::instance()->getItemById(id);

    if (historyItem.isScreenshot()) {
        // 打开截图文件
        QDesktopServices::openUrl(QUrl::fromLocalFile(historyItem.content));
        LOG_INFO(QString("Screenshot opened: id=%1, path=%2").arg(id).arg(historyItem.content));
    } else {
        // 复制文本到剪贴板
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(historyItem.content);
        LOG_INFO(QString("Text copied on double click: id=%1").arg(id));
    }
}

/**
 * @brief 列表选择变化槽函数
 *
 * 选中项变化时（包括点击、框选、Ctrl/Shift+点击）更新按钮状态。
 * @author chiangyang
 */
void HistoryWindow::onItemSelectionChanged()
{
    updateButtonStates();
}

/**
 * @brief 复制文本槽函数
 *
 * 根据选中记录的类型，复制文本内容或截图到系统剪贴板。
 * @author chiangyang
 */
void HistoryWindow::onCopyText()
{
    qint64 id = getSelectedItemId();
    if (id <= 0) {
        return;
    }

    HistoryItem item = HistoryManager::instance()->getItemById(id);
    QClipboard *clipboard = QApplication::clipboard();

    if (item.isClipboardText()) {
        clipboard->setText(item.content);
        LOG_INFO(QString("Text copied to clipboard: id=%1").arg(id));
    } else if (item.isScreenshot()) {
        QPixmap pixmap(item.content);
        if (!pixmap.isNull()) {
            clipboard->setPixmap(pixmap);
            LOG_INFO(QString("Screenshot copied to clipboard: id=%1").arg(id));
        } else {
            LOG_WARNING(QString("Failed to load screenshot for copy: id=%1, path=%2").arg(id).arg(item.content));
        }
    }
}

/**
 * @brief 保存截图槽函数
 *
 * 将选中的截图记录另存到用户指定路径，复用主程序的 Utils::savePixmapToFile
 * 方法，保证保存路径、目录记忆等行为与主程序完全一致。
 * @author chiangyang
 */
void HistoryWindow::onSaveScreenshot()
{
    qint64 id = getSelectedItemId();
    if (id <= 0) {
        return;
    }

    HistoryItem item = HistoryManager::instance()->getItemById(id);
    if (!item.isScreenshot()) {
        return;
    }

    QString sourcePath = item.content;
    if (!QFile::exists(sourcePath)) {
        TranslationManager *tm = TranslationManager::instance();
        MessageBox::warning(this,
            tm->get("history.window.error", "Error"),
            tm->get("history.window.fileNotFound", "Source file not found"));
        LOG_WARNING(QString("Screenshot source file not found: id=%1, path=%2").arg(id).arg(sourcePath));
        return;
    }

    TranslationManager *tm = TranslationManager::instance();

    // 默认文件名：使用原始文件名
    QString defaultName = QFileInfo(sourcePath).fileName();

    // 复用主程序的保存方法，保证保存路径、目录记忆等行为与主程序一致
    QString savedPath = Utils::savePixmapToFile(
        this,
        [sourcePath]() { return QPixmap(sourcePath); },
        defaultName,
        tm->get("history.window.saveScreenshot", "Save Screenshot"),
        QStringLiteral("PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)"));

    if (!savedPath.isEmpty()) {
        LOG_INFO(QString("Screenshot saved: id=%1, from=%2, to=%3").arg(id).arg(sourcePath).arg(savedPath));
    }
}

/**
 * @brief 删除记录槽函数
 *
 * 弹出确认对话框，用户确认后删除选中的历史记录（支持单条和批量删除）。
 * @author chiangyang
 */
void HistoryWindow::onDeleteItem()
{
    QList<qint64> ids = getSelectedItemIds();
    if (ids.isEmpty()) {
        return;
    }

    TranslationManager *tm = TranslationManager::instance();

    QString msg;
    if (ids.size() == 1) {
        msg = tm->get("history.window.confirmDeleteMsg",
                      "Are you sure you want to delete this record?");
    } else {
        msg = tm->get("history.window.confirmDeleteMultiMsg",
                      "Are you sure you want to delete the selected %1 record(s)?")
                  .arg(ids.size());
    }

    if (MessageBox::question(this,
            tm->get("history.window.confirmDeleteTitle", "Confirm Delete"),
            msg)) {
        for (qint64 id : ids) {
            HistoryManager::instance()->removeItem(id);
        }
        loadItems(currentType(), true);
        LOG_INFO(QString("History item(s) deleted by user: count=%1").arg(ids.size()));
    }
}

/**
 * @brief 清空历史槽函数
 *
 * 弹出确认对话框，用户确认后清空所有历史记录。
 * @author chiangyang
 */
void HistoryWindow::onClearAll()
{
    TranslationManager *tm = TranslationManager::instance();

    if (MessageBox::question(this,
            tm->get("history.dataManagement.confirmClearTitle", "Confirm Clear"),
            tm->get("history.dataManagement.confirmClearMsg",
                    "Are you sure you want to clear all history? This action cannot be undone."))) {
        HistoryManager::instance()->clearAll();
        loadItems(currentType(), true);
        LOG_INFO("All history cleared by user");
    }
}

/**
 * @brief 加载更多数据槽函数
 * @author chiangyang
 */
void HistoryWindow::onLoadMore()
{
    loadItems(currentType(), false);
}

/**
 * @brief 右键菜单槽函数
 *
 * 右键点击列表项时弹出上下文菜单。支持多选场景：
 * - 右键点击已选中项时保持多选状态，菜单显示批量删除
 * - 右键点击未选中项时清空选中并仅选中该项，菜单显示单项操作
 * @author chiangyang
 */
void HistoryWindow::onCustomContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_listWidget->itemAt(pos);
    if (!item) {
        // 点击空白处，清空选中
        m_listWidget->clearSelection();
        return;
    }

    // 若右键点击的项不在已选中集合中，则清空选中并仅选中该项
    if (!item->isSelected()) {
        m_listWidget->clearSelection();
        item->setSelected(true);
    }

    QList<QListWidgetItem *> selectedItems = m_listWidget->selectedItems();
    int selectedCount = selectedItems.size();
    bool multiSelect = (selectedCount > 1);

    qint64 id = item->data(Qt::UserRole).toLongLong();
    int typeInt = item->data(Qt::UserRole + 1).toInt();
    HistoryType type = static_cast<HistoryType>(typeInt);

    HistoryItem historyItem = HistoryManager::instance()->getItemById(id);
    TranslationManager *tm = TranslationManager::instance();

    QMenu menu(this);
    // 设置菜单样式，与 PinWindow 右键菜单保持一致
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(StyleManager::getMenuStyle());

    // 多选时仅显示批量删除；单选时显示复制/保存/删除
    QAction *copyAction = nullptr;
    QAction *saveAction = nullptr;
    if (!multiSelect) {
        copyAction = menu.addAction(tm->get("history.window.actions.copy", "Copy"));
        if (type == HistoryType::Screenshot) {
            saveAction = menu.addAction(tm->get("history.window.actions.save", "Save Screenshot"));
        }
        menu.addSeparator();
    }
    QAction *deleteAction = menu.addAction(
        multiSelect
            ? tm->get("history.window.actions.deleteSelected", "Delete Selected (%1)").arg(selectedCount)
            : tm->get("history.window.actions.delete", "Delete"));

    QAction *selectedAction = menu.exec(m_listWidget->mapToGlobal(pos));

    if (selectedAction == copyAction) {
        QClipboard *clipboard = QApplication::clipboard();
        if (historyItem.isClipboardText()) {
            clipboard->setText(historyItem.content);
        } else {
            QPixmap pixmap(historyItem.content);
            if (!pixmap.isNull()) {
                clipboard->setPixmap(pixmap);
            }
        }
    } else if (saveAction && selectedAction == saveAction) {
        // 复用主程序的保存方法，保证保存路径、目录记忆等行为与主程序一致
        QString sourcePath = historyItem.content;
        QString defaultName = QFileInfo(sourcePath).fileName();
        Utils::savePixmapToFile(
            this,
            [sourcePath]() { return QPixmap(sourcePath); },
            defaultName,
            tm->get("history.window.saveScreenshot", "Save Screenshot"),
            QStringLiteral("PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)"));
    } else if (selectedAction == deleteAction) {
        // 复用 onDeleteItem，支持单条和批量删除（含确认对话框）
        onDeleteItem();
    }
}

/**
 * @brief 获取选中记录的 ID
 * @return 当前项的记录 ID，未选中返回 -1
 * @author chiangyang
 */
qint64 HistoryWindow::getSelectedItemId() const
{
    QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return -1;
    }
    return items.first()->data(Qt::UserRole).toLongLong();
}

/**
 * @brief 获取所有选中记录的 ID 列表
 * @return 选中记录的 ID 列表，未选中返回空列表
 * @author chiangyang
 */
QList<qint64> HistoryWindow::getSelectedItemIds() const
{
    QList<qint64> ids;
    const QList<QListWidgetItem *> items = m_listWidget->selectedItems();
    for (const QListWidgetItem *item : items) {
        ids.append(item->data(Qt::UserRole).toLongLong());
    }
    return ids;
}

/**
 * @brief 获取当前选项卡对应的记录类型
 * @author chiangyang
 */
HistoryType HistoryWindow::currentType() const
{
    int index = m_tabWidget->currentIndex();
    switch (index) {
        case 0: return HistoryType::All;
        case 1: return HistoryType::Screenshot;
        case 2: return HistoryType::ClipboardText;
        default: return HistoryType::All;
    }
}

/**
 * @brief 更新记录统计信息
 * @author chiangyang
 */
void HistoryWindow::updateItemCount()
{
    HistoryType type = currentType();
    int count = HistoryManager::instance()->getItemCount(type);

    TranslationManager *tm = TranslationManager::instance();
    m_countLabel->setText(QString("%1 %2").arg(count).arg(tm->get("history.stats.items", "items")));
}

/**
 * @brief 更新按钮状态
 *
 * 根据当前选中项的数量和类型更新底部操作按钮的启用状态：
 * - 未选中：所有操作按钮禁用
 * - 选中单个：复制可用，保存仅在截图类型时可用，删除可用
 * - 选中多个：复制和保存禁用，删除可用（批量删除）
 * @author chiangyang
 */
void HistoryWindow::updateButtonStates()
{
    QList<QListWidgetItem *> selectedItems = m_listWidget->selectedItems();
    int count = selectedItems.size();

    if (count == 0) {
        m_copyBtn->setEnabled(false);
        m_saveBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
    } else if (count == 1) {
        qint64 id = selectedItems.first()->data(Qt::UserRole).toLongLong();
        HistoryItem item = HistoryManager::instance()->getItemById(id);
        bool isScreenshot = item.isScreenshot();

        m_copyBtn->setEnabled(true);
        m_saveBtn->setEnabled(isScreenshot);
        m_deleteBtn->setEnabled(true);
    } else {
        // 多选：复制和保存仅支持单条操作，删除支持批量
        m_copyBtn->setEnabled(false);
        m_saveBtn->setEnabled(false);
        m_deleteBtn->setEnabled(true);
    }
}
