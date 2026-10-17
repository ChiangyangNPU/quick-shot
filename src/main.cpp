#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QLockFile>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <csignal>

#include "shortcut/ShortcutManager.h"
#include "shortcut/ShortcutTypes.h"
#include "core/ConfigManager.h"
#include "core/TranslationManager.h"
#include "core/StyleManager.h"
#include "capture/SnipScreen.h"
#include "widgets/PinWindow.h"
#include "widgets/SettingsWindow.h"
#include "history/HistoryManager.h"
#include "history/ClipboardMonitor.h"
#include "widgets/HistoryWindow.h"
#include "ocr/OcrEngine.h"
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
 * @brief "已在运行"提示文案（按系统语言挑选，不初始化翻译子系统）
 *
 * 第二个实例在拿到锁之前就退出，此时不应初始化 ConfigManager/
 * TranslationManager（会读写配置文件、与既有实例产生无谓竞争），
 * 故用 QLocale 从六种支持语言中直接选取一句固定文案。
 * @author chiangyang
 */
static QString alreadyRunningText() {
    const QString lang = QLocale::system().name();
    if (lang.contains(QStringLiteral("Hant"), Qt::CaseInsensitive)
        || lang.startsWith(QStringLiteral("zh_TW"))
        || lang.startsWith(QStringLiteral("zh_HK"))) {
        return QStringLiteral("QuickShot 已在執行中");
    }
    if (lang.startsWith(QStringLiteral("zh"))) {
        return QStringLiteral("QuickShot 已在运行中");
    }
    if (lang.startsWith(QStringLiteral("ja"))) {
        return QStringLiteral("QuickShot はすでに起動しています");
    }
    if (lang.startsWith(QStringLiteral("ko"))) {
        return QStringLiteral("QuickShot이 이미 실행 중입니다");
    }
    return QStringLiteral("QuickShot is already running");
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

    // ============================================================
    // 单实例互斥：第二个实例提示后立即退出
    //
    // 无防护时双开的后果：全局热键注册静默失败（先到者已占用）形成
    // "半死"托盘实例、剪贴板历史被两个 ClipboardMonitor 重复记录、
    // 配置 INI 与 SQLite 历史库双写竞争。
    // 锁文件放按用户隔离的应用数据目录（非共享 Temp），避免 Linux
    // 多用户场景下互相误拦；崩溃残留的死锁由 QLockFile 按 PID 存活
    // 自动清理（setStaleLockTime(0)=不按文件年龄抢占活实例的锁）。
    // ============================================================
    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(lockDir);
    QLockFile instanceLock(lockDir + QStringLiteral("/QuickShot.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        if (instanceLock.error() == QLockFile::LockFailedError) {
            LOG_WARNING("Another QuickShot instance is running, exiting");
            QMessageBox box(QMessageBox::Warning, QStringLiteral("QuickShot"),
                            alreadyRunningText(), QMessageBox::Ok);
            box.exec();
            return 0;
        }
        // 锁文件无法创建（只读目录等）：降级为无单实例防护继续运行，
        // 不因基础设施故障阻止应用启动
        LOG_WARNING(QString("Single-instance lock unavailable (error=%1), "
                            "continuing without instance guard").arg(instanceLock.error()));
    } else {
        LOG_INFO("Single-instance lock acquired");
    }

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

    // OCR 引擎主线程预热：确保单例对象在线程池首次识别前就绑定主线程
    // （OcrAsync 经 QtConcurrent 执行，若不预热，单例会在 worker 线程上构造）
    OcrEngine::instance();

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

    // 创建设置窗口（懒加载：首次从托盘打开时才构造，缩短启动路径。
    // TrayMenuBuilder 对 settingsWindow 仅存指针不使用，传 nullptr 即可；
    // 首次构造时补全全部信号接线。工具栏调节 → 设置页的同步在创建前
    // 不存在（创建时 loadSettings 会从配置加载当前值），无行为差异）
    SettingsWindow *settingsWindow = nullptr;
    auto ensureSettingsWindow = [&]() -> SettingsWindow* {
        if (settingsWindow) {
            return settingsWindow;
        }
        settingsWindow = new SettingsWindow();

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

        // 同步刷新所有存活的贴图窗口（取代 PinWindow 构造里失效的 findChild 接线）
        QObject::connect(settingsWindow, &SettingsWindow::defaultPenWidthChanged,
                         &PinWindow::refreshAllToolDefaults);
        QObject::connect(settingsWindow, &SettingsWindow::defaultFontSizeChanged,
                         &PinWindow::refreshAllToolDefaults);
        QObject::connect(settingsWindow, &SettingsWindow::defaultEraserWidthChanged,
                         &PinWindow::refreshAllToolDefaults);
        QObject::connect(settingsWindow, &SettingsWindow::defaultMosaicSizeChanged,
                         &PinWindow::refreshAllToolDefaults);

        // 工具栏信号连接到 SettingsWindow（工具栏调节时同步显示到设置页面）
        snipScreen->connectToolBarToSettingsWindow(settingsWindow);

        // 快捷键设置变更时实时更新（委托 ShortcutManager 统一处理）
        // SettingsWindow 发出 (QString, QKeySequence) 信号 →
        // ShortcutManager::updateFromUiString 适配为枚举版 update →
        // 内部 emit shortcutChanged(ShortcutType,...) →
        // TrayMenuBuilder::refresh 自动刷新托盘菜单显示值
        QObject::connect(settingsWindow, &SettingsWindow::shortcutChanged,
                         ShortcutManager::instance(), &ShortcutManager::updateFromUiString);

        return settingsWindow;
    };

    // 委托 ShortcutManager 构建托盘菜单的快捷键 action 部分
    // 内部按数据表顺序创建 9 个 action（历史→截图→录屏→贴图→全屏→活动窗口→
    // 录屏暂停→录屏停止→TogglePins），并在首尾插入分隔线
    // 相比原 main.cpp 补全了录屏暂停/停止/TogglePins 三个菜单项
    ShortcutManager::instance()->buildTrayMenu(trayMenu, snipScreen,
                                                historyWindow, nullptr);

    // 设置动作（首次点击时构造设置窗口）
    QAction *settingsAction = trayMenu->addAction(tm->get("traySettings"));
    QObject::connect(settingsAction, &QAction::triggered, [&]() {
        SettingsWindow *w = ensureSettingsWindow();
        w->show();
        w->raise();
        w->activateWindow();
    });

    trayMenu->addSeparator();

    // 退出动作
    QAction *quitAction = trayMenu->addAction(tm->get("trayQuit"));
    QObject::connect(quitAction, &QAction::triggered, [&app]() {
        app.quit();
    });


    // 更新托盘静态文本（设置/退出 action 文本 + tooltip）的函数
    // 9 个快捷键 action 的文本刷新由 TrayMenuBuilder::refresh/retranslate 自动处理
    auto updateStaticText = [trayIcon, settingsAction, quitAction, tm]() {
        settingsAction->setText(tm->get("traySettings"));
        quitAction->setText(tm->get("trayQuit"));
        QString sKey = ShortcutManager::instance()->getSequence(ShortcutType::Snip).toString();
        QString rKey = ShortcutManager::instance()->getSequence(ShortcutType::Record).toString();
        QString hKey = ShortcutManager::instance()->getSequence(ShortcutType::History).toString();
        trayIcon->setToolTip(tm->get("runningInBackground", {sKey, rKey, hKey}));
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

    // Windows / macOS：显示启动通知（两平台行为一致，统一处理）
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QString title = tm->get("quickShot", "QuickShot");
        QString snipKey = ShortcutManager::instance()->getSequence(ShortcutType::Snip).toString();
        QString recordKey = ShortcutManager::instance()->getSequence(ShortcutType::Record).toString();
        QString historyKey = ShortcutManager::instance()->getSequence(ShortcutType::History).toString();
        QString message = tm->get("runningInBackground", {snipKey, recordKey, historyKey});
        trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
        LOG_INFO("Startup notification shown");
    }

    LOG_INFO("QuickShot application started successfully");
    return app.exec();
}
