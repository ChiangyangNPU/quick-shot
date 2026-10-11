#include "SettingsWindow.h"
#include "../core/StyleManager.h"
#include "../core/ConfigManager.h"
#include "../core/TranslationManager.h"
#include "../shortcut/ShortcutTypes.h"
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

#include <QProgressBar>

#include "Logger.h"

/**
 * @brief 构造函数
 * @param parent 父对象
 * @author chiangyang
 */
SettingsWindow::SettingsWindow(QWidget *parent) : QWidget(parent), heightAnimation(nullptr), m_animatedHeight(0) {
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
 * @brief 重新计算历史记录选项卡下拉框的固定宽度
 *
 * 取保留天数与最大记录数两个下拉框的最大内容宽度作为统一宽度。
 * 在 setupHistoryTab 构造时、语言切换时与 DPI 变化时均会调用，
 * 消除原先 setupHistoryTab 与 retranslateUi 中的重复代码。
 * @author chiangyang
 */
/**
 * @brief 重新计算所有需要固定宽度的控件
 *
 * DPI 变化时调用，依次更新通用选项卡、历史记录选项卡、翻译选项卡
 * 中控件的固定/最小宽度，确保文字不被截断。
 * @author chiangyang
 */
void SettingsWindow::updateControlWidths() {
    if (m_generalTabWidget) m_generalTabWidget->updateControlWidths();
    if (m_historyTabWidget) m_historyTabWidget->updateComboWidths();
    if (m_translateTabWidget) m_translateTabWidget->updateComboWidths();
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
    m_generalTabWidget = new SettingsGeneralTab(this);
    // 语言切换：全窗口重翻译 + 转发给 main.cpp 订阅者
    connect(m_generalTabWidget, &SettingsGeneralTab::languageChanged, this,
            [this](const QString &code) {
                retranslateUi();
                emit languageChanged(code);
            });
    tabWidget->addTab(m_generalTabWidget, "General");
}


/**
 * @brief 设置快捷键选项卡
 * @author chiangyang
 */
void SettingsWindow::setupShortcutsTab() {
    m_shortcutsTabWidget = new SettingsShortcutsTab(this);
    // 快捷键改变 → 信号转发（main.cpp 对 SettingsWindow::shortcutChanged 的订阅保持不变）
    connect(m_shortcutsTabWidget, &SettingsShortcutsTab::shortcutChanged,
            this, &SettingsWindow::shortcutChanged);
    tabWidget->addTab(m_shortcutsTabWidget, "Shortcuts");
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
/**
 * @brief 设置翻译选项卡
 * @author chiangyang
 */
void SettingsWindow::setupTranslateTab() {
    m_translateTabWidget = new SettingsTranslateTab(this);
    tabWidget->addTab(m_translateTabWidget, "Translate");
}


/**
 * @brief 翻译引擎切换槽函数，显隐对应引擎配置行
 * @param index 引擎下拉框索引
 * @author chiangyang
 */
/**
 * @brief 翻译设置改变槽函数（保存配置并重载翻译引擎）
 * @author chiangyang
 */
/**
 * @brief 更新翻译选项卡两个下拉框宽度
 *
 * 取翻译引擎下拉框与目标语言下拉框中所有条目文本最宽者，
 * 统一设置二者的最小宽度，保证两框宽度一致（谁长选谁）。
 * @author chiangyang
 */
/**
 * @brief 设置关于选项卡
 * @author chiangyang
 */
void SettingsWindow::setupAboutTab() {
    m_aboutTab = new SettingsAboutTab(this);
    connect(m_aboutTab, &SettingsAboutTab::contentHeightChanged,
            this, &SettingsWindow::fitWindowHeight);
    tabWidget->addTab(m_aboutTab, TranslationManager::instance()->get("tabAbout", "About"));
}

/**
 * @brief 加载设置
 * @author chiangyang
 */
void SettingsWindow::loadSettings() {
    LOG_INFO("SettingsWindow: loading settings");

    // General 通用选项卡（语言 / 自启 / 日志 / OCR / 保存目录）
    m_generalTabWidget->loadSettings();

    // Style
    m_styleTabWidget->loadStyleSettings();

    retranslateUi();
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

    // About 选项卡文案（应用信息为静态；更新组标题/按钮/状态文字由选项卡自行翻译）
    m_aboutTab->retranslateUi();

    // Translate 选项卡文案（引擎条目 / 官网链接 / 网络提示由选项卡自行翻译）
    m_translateTabWidget->retranslateUi();
    
    // General 通用选项卡文案（语言 / 自启 / 日志 / OCR / 保存目录 / 配置文件由选项卡自行翻译）
    m_generalTabWidget->retranslateUi();


    // Shortcuts 快捷键选项卡文案（行生成 / 分类标题 / 行文案由选项卡自行翻译）
    m_shortcutsTabWidget->retranslateUi();

    // Style 样式选项卡文案（颜色标签 / 默认值 / 按钮样式由选项卡自行翻译）
    m_styleTabWidget->retranslateUi();

    // History 历史记录选项卡文案（含保留天数下拉框重建与统计刷新）
    m_historyTabWidget->retranslateUi();
}



void SettingsWindow::setupStyleTab() {
    m_styleTabWidget = new SettingsStyleTab(this);
    // 跨选项卡联动：tabWidget QSS / 其他选项卡背景 / 全部分组样式
    connect(m_styleTabWidget, &SettingsStyleTab::tabWidgetStyleChanged, this, [this]() {
        tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
    });
    connect(m_styleTabWidget, &SettingsStyleTab::tabWidgetBgChanged, this, [this](const QColor &color) {
        tabWidget->setStyleSheet(StyleManager::getTabWidgetStyle());
        updateAllTabsPalette(color);
        updateAllGroupBoxStyles();
    });
    // 对外信号转发（HistoryWindow / 工具栏订阅保持不变）
    connect(m_styleTabWidget, &SettingsStyleTab::tabWidgetBgColorChanged, this, &SettingsWindow::tabWidgetBgColorChanged);
    connect(m_styleTabWidget, &SettingsStyleTab::toolbarButtonStyleChanged, this, &SettingsWindow::toolbarButtonStyleChanged);
    connect(m_styleTabWidget, &SettingsStyleTab::defaultPenWidthChanged, this, &SettingsWindow::defaultPenWidthChanged);
    connect(m_styleTabWidget, &SettingsStyleTab::defaultFontSizeChanged, this, &SettingsWindow::defaultFontSizeChanged);
    connect(m_styleTabWidget, &SettingsStyleTab::defaultEraserWidthChanged, this, &SettingsWindow::defaultEraserWidthChanged);
    connect(m_styleTabWidget, &SettingsStyleTab::defaultMosaicSizeChanged, this, &SettingsWindow::defaultMosaicSizeChanged);
    tabWidget->addTab(m_styleTabWidget, TranslationManager::instance()->get("tabStyle", "Style"));
}

/**
 * @brief 工具栏画笔粗细变更槽：转发给样式选项卡（SpinBox 同步 + 持久化）
 *
 * 由 SnipScreen::connectToolBarToSettingsWindow 连接工具栏信号；
 * 默认值本身的持久化已在 BaseToolBar 信号源头收拢，此处仅为设置页显示同步。
 * @param width 新的画笔粗细
 * @author chiangyang
 */
void SettingsWindow::onToolPenWidthChanged(int width) {
    m_styleTabWidget->onToolPenWidthChanged(width);
}

/**
 * @brief 工具栏字号变更槽：转发给样式选项卡
 * @param size 新的字号
 * @author chiangyang
 */
void SettingsWindow::onToolFontSizeChanged(int size) {
    m_styleTabWidget->onToolFontSizeChanged(size);
}

/**
 * @brief 工具栏橡皮擦粗细变更槽：转发给样式选项卡
 * @param width 新的橡皮擦粗细
 * @author chiangyang
 */
void SettingsWindow::onToolEraserWidthChanged(int width) {
    m_styleTabWidget->onToolEraserWidthChanged(width);
}

/**
 * @brief 工具栏马赛克大小变更槽：转发给样式选项卡
 * @param size 新的马赛克大小
 * @author chiangyang
 */
void SettingsWindow::onToolMosaicSizeChanged(int size) {
    m_styleTabWidget->onToolMosaicSizeChanged(size);
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
    // 快捷键选项卡索引为 1，其中所有 QKeySequenceEdit 由 SettingsShortcutsTab 数据驱动创建
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
    m_historyTabWidget = new SettingsHistoryTab(this);
    tabWidget->addTab(m_historyTabWidget, TranslationManager::instance()->get("history.tabName", "History"));
}

/**
 * @brief 刷新所有 QGroupBox 的背景样式（绑定到 StyleManager 的 s_tabWidgetBgColor）
 *
 * 各选项卡改色/重置路径统一调用，
 * 覆盖通用/翻译/样式/快捷键/历史记录选项卡的全部 QGroupBox，
 * 避免每个 GroupBox 在每个改色路径下都重复写 if 判断。
 * @author chiangyang
 */
void SettingsWindow::updateAllGroupBoxStyles() {
    const QString style = StyleManager::getGroupBoxStyle();

    // General 通用选项卡（样式刷新内聚在 SettingsGeneralTab）
    if (m_generalTabWidget) m_generalTabWidget->refreshStyles();

    // Translate 翻译选项卡（样式刷新内聚在 SettingsTranslateTab）
    if (m_translateTabWidget) m_translateTabWidget->refreshStyles();

    // Style 样式选项卡（样式刷新内聚在 SettingsStyleTab）
    if (m_styleTabWidget) m_styleTabWidget->refreshStyles();

    // Shortcuts 快捷键选项卡（样式刷新内聚在 SettingsShortcutsTab）
    if (m_shortcutsTabWidget) m_shortcutsTabWidget->refreshStyles();

    // History 历史记录选项卡（样式刷新内聚在 SettingsHistoryTab）
    if (m_historyTabWidget) m_historyTabWidget->refreshStyles();

    // About 关于选项卡（GroupBox 样式刷新内聚在 SettingsAboutTab）
    if (m_aboutTab) m_aboutTab->refreshStyles();
}

/**
 * @brief 刷新历史记录选项卡滚动区的 QPalette 背景色
 * @param color 新的背景色（通常是 StyleManager::getTabWidgetBgColor()）
 *
 * 历史记录选项卡用 QPalette 设背景（避免 setStyleSheet 禁用子控件原生渲染），
 * 每次改颜色时需重新设置选项卡滚动内容与 viewport 的调色板（内聚在
 * SettingsHistoryTab::applyTabBackground）。
 * @author chiangyang
 */
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
    if (m_generalTabWidget) {
        m_generalTabWidget->applyTabBackground(color);
    }
    if (m_translateTabWidget) {
        m_translateTabWidget->applyTabBackground(color);
    }
    if (m_historyTabWidget) {
        m_historyTabWidget->applyTabBackground(color);
    }
    // 快捷键选项卡（背景刷新内聚在 SettingsShortcutsTab）
    if (m_shortcutsTabWidget) m_shortcutsTabWidget->applyTabBackground(color);
}

/**
 * @brief 更新历史统计信息
 * @author chiangyang
 */

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
        
        // 在样式选项卡（index == 2）和历史记录选项卡（index == 4）中处理滚轮滚动
        // （快捷键选项卡的滚轮滚动内聚在 SettingsShortcutsTab::eventFilter）
        int currentIndex = tabWidget->currentIndex();
        QScrollArea *scrollArea = nullptr;
        if (currentIndex == 2 && m_styleTabWidget) {
            scrollArea = m_styleTabWidget->scrollArea();
        } else if (currentIndex == 4 && m_historyTabWidget) {
            scrollArea = m_historyTabWidget->scrollArea();
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






