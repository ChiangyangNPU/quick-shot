#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <csignal>

#include "shortcut/ShortcutManager.h"
#include "shortcut/ShortcutTypes.h"
#include "core/ConfigManager.h"
#include "core/TranslationManager.h"
#include "core/StyleManager.h"
#include "capture/SnipScreen.h"
#include "widgets/SettingsWindow.h"
#include "history/HistoryManager.h"
#include "history/ClipboardMonitor.h"
#include "widgets/HistoryWindow.h"
#include "../log/Logger.h"

// 版本号宏，由 CMake 通过 add_compile_definitions 注入
// 当未通过 CMake 构建时（如临时单文件编译），提供兜底默认值
#ifndef QUICKSHOT_VERSION
#define QUICKSHOT_VERSION "0.0.0-unknown"
#endif

/**
 * @brief 信号处理函数
 * @param sig 信号类型
 *
 * 处理SIGINT和SIGTERM信号，用于优雅退出应用程序
 * @author chiangyang
 */
void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        QCoreApplication::quit();
    }
}

/**
 * @brief 主函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 *
 * 应用程序入口点，负责：
 * 1. 初始化核心管理器（配置、翻译、样式）
 * 2. 创建截图主界面 SnipScreen
 * 3. 通过 ShortcutManager 注册全局快捷键并构建托盘菜单
 * 4. 启动事件循环
 * @author chiangyang
 */
int main(int argc, char *argv[]) {
    // 禁用 Qt 高 DPI 缩放，使用物理像素坐标
    // 这样窗口和截图都使用物理像素，避免多显示器不同 DPI 导致的缩放问题
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

    QApplication app(argc, argv);

    LOG_INFO("QuickShot application starting");

    // 加载全局样式表（qss），使用 em/pt 单位实现 DPI 自适应
    // 字体用 pt 随 DPI 缩放，控件尺寸用 em 相对字体，两者天然协调
    {
        QFile qss(":/stylesheets/app.qss");
        if (qss.open(QFile::ReadOnly)) {
            qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
            LOG_INFO("Global stylesheet loaded");
        } else {
            LOG_INFO("Failed to load global stylesheet");
        }
    }

    // 禁用"关闭最后一个窗口时退出"，托盘程序应通过菜单退出
    QApplication::setQuitOnLastWindowClosed(false);

    // 设置应用程序属性
    // 版本号由 CMake 注入的 QUICKSHOT_VERSION 宏提供，避免在源码中硬编码
    QApplication::setOrganizationName("QuickShot");
    QApplication::setApplicationName("QuickShot");
    QApplication::setApplicationVersion(QString::fromLatin1(QUICKSHOT_VERSION));


    // 注册信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 初始化核心管理器（依赖注入方式）
    // 创建 ConfigManager 实例并设置为全局单例，支持后续通过依赖注入使用
    ConfigManager* configManager = new ConfigManager();
    ConfigManager::setInstance(configManager);

    // StyleManager 启动自初始化：在任何窗口创建之前从持久化配置恢复样式
    // （依赖注入，与窗口构造顺序解耦，SnipScreen 构造即可读到持久化的标注默认值）
    StyleManager::initFromConfig(configManager);

    TranslationManager::instance();

    // 初始化历史记录管理器
    HistoryManager::instance();

    // 启动剪贴板监控
    ClipboardMonitor *clipboardMonitor = new ClipboardMonitor();
    clipboardMonitor->start();
    LOG_INFO("Clipboard monitor started");

    // 创建历史记录查看窗口
    HistoryWindow *historyWindow = new HistoryWindow();

    // 创建截图主界面（StyleManager 已在上面由 initFromConfig 恢复持久化的标注默认值）
    SnipScreen *snipScreen = new SnipScreen();

    // 初始化快捷键子系统：注入依赖 → 批量注册 9 种全局热键
    // 替代原先 main.cpp 中 9 个 GlobalShortcut 局部变量和 9 处 settings->value 读取
    ShortcutManager::instance()->initialize(snipScreen, historyWindow);
    ShortcutManager::instance()->registerAll();

    // 创建系统托盘图标
    QSystemTrayIcon *trayIcon = new QSystemTrayIcon();
    trayIcon->setIcon(StyleManager::loadAppIcon());

    // 创建托盘菜单
    QMenu *trayMenu = new QMenu();
    // 设置菜单样式，与 PinWindow 右键菜单保持一致
    trayMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    trayMenu->setAttribute(Qt::WA_TranslucentBackground);
    trayMenu->setStyleSheet(StyleManager::getMenuStyle());
    TranslationManager *tm = TranslationManager::instance();

    // 创建设置窗口（需在 buildTrayMenu 之前，以便传入）
    SettingsWindow *settingsWindow = new SettingsWindow();

    // 委托 ShortcutManager 构建托盘菜单的快捷键 action 部分
    // 内部按数据表顺序创建 9 个 action（历史→截图→录屏→贴图→全屏→活动窗口→
    // 录屏暂停→录屏停止→TogglePins），并在首尾插入分隔线
    // 相比原 main.cpp 补全了录屏暂停/停止/TogglePins 三个菜单项
    ShortcutManager::instance()->buildTrayMenu(trayMenu, snipScreen,
                                                historyWindow, settingsWindow);

    // 设置动作（buildTrayMenu 已在末尾插入分隔线，此处直接追加）
    QAction *settingsAction = trayMenu->addAction(tm->get("traySettings"));
    QObject::connect(settingsAction, &QAction::triggered, [settingsWindow]() {
        settingsWindow->show();
        settingsWindow->raise();
        settingsWindow->activateWindow();
    });

    trayMenu->addSeparator();

    // 退出动作
    QAction *quitAction = trayMenu->addAction(tm->get("trayQuit"));
    QObject::connect(quitAction, &QAction::triggered, [&app]() {
        app.quit();
    });

    // 工具栏按钮样式变更时同步 StyleManager
    QObject::connect(settingsWindow, &SettingsWindow::toolbarButtonStyleChanged,
                     [](const QString &style) {
        StyleManager::setToolbarButtonStyle(style);
    });

    // 选项卡背景颜色变更时同步更新历史记录窗口
    QObject::connect(settingsWindow, &SettingsWindow::tabWidgetBgColorChanged,
                     historyWindow, &HistoryWindow::updateWindowColors);

    // 标注工具默认值变更时同步刷新工具栏设置
    QObject::connect(settingsWindow, &SettingsWindow::defaultPenWidthChanged,
                     snipScreen, &SnipScreen::refreshAnnotationToolDefaults);
    QObject::connect(settingsWindow, &SettingsWindow::defaultFontSizeChanged,
                     snipScreen, &SnipScreen::refreshAnnotationToolDefaults);
    QObject::connect(settingsWindow, &SettingsWindow::defaultEraserWidthChanged,
                     snipScreen, &SnipScreen::refreshAnnotationToolDefaults);
    QObject::connect(settingsWindow, &SettingsWindow::defaultMosaicSizeChanged,
                     snipScreen, &SnipScreen::refreshAnnotationToolDefaults);

    // 工具栏信号连接到 SettingsWindow（工具栏调节时同步显示到设置页面）
    snipScreen->connectToolBarToSettingsWindow(settingsWindow);

    // 快捷键设置变更时实时更新（委托 ShortcutManager 统一处理）
    // SettingsWindow 发出 (QString, QKeySequence) 信号 →
    // ShortcutManager::updateFromUiString 适配为枚举版 update →
    // 内部 emit shortcutChanged(ShortcutType,...) →
    // TrayMenuBuilder::refresh 自动刷新托盘菜单显示值
    QObject::connect(settingsWindow, &SettingsWindow::shortcutChanged,
                     ShortcutManager::instance(), &ShortcutManager::updateFromUiString);

    // 更新托盘静态文本（设置/退出 action 文本 + tooltip）的函数
    // 9 个快捷键 action 的文本刷新由 TrayMenuBuilder::refresh/retranslate 自动处理
    auto updateStaticText = [trayIcon, settingsAction, quitAction, tm]() {
        settingsAction->setText(tm->get("traySettings"));
        quitAction->setText(tm->get("trayQuit"));
        QString sKey = ShortcutManager::instance()->getSequence(ShortcutType::Snip).toString();
        QString rKey = ShortcutManager::instance()->getSequence(ShortcutType::Record).toString();
        trayIcon->setToolTip(tm->get("runningInBackground", {sKey, rKey}));
    };
    updateStaticText();

    // 语言切换时更新托盘静态文本 + 委托 ShortcutManager 重译快捷键 action 文本
    QObject::connect(tm, &TranslationManager::languageChanged, [&updateStaticText]() {
        updateStaticText();
        ShortcutManager::instance()->retranslateTrayMenu();
    });

    // 快捷键变更时更新 tooltip（快捷键 action 文本已由 TrayMenuBuilder::refresh 自动刷新）
    QObject::connect(ShortcutManager::instance(), &ShortcutManager::shortcutChanged,
                     [&updateStaticText]() {
        updateStaticText();
    });

    trayIcon->setContextMenu(trayMenu);

    // 托盘图标双击 → 截图
    QObject::connect(trayIcon, &QSystemTrayIcon::activated, [snipScreen](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            snipScreen->start();
        }
#ifndef Q_OS_MAC
        // macOS: 左键单击由系统自动显示托盘菜单，不触发截屏
        // 其他平台: 左键单击触发截屏
        else if (reason == QSystemTrayIcon::Trigger) {
            snipScreen->start();
        }
#endif
    });

    // 显示托盘图标
    trayIcon->show();
    LOG_INFO("Tray icon shown, entering event loop");

#ifdef Q_OS_WIN
    // Windows: 显示启动通知
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QString title = tm->get("quickShot", "QuickShot");
        QString snipKey = ShortcutManager::instance()->getSequence(ShortcutType::Snip).toString();
        QString recordKey = ShortcutManager::instance()->getSequence(ShortcutType::Record).toString();
        QString historyKey = ShortcutManager::instance()->getSequence(ShortcutType::History).toString();
        QString message = tm->get("runningInBackground", {snipKey, recordKey, historyKey});
        trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
        LOG_INFO("Startup notification shown");
    }
#endif

#ifdef Q_OS_MAC
    // macOS: 显示启动通知
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QString title = tm->get("quickShot", "QuickShot");
        QString snipKey = ShortcutManager::instance()->getSequence(ShortcutType::Snip).toString();
        QString recordKey = ShortcutManager::instance()->getSequence(ShortcutType::Record).toString();
        QString historyKey = ShortcutManager::instance()->getSequence(ShortcutType::History).toString();
        QString message = tm->get("runningInBackground", {snipKey, recordKey, historyKey});
        trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
        LOG_INFO("Startup notification shown");
    }
#endif

    LOG_INFO("QuickShot application started successfully");
    return app.exec();
}
