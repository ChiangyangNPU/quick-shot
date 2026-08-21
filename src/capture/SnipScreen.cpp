#include "SnipScreen.h"
#include "PinWindow.h"
#include "AnnotationInteractionHandler.h"
#include "Logger.h"
#include "../core/TranslationManager.h"
#include "../core/StyleManager.h"
#include "../core/ConfigManager.h"
#include "../widgets/SettingsWindow.h"
#include "../utils/Utils.h"
#include "../history/HistoryManager.h"
#include "../shortcut/AnnotationShortcutController.h"
#include "../ocr/OcrEngine.h"
#include "../ocr/OcrResultDialog.h"
#include "../translate/TranslateService.h"
#include "../widgets/TranslateOverlayWindow.h"
#include "../translate/TranslateEngine.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QCheckBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// WDA_EXCLUDEFROMCAPTURE 在 Windows 10 2004+ SDK 中定义，旧 SDK 需手动定义
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

#ifdef Q_OS_MACOS
#include <objc/objc.h>
#include <objc/message.h>

/**
 * @brief 将窗口级别设到菜单栏之上，使窗口能覆盖整个屏幕（含菜单栏区域）
 *
 * macOS 的 Qt::WindowStaysOnTopHint 对应 NSFloatingWindowLevel(3)，
 * 低于 NSMainMenuWindowLevel(24)，系统会把窗口推到菜单栏下方，
 * 导致截图背景整体下移约 25px。
 * 使用 NSPopUpMenuWindowLevel(101) 确保窗口覆盖菜单栏。
 * @param wid QWidget::winId() 返回的 NSView 指针
 * @author chiangyang
 */
static void raiseWindowAboveMenuBar(WId wid) {
    if (!wid) return;
    id nsView = reinterpret_cast<id>(wid);
    // [nsView window]
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    // [nsWindow setLevel:NSPopUpMenuWindowLevel]
    ((void (*)(id, SEL, long))objc_msgSend)(nsWindow, sel_registerName("setLevel:"), 101);
}
#endif

// ============================================================
// 构造函数
// ============================================================

SnipScreen::SnipScreen(QWidget *parent)
    : QWidget(parent)
{
#ifdef Q_OS_MACOS
    // macOS Sidecar/AirPlay 副屏不支持跨多屏的透明窗口：
    // 单屏窗口 + 不透明背景，
    // paintEvent 直接 drawPixmap 截图覆盖整个 widget，不依赖窗口透明。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#else
    // 无边框、穿透窗口管理器、置顶
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint);

    // 背景透明，让截图作为背景绘制
    setAttribute(Qt::WA_TranslucentBackground);
#endif

    // 创建选区组件，注入 Hunter 检测器
    m_selector = new Selector(&m_hunter, this);
    m_selector->installEventFilter(this);  // 安装事件过滤器，拦截鼠标事件

    // 创建截图工具栏
    m_toolbar = new ScreenshotToolBar(this);
    m_toolbar->hide();

    // 创建录屏工具栏
    m_recordingToolbar = new RecordingToolBar(this);
    m_recordingToolbar->hide();

    // 创建录屏控制栏
    m_recordingControl = new RecordingControlWindow(this);
    m_recordingControl->hide();

    // 创建录屏器
    m_screenRecorder = new ScreenRecorder(this);

    // 创建标注交互处理器，注入 SnipScreen 特有的 Host 回调
    m_annotationHandler = std::make_unique<AnnotationInteractionHandler>();
    AnnotationInteractionHandler::Host host;
    host.clampPos = [this](const QPoint &p) { return clampToSelection(p); };
    host.isInSelection = [this](const QPoint &p) { return isMouseInSelection(p); };
    host.isInToolBar = [this](const QPoint &p) { return isMouseInToolBar(p); };
    host.selectionRect = [this]() { return m_selector->selected(); };
    host.requestUpdate = [this]() { update(); };
    host.syncOverlay = [this]() { pushAnnotationOverlay(); };
    host.updateToolBarState = [this](bool u, bool r) { updateToolBarState(u, r); };
    host.createTextEdit = [this](const QPoint &globalPos) {
        // 文本工具：创建 OverlayTextEdit（全局坐标 → 窗口本地坐标）
        m_textEdit = new OverlayTextEdit(this);
        QPoint localPos = globalPos - m_virtualGeometry.topLeft();
        m_textEdit->setGeometry(QRect(localPos, QSize(200, 50)));
        m_textEdit->setEditorColor(m_annotationHandler->color());
        m_textEdit->setFontSize(m_annotationHandler->fontSize());
        // 设置选区边界，防止文本框拖出选区
        QRect selection = m_selector->selected();
        QRect boundary = selection.translated(-m_virtualGeometry.topLeft());
        m_textEdit->setBoundaryRect(boundary);
        connect(m_textEdit, &OverlayTextEdit::closeRequested,
                this, &SnipScreen::finalizeTextEdit);
        m_textEdit->show();
        m_textEdit->setFocus();
        // 文本编辑框获得焦点：禁用与文本输入冲突的标注快捷键
        m_annotationController->setBareKeysEnabled(false);
    };
    host.finalizeTextEdit = [this]() { finalizeTextEdit(); };
    host.activeTextEdit = [this]() { return m_textEdit; };
    host.setCursor = [this](Qt::CursorShape s) { m_selector->setCursor(s); };
    host.setBareKeysEnabled = [this](bool e) { if (m_annotationController) m_annotationController->setBareKeysEnabled(e); };
    m_annotationHandler->setHost(std::move(host));

#ifdef Q_OS_MACOS
    // macOS 单屏窗口模式：轮询鼠标位置，跨屏时自动切换窗口到鼠标所在屏
    m_screenWatchTimer = new QTimer(this);
    m_screenWatchTimer->setInterval(30);
    connect(m_screenWatchTimer, &QTimer::timeout, this, &SnipScreen::checkScreenSwitch);
#endif

    // 语言切换时更新工具栏文字
    connect(TranslationManager::instance(), &TranslationManager::languageChanged,
            [this](const QString &) {
        m_toolbar->retranslateUi();
        m_recordingToolbar->retranslateUi();
    });

    // 录制控制信号连接
    connect(m_recordingControl, &RecordingControlWindow::startRequested, [this]() {
        QRect selection = m_selector->selected();
        // 减去边框宽度，确保边框不被录入视频
        int borderWidth = StyleManager::SNIP_BORDER_WIDTH;
        QRect captureRect = selection.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
        // 使用实际捕获大小作为输出分辨率
        QSize resolution = captureRect.size();

        // 生成默认保存路径
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            + "/QuickShot_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss") + ".mp4";

        bool ok = m_screenRecorder->start(captureRect, resolution, defaultPath, 30);
        if (ok) {
            m_recordingControl->setRecording(true);
            m_recordingControl->setPaused(false);
            // 录制中锁定选区，禁止移动/调整
            m_selector->setStatus(SelectorStatus::Locked);
            // 更新信息标签并显示录制时间
            m_selector->updateInfoLabel();
            m_selector->showTimerLabel();
            m_recordingToolbar->startTimer();
#ifdef Q_OS_WIN
            // 将本窗口排除出录屏捕获：BitBlt(CAPTUREBLT) 会捕获到 SnipScreen 已绘制的标注，
            // 若不排除，再叠加 overlay 会导致视频中出现重复标注。排除后标注仅由 overlay 合成。
            if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE)) {
                LOG_WARNING("[SnipScreen] SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed, "
                            "recorded video may contain duplicate annotations");
            }
#endif
            pushAnnotationOverlay();
            updateInputMask();
            // 录屏开始后禁用录屏工具栏上的截图按钮
            m_recordingToolbar->setScreenshotButtonEnabled(false);
            LOG_INFO(QString("[SnipScreen] Recording started: %1").arg(defaultPath));
        }
    });
    connect(m_recordingControl, &RecordingControlWindow::pauseRequested, [this]() {
        m_screenRecorder->pause();
        m_recordingControl->setPaused(true);
        m_recordingToolbar->pauseTimer();
    });
    connect(m_recordingControl, &RecordingControlWindow::resumeRequested, [this]() {
        m_screenRecorder->resume();
        m_recordingControl->setPaused(false);
        m_recordingToolbar->resumeTimer();
    });
    connect(m_recordingControl, &RecordingControlWindow::stopRequested, [this]() {
        m_recordingToolbar->stopTimer();
        m_recordingToolbar->resetRecordBtn();
        m_selector->hideTimerLabel();
        m_recordingControl->setRecording(false);
        m_recordingControl->setPaused(false);
        m_recordingControl->hide();
        m_screenRecorder->stop();
        // 不在此处 exit()，等待 ScreenRecorder::stopped 信号（Finalize 完成后）
    });
    connect(m_recordingControl, &RecordingControlWindow::systemAudioToggled, [this](bool enabled) {
        m_screenRecorder->setAudioEnabled(enabled);
    });
    connect(m_recordingControl, &RecordingControlWindow::microphoneToggled, [this](bool enabled) {
        m_screenRecorder->setMicrophoneEnabled(enabled);
    });
    connect(m_screenRecorder, &ScreenRecorder::stopped, [this](const QString &outputFilePath) {
#ifdef Q_OS_WIN
        // 录制结束，恢复窗口捕获属性
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_NONE);
#endif
        // 录制结束（正常停止或取消），重新启用截图按钮
        m_recordingToolbar->setScreenshotButtonEnabled(true);
        if (m_isRecordingCanceled) {
            LOG_INFO(QString("[SnipScreen] Recording canceled, output file: %1").arg(outputFilePath));
            // 录制被取消，不退出截图，重置取消标志
            m_isRecordingCanceled = false;
        } else {
            LOG_INFO(QString("[SnipScreen] Recording saved: %1").arg(outputFilePath));
            exit();
        }
    });
    connect(m_screenRecorder, &ScreenRecorder::errorOccurred, [this](const QString &message) {
#ifdef Q_OS_WIN
        // 录制出错，恢复窗口捕获属性
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_NONE);
#endif
        LOG_INFO(QString("[SnipScreen] Recording error: %1").arg(message));
        m_recordingControl->setRecording(false);
        m_recordingControl->setPaused(false);
        // 录制出错，重新启用截图按钮
        m_recordingToolbar->setScreenshotButtonEnabled(true);
    });

    // 选区信号连接
    // 选区开始时隐藏工具栏，重置标注状态
    connect(m_selector, &Selector::selecting, [this]() {
        hideToolBar();
        m_annotationHandler->manager().clear();
        m_annotationHandler->exitAnnotation();
        if (m_recordingToolbar) m_recordingToolbar->uncheckAllAnnotationBtns();
    });

    // 选区完成时显示工具栏
    connect(m_selector, &Selector::captured, [this]() {
        showToolBar();
    });

    // 选区移动/调整时更新工具栏位置
    connect(m_selector, &Selector::moved, this, &SnipScreen::updateMenuPosition);
    connect(m_selector, &Selector::resized, this, &SnipScreen::updateMenuPosition);

    // 用户右键取消选区 → 退出截图
    connect(m_selector, &Selector::stopped, this, &SnipScreen::exit);

    // 工具栏标注工具信号连接
    connect(m_toolbar, &ScreenshotToolBar::toolSelected, [this](int toolId) {
        // ToolId 和 AnnotationType 从 0 开始，直接映射
        m_annotationHandler->setTool(static_cast<AnnotationType>(toolId));
        m_annotationToolSelected = true;
        m_selector->setAnnotationMode(true);
        updateInputMask();
        LOG_INFO(QString("[SnipScreen] Annotation tool selected: ToolId=%1, AnnotationType=%2")
            .arg(toolId).arg(static_cast<int>(m_annotationHandler->tool())));

        // 标注工具选择后，子工具栏显示完成，重新定位工具栏
        QTimer::singleShot(0, this, [this]() {
            QRect selection = m_selector->selected();
            QWidget *subToolbar = m_toolbar->getSubToolbarWindow();
            calculateToolbarPositions(m_toolbar, subToolbar, nullptr, selection, true);
        });
    });
    connect(m_toolbar, &ScreenshotToolBar::annotationToolDeselected, [this]() {
        m_annotationToolSelected = false;
        m_selector->setAnnotationMode(false);
        updateInputMask();
        LOG_INFO("[SnipScreen] Annotation tool deselected");
    });

    // 工具栏操作信号连接
    connect(m_toolbar, &ScreenshotToolBar::copyRequested, this, &SnipScreen::copy);
    connect(m_toolbar, &ScreenshotToolBar::saveRequested, this, &SnipScreen::save);
    connect(m_toolbar, &ScreenshotToolBar::closeRequested, this, &SnipScreen::exit);
    connect(m_toolbar, &ScreenshotToolBar::pinRequested, this, &SnipScreen::pin);

    // 标注操作信号连接
    connect(m_toolbar, &ScreenshotToolBar::undoRequested, this, &SnipScreen::undoAnnotation);
    connect(m_toolbar, &ScreenshotToolBar::redoRequested, this, &SnipScreen::redoAnnotation);
    connect(m_toolbar, &ScreenshotToolBar::clearRequested, this, &SnipScreen::clearAnnotations);
    connect(m_toolbar, &ScreenshotToolBar::shapeTypeChanged, [this](int type) {
        m_annotationHandler->setShapeType(type);
        LOG_INFO(QString("[SnipScreen] Shape type changed: %1").arg(type));
    });

    // 截图工具栏的录屏按钮 → 切换到录屏模式
    connect(m_toolbar, &ScreenshotToolBar::recordRequested, this, &SnipScreen::switchToRecordingMode);

    // 截图工具栏 OCR 按钮
    connect(m_toolbar, &ScreenshotToolBar::ocrRequested, [this]() {
        performOcr(captureSelectionForOcr());
    });

    // 截图工具栏翻译按钮
    connect(m_toolbar, &ScreenshotToolBar::translateRequested, [this]() {
        performTranslate();
    });

    // 录屏工具栏信号连接
    connect(m_recordingToolbar, &RecordingToolBar::toolSelected, [this](int toolId) {
        m_annotationHandler->setTool(static_cast<AnnotationType>(toolId));
        m_annotationToolSelected = true;
        m_selector->setAnnotationMode(true);
        updateInputMask();
        LOG_INFO(QString("[SnipScreen] Recording: Annotation tool selected: ToolId=%1").arg(toolId));
    });
    connect(m_recordingToolbar, &RecordingToolBar::annotationToolDeselected, [this]() {
        m_annotationToolSelected = false;
        m_selector->setAnnotationMode(false);
        // 子工具栏隐藏后，重新定位控制栏
        if (m_recordingControl && m_recordingControl->isVisible()) {
            // 使用统一位置计算方法
            QRect selection = m_selector->selected();
            QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
            calculateToolbarPositions(m_recordingToolbar, subToolbar, m_recordingControl, selection, false);
        }
        updateInputMask();
        LOG_INFO("[SnipScreen] Recording: Annotation tool deselected");
    });
    connect(m_recordingToolbar, &RecordingToolBar::screenshotRequested, this, &SnipScreen::switchToScreenshotMode);
    connect(m_recordingToolbar, &RecordingToolBar::cancelRecordRequested, [this]() {
        // 如果正在录制，取消录制（删除视频文件）
        if (m_screenRecorder && m_screenRecorder->isRecording()) {
            LOG_INFO("[SnipScreen] Cancel button pressed during recording, canceling...");
            // 设置取消标志，stopped 信号处理时不退出截图
            m_isRecordingCanceled = true;
            // 重置录制状态和UI
            m_recordingToolbar->stopTimer();
            m_recordingToolbar->resetRecordBtn();
            m_selector->hideTimerLabel();
            m_recordingControl->setRecording(false);
            m_recordingControl->setPaused(false);
            m_recordingControl->hide();
            clearMask();
            m_screenRecorder->cancel();
        } else {
            exit();
        }
    });
    connect(m_recordingToolbar, &RecordingToolBar::showControlRequested, [this](bool show) {
        if (show) {
            m_recordingControl->updateButtonStyles();
            m_recordingControl->show();
            m_recordingControl->raise();

            // 使用统一位置计算方法
            QRect selection = m_selector->selected();
            QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
            calculateToolbarPositions(m_recordingToolbar, subToolbar, m_recordingControl, selection, false);

            updateInputMask();
        } else {
            m_recordingControl->hide();
            updateInputMask();
        }
    });
    connect(m_recordingToolbar, &RecordingToolBar::snapshotRequested, [this]() {
        // 快照：复制当前选区画面（含标注）到剪贴板，不影响录屏状态
        QPixmap pixmap = captureSelectionForOcr();
        if (!pixmap.isNull()) {
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setPixmap(pixmap);

            // 记录录屏快照到历史（供 Alt+P 翻页）
            if (HistoryManager::instance()->isScreenshotEnabled()) {
                HistoryManager::instance()->addScreenshotPixmap(
                    pixmap, QApplication::applicationName());
            }

            LOG_INFO("[SnipScreen] Snapshot copied to clipboard");
        }
    });
    connect(m_recordingToolbar, &RecordingToolBar::undoRequested, this, &SnipScreen::undoAnnotation);
    connect(m_recordingToolbar, &RecordingToolBar::redoRequested, this, &SnipScreen::redoAnnotation);
    connect(m_recordingToolbar, &RecordingToolBar::clearRequested, this, &SnipScreen::clearAnnotations);
    connect(m_recordingToolbar, &RecordingToolBar::shapeTypeChanged, [this](int type) {
        m_annotationHandler->setShapeType(type);
        LOG_INFO(QString("[SnipScreen] Recording: Shape type changed: %1").arg(type));
    });

    // 录屏工具栏 OCR 按钮
    connect(m_recordingToolbar, &RecordingToolBar::ocrRequested, [this]() {
        performOcr(captureSelectionForOcr());
    });

    // 录屏工具栏翻译按钮
    connect(m_recordingToolbar, &RecordingToolBar::translateRequested, [this]() {
        performTranslate();
    });

    // 录屏工具栏画笔样式信号
    connect(m_recordingToolbar, &RecordingToolBar::penColorChanged, [this](const QColor &color) {
        m_annotationHandler->setColor(color);
    });
    // 标注工具选择后，子工具栏显示完成，重新定位工具栏
    connect(m_recordingToolbar, &RecordingToolBar::toolSelected, [this](int) {
        QTimer::singleShot(0, this, [this]() {
            // 使用统一位置计算方法
            QRect selection = m_selector->selected();
            QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
            calculateToolbarPositions(m_recordingToolbar, subToolbar, m_recordingControl, selection, false);
        });
    });
    connect(m_recordingToolbar, &RecordingToolBar::penWidthChanged, [this](int width) {
        m_annotationHandler->setPenWidth(width);
    });
    connect(m_recordingToolbar, &RecordingToolBar::fontSizeChanged, [this](int size) {
        m_annotationHandler->setFontSize(size);
    });

    connect(m_recordingToolbar, &RecordingToolBar::timerUpdated, [this](const QString &text) {
        m_selector->updateTimerText(text);
        updateInputMask();
    });

    // 画笔样式信号连接
    connect(m_toolbar, &ScreenshotToolBar::penColorChanged, [this](const QColor &color) {
        m_annotationHandler->setColor(color);
        LOG_INFO(QString("[SnipScreen] Pen color changed: %1").arg(color.name()));
    });
    connect(m_toolbar, &ScreenshotToolBar::penWidthChanged, [this](int width) {
        m_annotationHandler->setPenWidth(width);
        LOG_INFO(QString("[SnipScreen] Pen width changed: %1").arg(width));
    });
    connect(m_toolbar, &ScreenshotToolBar::fontSizeChanged, [this](int size) {
        m_annotationHandler->setFontSize(size);
        LOG_INFO(QString("[SnipScreen] Font size changed: %1").arg(size));
    });

    // 创建标注快捷键控制器（策略模式：SnipScreen 作为 IShortcutHandler 实现）
    // 内部统一用 QShortcut 注册全部标注快捷键，并处理文本编辑冲突
    m_annotationController = new AnnotationShortcutController(this, this);

    // Enter/Return：复制并退出（截图模式专属，不属于通用标注快捷键集合，PinWindow 不需要）
    connect(new QShortcut(Qt::Key_Return, this), &QShortcut::activated, [this]() {
        if (m_selector->status() == SelectorStatus::Captured) {
            copy();
        }
    });
    connect(new QShortcut(Qt::Key_Enter, this), &QShortcut::activated, [this]() {
        if (m_selector->status() == SelectorStatus::Captured) {
            copy();
        }
    });
}

// ============================================================
// 开始截图
// ============================================================

/**
 * @brief 开始截图模式
 * @author chiangyang
 */
void SnipScreen::start() {
    startCapture(false);
}

/**
 * @brief 开始录屏模式
 * @author chiangyang
 */
void SnipScreen::startRecording() {
    startCapture(true);
}

/**
 * @brief 启动截图或录屏的核心流程
 * @param recording true 为录屏模式，false 为截图模式
 * @author chiangyang
 */
void SnipScreen::startCapture(bool recording) {
    if (isVisible())
        return;

    // 设置模式
    m_isRecordingMode = recording;

    // 清理标注状态
    m_annotationHandler->manager().clear();
    m_annotationHandler->exitAnnotation();
    m_annotationHandler->setShapeType(1);
    m_annotationToolSelected = false;
    m_selector->setAnnotationMode(false);
    m_toolbar->uncheckAllAnnotationBtns();
    m_recordingToolbar->uncheckAllAnnotationBtns();

#ifdef Q_OS_MACOS
    // macOS Sidecar/AirPlay 副屏不支持跨多屏的透明窗口：
    // 窗口和截图都限制在鼠标所在的当前屏。
    QScreen *currentScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!currentScreen) {
        // 鼠标在屏幕边缘时 screenAt 可能返回 nullptr，回退到主屏
        currentScreen = QGuiApplication::primaryScreen();
    }
    m_virtualGeometry = currentScreen->geometry();

    // 先抓取背景并修正 m_virtualGeometry（含菜单栏），再据此定位窗口
    grabVirtualDesktop();

    LOG_INFO(QString("[SnipScreen] %1 mode (macOS single-screen). Screen: %2, Geometry: %3,%4 %5x%6")
        .arg(recording ? "Recording" : "Screenshot")
        .arg(currentScreen->name())
        .arg(m_virtualGeometry.x()).arg(m_virtualGeometry.y())
        .arg(m_virtualGeometry.width()).arg(m_virtualGeometry.height()));

    // 单屏窗口：move + resize（不用 setGeometry 跨多屏）
    move(m_virtualGeometry.topLeft());
    resize(m_virtualGeometry.size());
#else
    // 获取虚拟桌面几何（物理像素坐标，因为禁用了高 DPI 缩放）
    m_virtualGeometry = DisplayInfo::virtualScreenGeometry();

    // 调试输出
    LOG_INFO(QString("[SnipScreen] %1 mode. Virtual Geometry: %2,%3 %4x%5")
        .arg(recording ? "Recording" : "Screenshot")
        .arg(m_virtualGeometry.x()).arg(m_virtualGeometry.y())
        .arg(m_virtualGeometry.width()).arg(m_virtualGeometry.height()));

    // 设置窗口覆盖整个虚拟桌面（物理像素坐标）
    setGeometry(m_virtualGeometry);
#endif

    // 设置 Selector 覆盖整个窗口区域（相对坐标从 0,0 开始）
    m_selector->setGeometry(0, 0, m_virtualGeometry.width(), m_virtualGeometry.height());

    // 设置选区坐标系：选区使用物理像素坐标
    m_selector->setCoordinate(m_virtualGeometry);

    // 根据模式设置边框颜色
    updateBorderColor();

#ifndef Q_OS_MACOS
    // 抓取整个虚拟桌面的截图（macOS 已在上面调用过）
    grabVirtualDesktop();
#endif

    // 启动选区交互
    m_selector->start();
    m_selector->show();

    // 显示全屏遮罩
    show();
    raise();
    activateWindow();

#ifdef Q_OS_MACOS
    // 将窗口级别设到菜单栏之上，确保窗口覆盖整个屏幕（含菜单栏区域）
    raiseWindowAboveMenuBar(winId());
    // 启动跨屏检测定时器（PreySelecting 状态下鼠标移到另一屏时自动切换窗口）
    m_screenWatchTimer->start();
#endif
}

// ============================================================
// 退出截图
// ============================================================

/**
 * @brief 退出截图/录屏，清理所有状态
 * @author chiangyang
 */
void SnipScreen::exit() {
#ifdef Q_OS_MACOS
    // 停止跨屏检测定时器
    m_screenWatchTimer->stop();
#endif

    // 清理文本编辑框
    if (m_textEdit) {
        m_textEdit->hide();
        m_textEdit->deleteLater();
        m_textEdit = nullptr;
        // 文本编辑框关闭：恢复标注快捷键
        m_annotationController->setBareKeysEnabled(true);
    }

    // 隐藏工具栏
    if (m_toolbar) m_toolbar->uncheckAllAnnotationBtns();
    if (m_recordingToolbar) m_recordingToolbar->uncheckAllAnnotationBtns();
    hideToolBar();

    // 清理标注
    m_annotationHandler->manager().clear();
    m_annotationHandler->exitAnnotation();
    m_annotationToolSelected = false;
    m_isRecordingMode = false;
    unsetCursor();

    // 关闭选区
    m_selector->close();

    // 清理背景
    m_background = QPixmap();
    m_annotatedBackground = QPixmap();

    // 隐藏窗口
    QWidget::close();
}

// ============================================================
// 复制到剪贴板
// ============================================================

/**
 * @brief 复制选区截图到剪贴板
 * @author chiangyang
 */
void SnipScreen::copy() {
    auto [pixmap, _] = snip();

    // 将图片放入剪贴板
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setPixmap(pixmap);

    // 记录截图到历史（复制操作时将 pixmap 保存到缓存目录）
    if (HistoryManager::instance()->isScreenshotEnabled() && !pixmap.isNull()) {
        QString windowTitle = this->windowTitle().isEmpty()
                                  ? QApplication::applicationName()
                                  : this->windowTitle();
        HistoryManager::instance()->addScreenshotPixmap(pixmap, windowTitle);
    }

    exit();
}

// ============================================================
// 保存到文件
// ============================================================

/**
 * @brief 保存选区截图到文件
 * @author chiangyang
 */
void SnipScreen::save() {
    // 生成默认文件名：QuickShot_yyyy-MM-dd_hhmmss_zzz.png
    QString defaultName =
        "QuickShot_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss_zzz") + ".png";

    // 使用懒加载：对话框确认后才执行 snip()，避免提前隐藏选区框
    QString filename = Utils::savePixmapToFile(
        this, [this]() -> QPixmap {
            auto [pixmap, _] = snip();
            return pixmap;
        }, defaultName, tr("Save Image"),
        QStringLiteral("PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)"));

    if (!filename.isEmpty()) {
        emit saved(filename);

        // 记录截图到历史
        if (HistoryManager::instance()->isScreenshotEnabled()) {
            auto [pixmap, _] = snip();
            QSize imageSize = pixmap.isNull() ? QSize(0, 0) : pixmap.size();
            QString windowTitle = this->windowTitle().isEmpty()
                                      ? QApplication::applicationName()
                                      : this->windowTitle();
            HistoryManager::instance()->addScreenshot(filename, windowTitle, imageSize);
        }
    }

    exit();
}

// ============================================================
// 贴图截图（创建贴图窗口）
// ============================================================

/**
 * @brief 贴图选区截图，创建贴图窗口
 *
 * 同时将截图写入系统剪贴板，使其可被"贴图剪贴板"(Alt+P) 复用。
 * @author chiangyang
 */
void SnipScreen::pin() {
    auto [pixmap, pos] = snip();

    // 检查截图是否有效
    if (pixmap.isNull() || pixmap.width() < 4 || pixmap.height() < 4) {
        LOG_WARNING(QString("[SnipScreen] pin: invalid pixmap, size=%1x%2, ignored")
            .arg(pixmap.width()).arg(pixmap.height()));
        return;
    }

    // 获取虚拟桌面几何，用于限制窗口位置
    QRect virtualGeo = QGuiApplication::primaryScreen()->virtualGeometry();
    
    // 确保位置在有效范围内
    QPoint safePos = pos;
    if (safePos.x() < virtualGeo.left()) safePos.setX(virtualGeo.left());
    if (safePos.y() < virtualGeo.top()) safePos.setY(virtualGeo.top());
    
    // 确保窗口右/下边界不超出虚拟桌面
    int maxX = virtualGeo.right() - pixmap.width();
    int maxY = virtualGeo.bottom() - pixmap.height();
    if (safePos.x() > maxX && maxX >= virtualGeo.left()) safePos.setX(maxX);
    if (safePos.y() > maxY && maxY >= virtualGeo.top()) safePos.setY(maxY);

    // 将截图写入剪贴板，便于 Alt+P 重复贴图
    QGuiApplication::clipboard()->setPixmap(pixmap);

    // 记录截图到历史（贴图操作时将 pixmap 保存到缓存目录，供 Alt+P 翻页）
    if (HistoryManager::instance()->isScreenshotEnabled()) {
        QString windowTitle = this->windowTitle().isEmpty()
                                  ? QApplication::applicationName()
                                  : this->windowTitle();
        HistoryManager::instance()->addScreenshotPixmap(pixmap, windowTitle);
    }

    // 创建贴图窗口，延迟设置几何和显示
    PinWindow *pinWindow = new PinWindow(pixmap);
    QRect targetRect(safePos, pixmap.size());
    LOG_INFO(QString("[SnipScreen] pin: creating PinWindow at (%1,%2) size %3x%4")
        .arg(safePos.x()).arg(safePos.y()).arg(pixmap.width()).arg(pixmap.height()));
    pinWindow->setGeometry(targetRect);
    
    // 延迟显示，确保事件循环稳定后再展示
    QTimer::singleShot(0, pinWindow, [pinWindow]() {
        pinWindow->show();
        pinWindow->raise();
    });

    // 发送信号
    emit snipped(pixmap, safePos);

    exit();
}

/**
 * @brief 贴图历史截图（全局热键入口）
 *
 * 从 HistoryManager 的截图记录中按时间倒序依次贴图：
 * - 首次按下（无活跃翻页贴图）：贴最新一张截图，位置在鼠标所在屏幕中央
 * - 后续按下（有活跃翻页贴图）：贴上一张截图，位置在上一张贴图位置偏移 (24,24)
 * - 循环翻页：到最早一张后回到最新
 * - 旧贴图保留不关闭，可多张同时存在
 * 无截图历史时记录日志并返回。
 * @author chiangyang
 */
void SnipScreen::pinClipboard() {
    // 从历史记录获取截图列表（按时间倒序，最新在前）
    const int kMaxHistoryItems = 1000;
    QList<HistoryItem> items = HistoryManager::instance()->getItems(
        HistoryType::Screenshot, 0, kMaxHistoryItems);

    if (items.isEmpty()) {
        LOG_INFO("[SnipScreen] pinClipboard: no screenshot history, ignored");
        return;
    }

    // 判断翻页状态：上一个翻页贴图是否还活着
    bool hasActivePin = (m_lastPinHistoryWindow != nullptr);

    if (hasActivePin) {
        // 有活跃贴图，索引往前移一张（循环到最早后回到最新）
        m_pinHistoryIndex = (m_pinHistoryIndex + 1) % items.size();
        // 位置偏移，避免与上一张贴图完全重叠
        m_lastPinPos += QPoint(24, 24);
    } else {
        // 无活跃贴图，从最新一张开始
        m_pinHistoryIndex = 0;
        // 位置 = 鼠标所在屏幕中央（用历史记录中的 imageSize 预估位置）
        QSize imgSize = items[0].imageSize;
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        QRect sg = scr->geometry();
        m_lastPinPos = QCursor::pos() - QPoint(imgSize.width() / 2, imgSize.height() / 2);
        int minX = sg.left();
        int maxX = qMax(minX, sg.right() - imgSize.width());
        int minY = sg.top();
        int maxY = qMax(minY, sg.bottom() - imgSize.height());
        m_lastPinPos.setX(qBound(minX, m_lastPinPos.x(), maxX));
        m_lastPinPos.setY(qBound(minY, m_lastPinPos.y(), maxY));
    }

    // 加载对应索引的截图文件
    const HistoryItem &item = items[m_pinHistoryIndex];
    QPixmap pix(item.content);
    if (pix.isNull()) {
        LOG_INFO(QString("[SnipScreen] pinClipboard: failed to load image: %1").arg(item.content));
        return;
    }

    // 位置 clamp 到屏幕范围内（偏移后可能超出）
    QScreen *scr = QGuiApplication::screenAt(m_lastPinPos);
    if (!scr) scr = QGuiApplication::primaryScreen();
    QRect sg = scr->geometry();
    QPoint pos = m_lastPinPos;
    int minX2 = sg.left();
    int maxX2 = qMax(minX2, sg.right() - pix.width());
    int minY2 = sg.top();
    int maxY2 = qMax(minY2, sg.bottom() - pix.height());
    pos.setX(qBound(minX2, pos.x(), maxX2));
    pos.setY(qBound(minY2, pos.y(), maxY2));
    m_lastPinPos = pos;

    // 检查截图是否有效
    if (pix.isNull() || pix.width() <= 0 || pix.height() <= 0) {
        LOG_WARNING("[SnipScreen] pinClipboard: invalid pixmap, skipped");
        return;
    }

    // 创建 PinWindow 显示历史截图
    PinWindow *pinWindow = new PinWindow(pix);
    QRect targetRect(pos, pix.size());
    pinWindow->setGeometry(targetRect);
    QTimer::singleShot(0, pinWindow, [pinWindow]() {
        pinWindow->show();
        pinWindow->raise();
    });

    // 更新跟踪状态
    m_lastPinHistoryWindow = pinWindow;
    m_lastPinPos = pos;

    LOG_INFO(QString("[SnipScreen] Pinned history screenshot index=%1/%2 at (%3,%4), file=%5")
        .arg(m_pinHistoryIndex).arg(items.size()).arg(pos.x()).arg(pos.y()).arg(item.content));
}

/**
 * @brief 全屏截图并复制到剪贴板（全局热键入口）
 *
 * 抓取整个虚拟桌面（多屏并集）直接复制到剪贴板，不进入选区交互。
 * @author chiangyang
 */
void SnipScreen::grabFullscreen() {
    QPixmap pix = Utils::grabVirtualDesktopPixmap();
    if (pix.isNull()) {
        LOG_INFO("[SnipScreen] grabFullscreen: grabbed empty pixmap, ignored");
        return;
    }
    QGuiApplication::clipboard()->setPixmap(pix);

    // 记录全屏截图到历史（供 Alt+P 翻页）
    if (HistoryManager::instance()->isScreenshotEnabled()) {
        HistoryManager::instance()->addScreenshotPixmap(pix, QApplication::applicationName());
    }

    LOG_INFO(QString("[SnipScreen] Fullscreen captured %1x%2 copied to clipboard")
        .arg(pix.width()).arg(pix.height()));
}

/**
 * @brief 活动窗口截图并复制到剪贴板（全局热键入口）
 *
 * 抓取前台活动窗口直接复制到剪贴板。
 * @author chiangyang
 */
void SnipScreen::grabActiveWindow() {
    QPixmap pix = Utils::grabActiveWindowPixmap();
    if (pix.isNull()) {
        LOG_INFO("[SnipScreen] grabActiveWindow: grabbed empty pixmap, ignored");
        return;
    }
    QGuiApplication::clipboard()->setPixmap(pix);

    // 记录活动窗口截图到历史（供 Alt+P 翻页）
    if (HistoryManager::instance()->isScreenshotEnabled()) {
        HistoryManager::instance()->addScreenshotPixmap(pix, QApplication::applicationName());
    }

    LOG_INFO(QString("[SnipScreen] Active window captured %1x%2 copied to clipboard")
        .arg(pix.width()).arg(pix.height()));
}

/**
 * @brief 切换录屏暂停/恢复（全局热键入口）
 *
 * 仅在录制中生效，非录制状态为 no-op。
 * @author chiangyang
 */
void SnipScreen::togglePauseRecording() {
    if (!m_screenRecorder || !m_screenRecorder->isRecording()) {
        LOG_INFO("[SnipScreen] togglePauseRecording: not recording, ignored");
        return;
    }
    if (m_screenRecorder->isPaused()) {
        m_screenRecorder->resume();
        m_recordingToolbar->resumeTimer();
        m_recordingControl->setPaused(false);
        LOG_INFO("[SnipScreen] Recording resumed by global hotkey");
    } else {
        m_screenRecorder->pause();
        m_recordingToolbar->pauseTimer();
        m_recordingControl->setPaused(true);
        LOG_INFO("[SnipScreen] Recording paused by global hotkey");
    }
}

/**
 * @brief 停止录屏（全局热键入口）
 *
 * 仅在录制中生效，停止录制并保留视频文件（区别于取消录制）。
 * 非录制状态为 no-op。
 * @author chiangyang
 */
void SnipScreen::stopRecording() {
    if (!m_screenRecorder || !m_screenRecorder->isRecording()) {
        LOG_INFO("[SnipScreen] stopRecording: not recording, ignored");
        return;
    }
    // 走正常停止路径（保留文件），区别于 ESC 的 cancel 路径（删除文件）
    m_isRecordingCanceled = false;
    m_recordingToolbar->stopTimer();
    m_recordingToolbar->resetRecordBtn();
    m_selector->hideTimerLabel();
    m_recordingControl->setRecording(false);
    m_recordingControl->setPaused(false);
    m_recordingControl->hide();
    clearMask();
    m_screenRecorder->stop();
    LOG_INFO("[SnipScreen] Recording stopped by global hotkey");
}

// ============================================================
// 获取选区截图（带标注）
// ============================================================

/**
 * @brief 获取选区截图（带标注合成）
 * @return 截图和左上角坐标的 pair
 * @author chiangyang
 */
std::pair<QPixmap, QPoint> SnipScreen::snip() {
    // 获取选区矩形（全局坐标）
    const QRect rect = m_selector->selected();

    // 关闭选区，停止交互
    m_selector->close();

    // 坐标偏移：将全局坐标转换为背景图片的本地坐标
    QPoint globalOffset = -m_virtualGeometry.topLeft();

    // 创建合成画布（与 paintEvent 相同的离屏渲染流程）
    QPixmap canvas(m_background.size());
    canvas.fill(Qt::transparent);
    QPainter canvasPainter(&canvas);

    // 1. 绘制背景
    if (!m_background.isNull()) {
        canvasPainter.drawPixmap(0, 0, m_background);
    }

    // 2. 绘制标注（转换为本地坐标）
    canvasPainter.save();
    canvasPainter.translate(globalOffset);
    if (m_annotationHandler->manager().hasAnnotations()) {
        m_annotationHandler->manager().draw(canvasPainter);
    }
    canvasPainter.restore();

    // 3. 在合成图上绘制马赛克（同时像素化背景和标注）
    if (m_annotationHandler->manager().hasMosaicStrokes()) {
        m_annotationHandler->manager().drawMosaic(canvasPainter, canvas, AnnotationManager::kDefaultMosaicBlockSize, globalOffset);
    }

    canvasPainter.end();

    // 4. 从合成图中裁剪选区区域（转换为本地坐标）
    QRect localRect = rect.translated(globalOffset);
    return { canvas.copy(localRect), rect.topLeft() };
}

// ============================================================
// OCR 识别
// ============================================================

/**
 * @brief 截取选区画面用于 OCR 识别（包含标注）
 * @return 选区截图
 * @author chiangyang
 */
QPixmap SnipScreen::captureSelectionForOcr() {
    const QRect rect = m_selector->selected();
    if (rect.isEmpty()) return QPixmap();

    QPoint globalOffset = -m_virtualGeometry.topLeft();

    // 合成背景和标注
    QPixmap canvas(m_background.size());
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    if (!m_background.isNull()) p.drawPixmap(0, 0, m_background);
    p.save();
    p.translate(globalOffset);
    if (m_annotationHandler->manager().hasAnnotations()) m_annotationHandler->manager().draw(p);
    p.restore();
    if (m_annotationHandler->manager().hasMosaicStrokes())
        m_annotationHandler->manager().drawMosaic(p, canvas, AnnotationManager::kDefaultMosaicBlockSize, globalOffset);
    p.end();

    QRect localRect = rect.translated(globalOffset);
    return canvas.copy(localRect);
}

// ============================================================
// 标注 overlay 渲染（用于录屏合成）
// ============================================================

QImage SnipScreen::renderAnnotationOverlay() const {
    if (!m_annotationHandler->manager().hasAnnotations() && !m_annotationHandler->manager().hasMosaicStrokes())
        return QImage();

    // 使用与录屏捕获区域一致的矩形（选区减去边框），确保 overlay 坐标与捕获帧对齐，
    // 否则 overlay 标注会相对捕获画面偏移一个边框宽度
    const int overlayBw = StyleManager::SNIP_BORDER_WIDTH;
    const QRect cap = m_selector->selected().adjusted(overlayBw, overlayBw, -overlayBw, -overlayBw);
    if (cap.isEmpty()) return QImage();

    if (!m_screenRecorder) return QImage();
    const QSize outSize = m_screenRecorder->outputSize();
    if (outSize.isEmpty()) return QImage();

    QImage overlay(outSize, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);

    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal scaleX = static_cast<qreal>(outSize.width())  / cap.width();
    qreal scaleY = static_cast<qreal>(outSize.height()) / cap.height();

    // 全局坐标 → 输出坐标
    painter.translate(-cap.x(), -cap.y());
    painter.scale(scaleX, scaleY);

    if (m_annotationHandler->manager().hasAnnotations())
        m_annotationHandler->manager().draw(painter);

    // 马赛克：基于选区背景做真实像素化，叠加在最上层遮挡内容
    if (m_annotationHandler->manager().hasMosaicStrokes() && !m_background.isNull()) {
        painter.save();
        // 取消外层的坐标变换，直接在输出尺寸像素坐标系下操作
        painter.setWorldTransform(QTransform());

        // 1. 从全屏背景裁剪出选区，缩放到录屏输出尺寸
        QPixmap bgSelection = m_background.copy(cap);
        QPixmap bgScaled = bgSelection.scaled(outSize,
                                              Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);

        // 2. 整体马赛克预处理：先 1/5 缩小再放大
        const int mosaicScale = AnnotationManager::kDefaultMosaicBlockSize;
        const QSize smallSize(qMax(1, outSize.width()  / mosaicScale),
                              qMax(1, outSize.height() / mosaicScale));
        QPixmap small = bgScaled.scaled(smallSize,
                                        Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);
        QPixmap mosaicizedFull = small.scaled(outSize,
                                              Qt::IgnoreAspectRatio,
                                              Qt::FastTransformation);

        // 3. 笔迹矩形坐标：全局 → 选区局部 → 输出尺寸像素
        QRegion mosaicRegion;
        const std::vector<QRect> strokes = m_annotationHandler->manager().mosaicStrokes();
        for (const QRect &r : strokes) {
            // 全局 -> 选区局部（cap 左上角为原点）
            QRect local(r.x() - cap.x(), r.y() - cap.y(),
                        r.width(), r.height());
            // 缩放到输出像素
            QRect outRect(qRound(local.x()      * scaleX),
                          qRound(local.y()      * scaleY),
                          qRound(local.width()  * scaleX),
                          qRound(local.height() * scaleY));
            QRect clipped = outRect.intersected(QRect(QPoint(0, 0), outSize));
            if (!clipped.isEmpty())
                mosaicRegion += clipped;
        }

        // 4. 仅在马赛克区域绘制预处理好的全局马赛克图（其他部分保持透明）
        if (!mosaicRegion.isEmpty()) {
            painter.setClipRegion(mosaicRegion);
            painter.drawPixmap(0, 0, mosaicizedFull);
        }

        painter.restore();
    }

    painter.end();
    return overlay.convertToFormat(QImage::Format_ARGB32);
}

void SnipScreen::pushAnnotationOverlay() {
    if (!m_screenRecorder || !m_screenRecorder->isRecording())
        return;
    m_screenRecorder->setAnnotationOverlay(renderAnnotationOverlay());
}

void SnipScreen::updateInputMask() {
    // 仅在录屏中且未选中标注工具时限制输入区域
    bool passthrough = m_isRecordingMode && m_screenRecorder
                       && m_screenRecorder->isRecording()
                       && !m_annotationToolSelected;

    if (!passthrough) {
        clearMask();
        return;
    }

    QRegion mask;
    QPoint localOffset = -m_virtualGeometry.topLeft();

    // 工具栏区域（子控件 geometry() 已是父相对坐标）
    if (m_recordingToolbar && m_recordingToolbar->isVisible()) {
        mask += m_recordingToolbar->geometry();
        QWidget *sub = m_recordingToolbar->getSubToolbarWindow();
        if (sub && sub->isVisible())
            mask += sub->geometry();
    }

    // 录制控制栏区域
    if (m_recordingControl && m_recordingControl->isVisible()) {
        mask += m_recordingControl->geometry();
    }

    // 选区边框（selected() 返回屏幕坐标，需转为本地坐标）
    if (m_selector) {
        QRect sel = m_selector->selected();
        if (sel.isValid() && !sel.isEmpty()) {
            QRect localSel = sel.translated(localOffset);
            const int bw = 3;
            mask += QRegion(localSel).subtracted(localSel.adjusted(bw, bw, -bw, -bw));
        }

        // 信息标签和时间标签区域
        QRegion labelGeo = m_selector->getLabelGeometry();
        if (!labelGeo.isEmpty()) {
            mask += labelGeo;
        }
    }

    setMask(mask);
}

/**
 * @brief 执行 OCR 识别
 * @param pixmap 要识别的截图
 * @author chiangyang
 */
void SnipScreen::performOcr(const QPixmap &pixmap) {
    if (pixmap.isNull()) return;

    TranslationManager *tm = TranslationManager::instance();
    QRect sel = m_selector->selected();

    // 在选区正中创建加载提示标签
    // 提示标签是 SnipScreen 的子控件，move() 使用父相对坐标，需从全局坐标转换
    QRect localSel = sel.translated(-m_virtualGeometry.topLeft());
    auto centerInSelection = [this, localSel](QWidget *w) {
        w->adjustSize();
        int x = localSel.x() + (localSel.width() - w->width()) / 2;
        int y = localSel.y() + (localSel.height() - w->height()) / 2;
        w->move(x, y);
    };

    QLabel *loadingLabel = new QLabel(tm->get("ocr.recognizing"), this);
    loadingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    loadingLabel->setAlignment(Qt::AlignCenter);
    centerInSelection(loadingLabel);
    loadingLabel->show();

    // 异步执行 OCR
    auto *watcher = new QFutureWatcher<OcrEngine::OcrResult>(this);
    connect(watcher, &QFutureWatcher<OcrEngine::OcrResult>::finished, this, [this, watcher, loadingLabel, sel, centerInSelection]() {
        loadingLabel->hide();
        loadingLabel->deleteLater();

        OcrEngine::OcrResult result = watcher->result();
        if (result.texts.isEmpty()) {
            TranslationManager *tm = TranslationManager::instance();
            QLabel *noText = new QLabel(tm->get("ocr.noText"), this);
            noText->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
            noText->setAlignment(Qt::AlignCenter);
            centerInSelection(noText);
            noText->show();
            QTimer::singleShot(2000, noText, &QWidget::deleteLater);
        } else {
            auto *dialog = new OcrResultDialog(result);
            // 将弹框居中在选区（弹框是独立窗口，使用全局坐标）
            dialog->adjustSize();
            int dx = sel.x() + (sel.width() - dialog->width()) / 2;
            int dy = sel.y() + (sel.height() - dialog->height()) / 2;
            dialog->move(dx, dy);
            dialog->show();
        }
        watcher->deleteLater();

        // OCR 识别结束后释放模型资源，下次识别时重新初始化
        OcrEngine::instance()->release();
    });

    QImage image = pixmap.toImage();
    QFuture<OcrEngine::OcrResult> future = QtConcurrent::run([image]() {
        return OcrEngine::instance()->recognize(image);
    });
    watcher->setFuture(future);
}

// ============================================================
// 翻译流程（二期）
// ============================================================

/**
 * @brief 执行翻译流程：截图选区 → OCR 识别 → 批量翻译 → 显示译文叠加窗口
 * @author chiangyang
 */
void SnipScreen::performTranslate() {
    // 1. 截取选区画面（含标注）
    QPixmap pixmap = captureSelectionForOcr();
    if (pixmap.isNull()) {
        LOG_INFO("[SnipScreen] Translate aborted: empty selection");
        return;
    }

    // 2. 检查翻译功能是否启用 + 首次隐私提示（弹窗居中在选区）
    if (!TranslateService::checkEnabledAndPrivacy(this, m_selector->selected())) {
        LOG_INFO("[SnipScreen] Translate aborted: disabled or declined privacy warning");
        return;
    }

    TranslationManager *tm = TranslationManager::instance();
    QRect sel = m_selector->selected();
    QRect localSel = sel.translated(-m_virtualGeometry.topLeft());

    // 居中辅助函数：将控件居中在选区内
    auto centerInSelection = [this, localSel](QWidget *w) {
        w->adjustSize();
        int x = localSel.x() + (localSel.width() - w->width()) / 2;
        int y = localSel.y() + (localSel.height() - w->height()) / 2;
        w->move(x, y);
    };

    // 3. 显示"识别中"加载提示
    QLabel *loadingLabel = new QLabel(tm->get("ocr.recognizing"), this);
    loadingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    loadingLabel->setAlignment(Qt::AlignCenter);
    centerInSelection(loadingLabel);
    loadingLabel->show();

    LOG_INFO("[SnipScreen] Translate requested, starting OCR");

    // 4. 异步执行 OCR 识别
    auto *watcher = new QFutureWatcher<OcrEngine::OcrResult>(this);
    connect(watcher, &QFutureWatcher<OcrEngine::OcrResult>::finished, this,
            [this, watcher, loadingLabel, pixmap, sel, centerInSelection]() {
        loadingLabel->hide();
        loadingLabel->deleteLater();

        OcrEngine::OcrResult result = watcher->result();
        // 释放 OCR 模型资源，下次识别时重新初始化
        OcrEngine::instance()->release();

        if (result.texts.isEmpty()) {
            // 无识别文本
            QLabel *noText = new QLabel(TranslationManager::instance()->get("ocr.noText"), this);
            noText->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
            noText->setAlignment(Qt::AlignCenter);
            centerInSelection(noText);
            noText->show();
            QTimer::singleShot(2000, noText, &QWidget::deleteLater);
            watcher->deleteLater();
            return;
        }

        // 5. 批量翻译并显示译文叠加窗口（封装了标签提示、信号连接、错误处理）
        //    翻译成功显示 Overlay 后退出截图框（类似贴图完成后销毁截图框和工具栏）
        QRect localSel = sel.translated(-m_virtualGeometry.topLeft());
        TranslateOverlayWindow::translateAndShow(
            this, pixmap, result.texts, result.polygons,
            QPoint(sel.x(), sel.y()), localSel,
            [this]() { exit(); });

        LOG_INFO(QString("[SnipScreen] OCR done, %1 segments, starting batch translation")
                     .arg(result.texts.size()));

        watcher->deleteLater();
    });

    QImage image = pixmap.toImage();
    QFuture<OcrEngine::OcrResult> future = QtConcurrent::run([image]() {
        return OcrEngine::instance()->recognize(image);
    });
    watcher->setFuture(future);
}

// ============================================================
// 抓取虚拟桌面截图
// ============================================================

/**
 * @brief 抓取整个虚拟桌面的截图作为背景
 * @author chiangyang
 */
void SnipScreen::grabVirtualDesktop() {
#ifdef Q_OS_MACOS
    // macOS 单屏截图：只抓当前屏（与窗口大小一致）
    QScreen *currentScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!currentScreen) currentScreen = QGuiApplication::primaryScreen();
    m_background = currentScreen->grabWindow(0);
    qreal dpr = currentScreen->devicePixelRatio();
    m_background.setDevicePixelRatio(dpr);

    // macOS: QScreen::geometry() 对主屏可能不含菜单栏（y 从菜单栏高度开始），
    // 但 grabWindow(0) 抓取含菜单栏的整个屏幕。
    // 用背景图逻辑尺寸修正 m_virtualGeometry，确保窗口覆盖整个屏幕。
    QSize fullSize = m_background.size() / dpr;
    QRect g = currentScreen->geometry();
    if (g.height() < fullSize.height()) {
        // geometry 不含菜单栏，向上扩展窗口以包含菜单栏区域
        int yOffset = fullSize.height() - g.height();
        m_virtualGeometry = QRect(g.x(), g.y() - yOffset, fullSize.width(), fullSize.height());
    } else {
        m_virtualGeometry = QRect(g.x(), g.y(), fullSize.width(), fullSize.height());
    }

    LOG_INFO(QString("[SnipScreen] Background (macOS single-screen %1): %2x%3 dpr=%4, windowGeo: %5,%6 %7x%8")
        .arg(currentScreen->name())
        .arg(m_background.width()).arg(m_background.height())
        .arg(dpr)
        .arg(m_virtualGeometry.x()).arg(m_virtualGeometry.y())
        .arg(m_virtualGeometry.width()).arg(m_virtualGeometry.height()));
#else
    // 复用 Utils 的虚拟桌面抓取（与原 Win32 BitBlt 实现行为一致）
    m_background = Utils::grabVirtualDesktopPixmap();
    LOG_INFO(QString("[SnipScreen] Background size: %1x%2").arg(m_background.width()).arg(m_background.height()));
#endif
    setAutoFillBackground(false);
}

#ifdef Q_OS_MACOS
// ============================================================
// 跨屏检测（macOS 单屏窗口模式）
// ============================================================

/**
 * @brief 检测鼠标是否跨屏，若跨屏则将窗口切换到鼠标所在屏
 * @author chiangyang
 */
void SnipScreen::checkScreenSwitch() {
    // 仅在 PreySelecting（自动吸附，未开始拖拽/未完成选区）时切换
    if (m_selector->status() != SelectorStatus::PreySelecting)
        return;

    QPoint cursorPos = QCursor::pos();

    // 鼠标仍在当前窗口范围内，无需切换
    if (m_virtualGeometry.contains(cursorPos))
        return;

    // 鼠标已移到其他屏，获取目标屏
    QScreen *newScreen = QGuiApplication::screenAt(cursorPos);
    if (!newScreen)
        return;

    // 更新窗口几何到新屏
    m_virtualGeometry = newScreen->geometry();
    LOG_INFO(QString("[SnipScreen] Screen switch -> %1, Geometry: %2,%3 %4x%5")
        .arg(newScreen->name())
        .arg(m_virtualGeometry.x()).arg(m_virtualGeometry.y())
        .arg(m_virtualGeometry.width()).arg(m_virtualGeometry.height()));

    // 重新抓取新屏背景（grabVirtualDesktop 内部用 QCursor::pos 取屏）
    grabVirtualDesktop();

    // 移动窗口到新屏
    move(m_virtualGeometry.topLeft());
    resize(m_virtualGeometry.size());

    // 重新设置窗口级别到菜单栏之上（move 可能重置窗口级别）
    raiseWindowAboveMenuBar(winId());

    // 更新 Selector 几何和坐标系，重新吸附到鼠标下的目标
    m_selector->setGeometry(0, 0, m_virtualGeometry.width(), m_virtualGeometry.height());
    m_selector->switchScreen(m_virtualGeometry);

    // 重绘
    update();
}
#endif

// ============================================================
// 绘制
// ============================================================

/**
 * @brief 绘制事件：绘制背景和标注
 * @param event 绘制事件
 * @author chiangyang
 */
void SnipScreen::paintEvent(QPaintEvent *) {
    // 坐标偏移：将全局坐标（虚拟桌面坐标）转换为窗口本地坐标
    QPoint globalOffset = -m_virtualGeometry.topLeft();

    if (m_annotationHandler->manager().hasMosaicStrokes()) {
        // 有马赛克时：先渲染到离屏 pixmap，再对 pixmap 做马赛克处理
        QPixmap canvas(size());
        canvas.fill(Qt::transparent);
        QPainter canvasPainter(&canvas);

        // 1. 绘制背景
        if (!m_background.isNull()) {
            canvasPainter.drawPixmap(0, 0, m_background);
        }

        // 2. 绘制标注 + 马赛克 + 控制点（委托给 handler）
        m_annotationHandler->drawWithMosaic(canvasPainter, canvas,
            AnnotationManager::kDefaultMosaicBlockSize, globalOffset);
        canvasPainter.end();

        // 3. 绘制到屏幕
        QPainter painter(this);
        painter.drawPixmap(0, 0, canvas);
    } else {
        // 无马赛克时：直接绘制
        QPainter painter(this);

        if (!m_background.isNull()) {
            painter.drawPixmap(0, 0, m_background);
        }

        // 绘制标注 + 控制点（委托给 handler）
        m_annotationHandler->drawAnnotations(painter, globalOffset);
    }
}

// ============================================================
// 事件过滤器（拦截 Selector 的鼠标事件）
// ============================================================

/**
 * @brief 事件过滤器，拦截 Selector 的鼠标事件用于标注
 * @param watched 事件来源对象
 * @param event 事件
 * @return true 表示事件已被处理
 * @author chiangyang
 */
bool SnipScreen::eventFilter(QObject *watched, QEvent *event) {
    // 只拦截 Selector 的鼠标事件
    if (watched != m_selector) {
        return QWidget::eventFilter(watched, event);
    }

    // 只在标注工具选中且选区完成(Captured)或录制中(Locked)时拦截
    SelectorStatus s = m_selector->status();
    if (!m_annotationToolSelected || (s != SelectorStatus::Captured && s != SelectorStatus::Locked)) {
        return QWidget::eventFilter(watched, event);
    }

    // 转发给标注交互处理器
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QPoint globalPos = mouseEvent->globalPosition().toPoint();
                return m_annotationHandler->handleMousePress(globalPos, mouseEvent->modifiers());
            }
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            QPoint globalPos = mouseEvent->globalPosition().toPoint();
            return m_annotationHandler->handleMouseMove(globalPos, mouseEvent->modifiers());
        }
        case QEvent::MouseButtonRelease: {
            if (m_annotationHandler->isInteractionActive()) {
                return m_annotationHandler->handleMouseRelease();
            }
            break;
        }
        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}

// ============================================================
// 鼠标事件处理
// ============================================================

/**
 * @brief 鼠标按下事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void SnipScreen::mousePressEvent(QMouseEvent *event) {
    // 标注逻辑由 eventFilter 处理，这里只处理工具栏点击
    QPoint globalPos = event->globalPosition().toPoint();

    if (isMouseInToolBar(globalPos)) {
        QWidget::mousePressEvent(event);
        return;
    }

    // 其他情况不处理（由 Selector 的事件处理器处理）
    QWidget::mousePressEvent(event);
}

/**
 * @brief 鼠标移动事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void SnipScreen::mouseMoveEvent(QMouseEvent *event) {
    // 标注逻辑由 eventFilter 处理，这里只处理工具栏
    QPoint globalPos = event->globalPosition().toPoint();

    if (isMouseInToolBar(globalPos)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void SnipScreen::mouseReleaseEvent(QMouseEvent *event) {
    // 标注逻辑由 eventFilter 处理，这里只处理工具栏
    QPoint globalPos = event->globalPosition().toPoint();

    if (isMouseInToolBar(globalPos)) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 鼠标滚轮事件，转发给 Selector
 * @param event 滚轮事件
 * @author chiangyang
 */
void SnipScreen::wheelEvent(QWheelEvent *event) {
    QApplication::sendEvent(m_selector, event);
}

// ============================================================
// 键盘事件
// ============================================================

/**
 * @brief 键盘按下事件，Shift 键切换十字准线
 * @param event 键盘事件
 * @author chiangyang
 */
void SnipScreen::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        m_selector->showCrossHair(true);
    }
    QWidget::keyPressEvent(event);
}

/**
 * @brief 键盘释放事件
 * @param event 键盘事件
 * @author chiangyang
 */
void SnipScreen::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
        m_selector->showCrossHair(false);
    }
    QWidget::keyReleaseEvent(event);
}

// ============================================================
// 双击：快速复制
// ============================================================

/**
 * @brief 鼠标双击事件，快速复制选区截图
 * @param event 鼠标事件
 * @author chiangyang
 */
void SnipScreen::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_selector->status() == SelectorStatus::Captured) {
        copy();
    }
}

// ============================================================
// IShortcutHandler 接口实现（标注快捷键策略）
// ============================================================
// 原先的 registerShortcuts() / setAnnotationKeysEnabled() / m_annotationKeyShortcuts
// 已删除，标注快捷键统一由 AnnotationShortcutController 通过 QShortcut 注册，
// SnipScreen 仅实现 IShortcutHandler 接口的语义动作。
// Enter/Return（复制并退出）作为截图模式专属快捷键仍保留在构造函数中，
// 因其不属于通用标注快捷键集合（PinWindow 不需要）。

/**
 * @brief 判断当前是否可进行标注操作
 * @return 选区状态为 Captured 或 Locked 时返回 true
 * @author chiangyang
 */
bool SnipScreen::canAnnotate() const {
    return m_selector->status() == SelectorStatus::Captured ||
           m_selector->status() == SelectorStatus::Locked;
}

/**
 * @brief 切换标注工具（数字键 1-8 触发）
 * @param toolId 工具 ID（0-7，对应 AnnotationType 枚举值）
 * @author chiangyang
 */
void SnipScreen::onToolSwitch(int toolId) {
    BaseToolBar *tb = m_isRecordingMode
        ? static_cast<BaseToolBar*>(m_recordingToolbar)
        : static_cast<BaseToolBar*>(m_toolbar);
    if (tb) tb->selectAnnotationTool(toolId);
}

/**
 * @brief 复制到剪贴板（Ctrl+C 触发，复制后退出截图模式）
 * @author chiangyang
 */
void SnipScreen::onCopy() {
    copy();
}

/**
 * @brief 保存到文件（Ctrl+S 触发）
 * @author chiangyang
 */
void SnipScreen::onSave() {
    save();
}

/**
 * @brief 撤销标注（Ctrl+Z 触发）
 * @author chiangyang
 */
void SnipScreen::onUndo() {
    undoAnnotation();
}

/**
 * @brief 重做标注（Ctrl+Y / Ctrl+Shift+Z 触发）
 * @author chiangyang
 */
void SnipScreen::onRedo() {
    redoAnnotation();
}

/**
 * @brief 清除所有标注（Delete / Backspace 触发）
 * @author chiangyang
 */
void SnipScreen::onClear() {
    clearAnnotations();
}

/**
 * @brief 调整画笔宽度（[ / ] 触发，范围 1-20）
 * @param delta 宽度增量（+1 增加，-1 减少）
 * @author chiangyang
 */
void SnipScreen::onPenWidthChange(int delta) {
    BaseToolBar *tb = m_isRecordingMode
        ? static_cast<BaseToolBar*>(m_recordingToolbar)
        : static_cast<BaseToolBar*>(m_toolbar);
    if (!tb) return;
    tb->setCurrentPenWidth(qBound(AnnotationShortcutController::kMinPenWidth,
                                  m_annotationHandler->penWidth() + delta,
                                  AnnotationShortcutController::kMaxPenWidth));
}

/**
 * @brief 循环切换颜色（Tab 触发）
 * @author chiangyang
 */
void SnipScreen::onCycleColor() {
    BaseToolBar *tb = m_isRecordingMode
        ? static_cast<BaseToolBar*>(m_recordingToolbar)
        : static_cast<BaseToolBar*>(m_toolbar);
    if (tb) tb->selectNextColor();
}

/**
 * @brief 取消当前操作（Esc 触发）
 *
 * 优先级：取消文本编辑 → 取消录制 → 退出截图
 * @author chiangyang
 */
void SnipScreen::onCancel() {
    if (m_textEdit) {
        // 取消文本编辑，不创建标注
        m_textEdit->hide();
        m_textEdit->deleteLater();
        m_textEdit = nullptr;
        // 文本编辑框关闭：恢复标注快捷键
        m_annotationController->setBareKeysEnabled(true);
        update();
    } else if (m_screenRecorder && m_screenRecorder->isRecording()) {
        // 如果正在录制，取消录制（删除视频文件）
        LOG_INFO("[SnipScreen] ESC pressed during recording, canceling...");
        // 设置取消标志，stopped 信号处理时不退出截图
        m_isRecordingCanceled = true;
        // 重置录制状态和UI
        m_recordingToolbar->stopTimer();
        m_recordingToolbar->resetRecordBtn();
        m_selector->hideTimerLabel();
        m_recordingControl->setRecording(false);
        m_recordingControl->setPaused(false);
        m_recordingControl->hide();
        clearMask();
        m_screenRecorder->cancel();
    } else {
        exit();
    }
}

/**
 * @brief 刷新截图（F5 触发，重新抓取屏幕保留选区）
 * @author chiangyang
 */
void SnipScreen::onRefresh() {
    if (!isVisible())
        return;

    Prey currentPrey = m_selector->prey();
    SelectorStatus currentStatus = m_selector->status();

    m_selector->hide();
    hide();

    grabVirtualDesktop();

    show();
    raise();
    m_selector->show();
    activateWindow();

    m_selector->select(currentPrey);
    if (currentStatus == SelectorStatus::Captured) {
        m_selector->setStatus(SelectorStatus::Captured);
    }
}

// ============================================================
// 工具栏位置更新
// ============================================================

/**
 * @brief 计算工具栏位置的辅助方法
 * 
 * 根据选区位置和屏幕空间，计算一级工具栏、子工具栏和录屏控制栏的位置。
 * 顺序始终保持：一级工具栏 → 子工具栏 → 录屏控制栏。
 * 
 * @param toolbar 一级工具栏
 * @param subToolbar 子工具栏
 * @param recordingControl 录屏控制栏（可为nullptr）
 * @param selection 选区矩形
 * @param screen 屏幕对象
 * @author chiangyang
 */
void SnipScreen::calculateToolbarPositions(QWidget *toolbar, QWidget *subToolbar,
                                           QWidget *recordingControl, const QRect &selection,
                                           bool alignRight) {
    if (!toolbar) return;

    // 使用虚拟桌面几何作为边界，避免跨屏选区时被单一屏幕限制
    QRect screenGeometry = m_virtualGeometry;
    int toolbarWidth = toolbar->width();
    int toolbarHeight = toolbar->height();
    int subToolbarHeight = subToolbar ? subToolbar->height() : 0;
    int controlHeight = recordingControl ? recordingControl->height() : 0;

    // 子控件 move() 使用父相对坐标，x()/y() 也返回父相对坐标
    // 内部计算使用屏幕坐标，通过以下 helper 在边界处转换
    auto scrMove = [this](QWidget *w, int sx, int sy) {
        w->move(sx - m_virtualGeometry.x(), sy - m_virtualGeometry.y());
    };
    auto scrX = [this](QWidget *w) { return w->x() + m_virtualGeometry.x(); };
    auto scrY = [this](QWidget *w) { return w->y() + m_virtualGeometry.y(); };

    // 计算屏幕边缘与选区的距离
    int spaceBelowSelection = screenGeometry.bottom() - selection.bottom();
    int spaceAboveSelection = selection.top() - screenGeometry.top();

    // 确定一级工具栏的X位置
    int toolbarX;
    if (alignRight) {
        // 截屏模式：右对齐选区右边界
        toolbarX = selection.right() - toolbarWidth;
        if (toolbarX < screenGeometry.left() + 5) {
            toolbarX = screenGeometry.left() + 5;
        }
    } else {
        // 录屏模式：左对齐选区左边界
        toolbarX = selection.left();
    }

    // 确定一级工具栏的Y位置（与 positionNearSelection 逻辑一致）
    int toolbarY;
    bool toolbarAboveSelection;

    if (spaceBelowSelection >= toolbarHeight + 10) {
        toolbarY = selection.bottom() + 5;
        toolbarAboveSelection = false;
    } else if (spaceAboveSelection >= toolbarHeight + 10) {
        toolbarY = selection.top() - toolbarHeight - 5;
        toolbarAboveSelection = true;
    } else {
        toolbarY = selection.bottom() - toolbarHeight - 5;
        toolbarAboveSelection = false;
    }

    // 更新一级工具栏位置
    scrMove(toolbar, toolbarX, toolbarY);

    // 计算子工具栏和录屏控制栏的位置
    if (!toolbarAboveSelection) {
        // 一级工具栏在下方
        // 下方空间足够：子工具栏在一级工具栏下方，控制栏在子工具栏下方
        // 下方空间不足：子工具栏在一级工具栏上方，控制栏在子工具栏上方
        int currentBottom = toolbarY + toolbarHeight;
        int spaceBelow = screenGeometry.bottom() - currentBottom;

        // 子工具栏位置
        if (subToolbar && subToolbar->isVisible()) {
            if (spaceBelow >= subToolbarHeight + 10) {
                // 下方空间足够，子工具栏放在一级工具栏下方
                scrMove(subToolbar, scrX(toolbar), currentBottom);
                currentBottom += subToolbarHeight;
            } else {
                // 下方空间不足，子工具栏放在一级工具栏上方
                int subY = toolbarY - subToolbarHeight - 2;
                scrMove(subToolbar, scrX(toolbar), subY);
            }
        }

        // 录屏控制栏位置
        if (recordingControl && recordingControl->isVisible()) {
            if (subToolbar && subToolbar->isVisible()) {
                bool subAboveMain = (scrY(subToolbar) < toolbarY);
                if (subAboveMain) {
                    // 子工具栏在上方，控制栏放在子工具栏上方
                    int controlY = scrY(subToolbar) - controlHeight - 2;
                    scrMove(recordingControl,toolbarX, controlY);
                } else {
                    // 子工具栏在下方，控制栏放在子工具栏下方
                    int spaceBelowControl = screenGeometry.bottom() - currentBottom;
                    if (spaceBelowControl >= controlHeight + 10) {
                        scrMove(recordingControl,toolbarX, currentBottom + 2);
                    } else {
                        // 下方空间不足，控制栏放在一级工具栏上方（或子工具栏上方）
                        int controlY = toolbarY - controlHeight - 2;
                        if (subToolbar && subToolbar->isVisible() && scrY(subToolbar) < toolbarY) {
                            controlY = scrY(subToolbar) - controlHeight - 2;
                        }
                        scrMove(recordingControl,toolbarX, controlY);
                    }
                }
            } else {
                // 没有子工具栏，控制栏放在一级工具栏下方（空间足够）或上方（空间不足）
                int spaceBelowControl = screenGeometry.bottom() - currentBottom;
                if (spaceBelowControl >= controlHeight + 10) {
                    scrMove(recordingControl,toolbarX, currentBottom + 2);
                } else {
                    scrMove(recordingControl,toolbarX, toolbarY - controlHeight - 2);
                }
            }
        }
    } else {
        // 一级工具栏在上方
        // 参考下方的处理逻辑：
        // 计算工具栏上方到屏幕顶部的距离，如果足够放子工具栏就放上，否则放下
        int spaceAboveToolbar = toolbarY - screenGeometry.top();

        // 一级工具栏位置已在上方设置

        // 子工具栏位置
        if (subToolbar && subToolbar->isVisible()) {
            if (spaceAboveToolbar >= subToolbarHeight + 10) {
                // 上方空间足够，子工具栏放在一级工具栏上方
                int subY = toolbarY - subToolbarHeight - 2;
                scrMove(subToolbar, toolbarX, subY);
            } else {
                // 上方空间不足，子工具栏放在一级工具栏下方
                int subY = toolbarY + toolbarHeight + 2;
                scrMove(subToolbar, toolbarX, subY);
            }
        }

        // 录屏控制栏位置
        if (recordingControl && recordingControl->isVisible()) {
            if (subToolbar && subToolbar->isVisible()) {
                bool subAboveMain = (scrY(subToolbar) < toolbarY);
                if (subAboveMain) {
                    // 子工具栏在上方，控制栏放在子工具栏上方
                    int controlY = scrY(subToolbar) - controlHeight - 2;
                    scrMove(recordingControl,toolbarX, controlY);
                } else {
                    // 子工具栏在下方，控制栏放在子工具栏下方
                    int controlY = scrY(subToolbar) + subToolbarHeight + 2;
                    scrMove(recordingControl,toolbarX, controlY);
                }
            } else {
                // 没有子工具栏，控制栏放在一级工具栏上方（空间足够）或下方（空间不足）
                if (spaceAboveToolbar >= controlHeight + 10) {
                    scrMove(recordingControl,toolbarX, toolbarY - controlHeight - 2);
                } else {
                    scrMove(recordingControl,toolbarX, toolbarY + toolbarHeight + 2);
                }
            }
        }
    }
}

/**
 * @brief 更新工具栏和子工具栏位置
 * @author chiangyang
 */
void SnipScreen::updateMenuPosition() {
    QRect selection = m_selector->selected();

    if (m_isRecordingMode) {
        if (m_recordingToolbar && m_recordingToolbar->isVisible()) {
            QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
            calculateToolbarPositions(m_recordingToolbar, subToolbar, m_recordingControl, selection, false);
        }
    } else {
        if (m_toolbar && m_toolbar->isVisible()) {
            QWidget *subToolbar = m_toolbar->getSubToolbarWindow();
            // 截图模式下不需要录屏控制栏
            calculateToolbarPositions(m_toolbar, subToolbar, nullptr, selection, true);
        }
    }
}

// ============================================================
// 工具栏显示/隐藏
// ============================================================

/**
 * @brief 显示当前模式对应的工具栏
 * @author chiangyang
 */
void SnipScreen::showToolBar() {
    if (!m_selector)
        return;

    QRect selection = m_selector->selected();
    if (selection.isEmpty())
        return;

    if (m_isRecordingMode) {
        showRecordingToolBar();
    } else {
        if (!m_toolbar)
            return;
        m_toolbar->updateButtonStyles();
        m_toolbar->positionNearSelection(selection);
        m_toolbar->show();
        m_toolbar->raise();
        m_toolbar->update();
        LOG_INFO(QString("[SnipScreen] Screenshot toolbar shown at (%1, %2)")
            .arg(m_toolbar->x()).arg(m_toolbar->y()));
    }
}

/**
 * @brief 隐藏所有工具栏
 * @author chiangyang
 */
void SnipScreen::hideToolBar() {
    if (m_toolbar) {
        m_toolbar->hide();
        QWidget *subToolbar = m_toolbar->getSubToolbarWindow();
        if (subToolbar) {
            subToolbar->hide();
        }
    }
    hideRecordingToolBar();
}

// ============================================================
// 标注操作
// ============================================================

/**
 * @brief 撤销标注操作
 * @author chiangyang
 */
void SnipScreen::undoAnnotation() {
    m_annotationHandler->manager().undo();
    if (m_isRecordingMode && m_recordingToolbar) {
        m_recordingToolbar->updateState(true, m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    } else if (m_toolbar) {
        updateToolBarState(m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    }
    update();
    pushAnnotationOverlay();
    LOG_INFO("[SnipScreen] Undo annotation");
}

/**
 * @brief 重做标注操作
 * @author chiangyang
 */
void SnipScreen::redoAnnotation() {
    m_annotationHandler->manager().redo();
    if (m_isRecordingMode && m_recordingToolbar) {
        m_recordingToolbar->updateState(true, m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    } else if (m_toolbar) {
        updateToolBarState(m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    }
    update();
    pushAnnotationOverlay();
    LOG_INFO("[SnipScreen] Redo annotation");
}

/**
 * @brief 清除所有标注
 * @author chiangyang
 */
void SnipScreen::clearAnnotations() {
    m_annotationHandler->manager().clear();
    if (m_isRecordingMode && m_recordingToolbar) {
        m_recordingToolbar->updateState(true, m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    } else if (m_toolbar) {
        updateToolBarState(m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    }
    update();
    pushAnnotationOverlay();
    LOG_INFO("[SnipScreen] Clear all annotations");
}

// ============================================================
// 模式切换
// ============================================================

/**
 * @brief 切换到录屏模式
 * @author chiangyang
 */
void SnipScreen::switchToRecordingMode() {
    if (m_isRecordingMode)
        return;

    LOG_INFO("[SnipScreen] Switching to recording mode");
    m_isRecordingMode = true;

    // 隐藏截图工具栏
    hideToolBar();

    // 更新边框颜色
    updateBorderColor();

    // 显示录屏工具栏
    if (m_selector->status() == SelectorStatus::Captured) {
        showToolBar();
        // 保留切换前已选中的标注工具
        if (m_annotationToolSelected) {
            m_recordingToolbar->selectAnnotationTool(static_cast<int>(m_annotationHandler->tool()));
        }
    }

    update();
}

/**
 * @brief 切换到截图模式
 * @author chiangyang
 */
void SnipScreen::switchToScreenshotMode() {
    if (!m_isRecordingMode)
        return;

    LOG_INFO("[SnipScreen] Switching to screenshot mode");
    m_isRecordingMode = false;

    // 隐藏录屏工具栏
    hideRecordingToolBar();

    // 更新边框颜色
    updateBorderColor();

    // 显示截图工具栏
    if (m_selector->status() == SelectorStatus::Captured) {
        showToolBar();
        // 保留切换前已选中的标注工具
        if (m_annotationToolSelected) {
            m_toolbar->selectAnnotationTool(static_cast<int>(m_annotationHandler->tool()));
        }
    }

    update();
}

/**
 * @brief 根据当前模式更新选区边框颜色
 * @author chiangyang
 */
void SnipScreen::updateBorderColor() {
    if (m_isRecordingMode) {
        m_selector->setBorderPen(QPen(StyleManager::getRecordBorderColor(), StyleManager::SNIP_BORDER_WIDTH, Qt::SolidLine));
    } else {
        m_selector->setBorderPen(QPen(StyleManager::getCaptureBorderColor(), StyleManager::SNIP_BORDER_WIDTH, Qt::SolidLine));
    }
    m_selector->update();
}

/**
 * @brief 显示录屏工具栏
 * @author chiangyang
 */
void SnipScreen::showRecordingToolBar() {
    if (!m_recordingToolbar || !m_selector)
        return;

    QRect selection = m_selector->selected();
    if (selection.isEmpty())
        return;

    m_recordingToolbar->updateButtonStyles();
    m_recordingToolbar->positionNearSelection(selection);
    m_recordingToolbar->show();
    m_recordingToolbar->raise();
    m_recordingToolbar->update();
    updateInputMask();

    // 录制控制栏不自动显示，由"录屏"按钮控制

    LOG_INFO(QString("[SnipScreen] Recording toolbar shown at (%1, %2)")
        .arg(m_recordingToolbar->x()).arg(m_recordingToolbar->y()));
}

/**
 * @brief 隐藏录屏工具栏和控制栏
 * @author chiangyang
 */
void SnipScreen::hideRecordingToolBar() {
    if (m_recordingToolbar) {
        m_recordingToolbar->hide();
        QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
        if (subToolbar) {
            subToolbar->hide();
        }
    }
    if (m_recordingControl) {
        m_recordingControl->hide();
    }
    updateInputMask();
}

// ============================================================
// 辅助方法
// ============================================================

/**
 * @brief 更新工具栏撤销/重做按钮状态
 * @param canUndo 是否可撤销
 * @param canRedo 是否可重做
 * @author chiangyang
 */
void SnipScreen::updateToolBarState(bool canUndo, bool canRedo) {
    if (m_isRecordingMode && m_recordingToolbar) {
        m_recordingToolbar->updateState(true, canUndo, canRedo);
    } else if (m_toolbar) {
        m_toolbar->updateState(true, canUndo, canRedo);
    }
}

/**
 * @brief 检查鼠标位置是否在工具栏区域内
 * @param globalPos 全局坐标
 * @return 是否在工具栏内
 * @author chiangyang
 */
bool SnipScreen::isMouseInToolBar(const QPoint &globalPos) const {
    // 检查截图工具栏
    if (m_toolbar && m_toolbar->isVisible()) {
        QPoint toolbarPos = m_toolbar->mapFromGlobal(globalPos);
        if (m_toolbar->rect().contains(toolbarPos))
            return true;

        QWidget *subToolbar = m_toolbar->getSubToolbarWindow();
        if (subToolbar && subToolbar->isVisible()) {
            QPoint subPos = subToolbar->mapFromGlobal(globalPos);
            if (subToolbar->rect().contains(subPos))
                return true;
        }
    }

    // 检查录屏工具栏
    if (m_recordingToolbar && m_recordingToolbar->isVisible()) {
        QPoint toolbarPos = m_recordingToolbar->mapFromGlobal(globalPos);
        if (m_recordingToolbar->rect().contains(toolbarPos))
            return true;

        QWidget *subToolbar = m_recordingToolbar->getSubToolbarWindow();
        if (subToolbar && subToolbar->isVisible()) {
            QPoint subPos = subToolbar->mapFromGlobal(globalPos);
            if (subToolbar->rect().contains(subPos))
                return true;
        }
    }

    // 检查录制控制栏
    if (m_recordingControl && m_recordingControl->isVisible()) {
        QPoint controlPos = m_recordingControl->mapFromGlobal(globalPos);
        if (m_recordingControl->rect().contains(controlPos))
            return true;
    }

    return false;
}

/**
 * @brief 检查鼠标位置是否在选区内
 * @param globalPos 全局坐标
 * @return 是否在选区内
 * @author chiangyang
 */
bool SnipScreen::isMouseInSelection(const QPoint &globalPos) const {
    QRect selection = m_selector->selected();
    return selection.contains(globalPos);
}

/**
 * @brief 将坐标限制在选区边界内
 * @param pos 全局坐标
 * @return 限制后的坐标
 * @author chiangyang
 */
QPoint SnipScreen::clampToSelection(const QPoint &pos) const {
    QRect selection = m_selector->selected();
    QPoint clamped = pos;
    if (clamped.x() < selection.left()) clamped.setX(selection.left());
    if (clamped.x() > selection.right()) clamped.setX(selection.right());
    if (clamped.y() < selection.top()) clamped.setY(selection.top());
    if (clamped.y() > selection.bottom()) clamped.setY(selection.bottom());
    return clamped;
}

/**
 * @brief 完成文本编辑，创建文本标注
 * @author chiangyang
 */
void SnipScreen::finalizeTextEdit() {
    if (!m_textEdit)
        return;

    QString text = m_textEdit->toPlainText().trimmed();
    LOG_INFO(QString("[SnipScreen] finalizeTextEdit: text='%1', isEmpty=%2")
        .arg(text).arg(text.isEmpty()));

    if (!text.isEmpty()) {
        // 计算文本在 OverlayTextEdit 中的实际渲染位置（基线坐标）
        // OverlayTextEdit 是 SnipScreen 的子控件，pos() 返回父相对坐标
        // 转换为全局坐标与其他标注类型统一
        QPoint widgetPos = m_textEdit->pos() + m_virtualGeometry.topLeft();
        int frameWidth = m_textEdit->frameWidth();
        qreal docMargin = m_textEdit->document()->documentMargin();
        QFont font;
        font.setPixelSize(m_textEdit->fontSize());
        QFontMetrics fm(font);

        // 文本相对于控件左上角的偏移（控件坐标）
        int textOffsetX = frameWidth + static_cast<int>(docMargin);
        int textOffsetY = frameWidth + static_cast<int>(docMargin) + fm.ascent();

        // 获取旋转角度
        qreal rotation = m_textEdit->rotationDegrees();
        LOG_INFO(QString("[SnipScreen] Text rotation: %1 degrees").arg(rotation));

        QPoint textPos;
        if (rotation != 0.0) {
            // 计算旋转后的文本位置
            // OverlayTextEdit 的旋转中心是控件中心
            int widgetWidth = m_textEdit->width();
            int widgetHeight = m_textEdit->height();
            QPoint widgetCenter = widgetPos + QPoint(widgetWidth / 2, widgetHeight / 2);

            // 文本相对于旋转中心的偏移（控件坐标）
            int dx = textOffsetX - widgetWidth / 2;
            int dy = textOffsetY - widgetHeight / 2;

            // 应用旋转变换（以控件中心为旋转中心）
            qreal rad = qDegreesToRadians(rotation);
            qreal cosRad = std::cos(rad);
            qreal sinRad = std::sin(rad);

            // 旋转后的相对偏移
            int rotatedDx = static_cast<int>(dx * cosRad - dy * sinRad);
            int rotatedDy = static_cast<int>(dx * sinRad + dy * cosRad);

            // 旋转后的全局坐标
            textPos = widgetCenter + QPoint(rotatedDx, rotatedDy);
        } else {
            // 无旋转时的正常位置
            textPos = QPoint(widgetPos.x() + textOffsetX, widgetPos.y() + textOffsetY);
        }

        LOG_INFO(QString("[SnipScreen] Text annotation pos: widget=(%1,%2) offset=(%3,%4) rotation=%5 -> final=(%6,%7)")
            .arg(widgetPos.x()).arg(widgetPos.y())
            .arg(textOffsetX).arg(textOffsetY)
            .arg(rotation)
            .arg(textPos.x()).arg(textPos.y()));

        auto textAnnotation = std::make_unique<TextAnnotation>(
            textPos, m_annotationHandler->color(), m_textEdit->fontSize());
        textAnnotation->setText(text);
        textAnnotation->setRotation(rotation);
        m_annotationHandler->manager().add(std::move(textAnnotation));
        m_annotationHandler->manager().commit();
        updateToolBarState(m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    }

    m_textEdit->hide();
    m_textEdit->deleteLater();
    m_textEdit = nullptr;
    // 文本编辑框关闭：恢复标注快捷键
    m_annotationController->setBareKeysEnabled(true);
    update();
    pushAnnotationOverlay();
    LOG_INFO("[SnipScreen] Text annotation finalized");
}

/**
 * @brief 将全局坐标映射到选区相对坐标
 * @param globalPos 全局坐标
 * @return 选区内相对坐标
 * @author chiangyang
 */
QPoint SnipScreen::mapToSelection(const QPoint &globalPos) const {
    QRect selection = m_selector->selected();
    return globalPos - selection.topLeft();
}

/**
 * @brief 将全局坐标映射到背景图坐标
 * @param globalPos 全局坐标
 * @return 背景图坐标
 * @author chiangyang
 */
QPoint SnipScreen::mapToBackground(const QPoint &globalPos) const {
    // 由于窗口geometry = 虚拟桌面，全局坐标即背景图坐标
    return globalPos;
}


/**
 * @brief 根据配置刷新标注工具默认值
 *
 * 当 SettingsWindow 中默认画笔粗细或字号变更时调用，
 * 更新截图工具栏和录屏工具栏的默认值设置。
 * @author chiangyang
 */
void SnipScreen::refreshAnnotationToolDefaults() {
    LOG_INFO("Refreshing annotation tool defaults from settings");
    if (m_toolbar) {
        m_toolbar->refreshDefaultValues();
    }
    if (m_recordingToolbar) {
        m_recordingToolbar->refreshDefaultValues();
    }
}


/**
 * @brief 连接工具栏信号到设置窗口
 *
 * 当 SettingsWindow 创建后调用，建立工具栏到设置窗口的双向同步。
 * @param settingsWindow 设置窗口指针
 * @author chiangyang
 */
void SnipScreen::connectToolBarToSettingsWindow(SettingsWindow *settingsWindow) {
    if (!settingsWindow) {
        LOG_INFO("connectToolBarToSettingsWindow: settingsWindow is null");
        return;
    }

    LOG_INFO("Connecting toolbar signals to SettingsWindow");

    // 截图工具栏 -> SettingsWindow
    if (m_toolbar) {
        connect(m_toolbar, &ScreenshotToolBar::toolPenWidthChanged,
                settingsWindow, &SettingsWindow::onToolPenWidthChanged);
        connect(m_toolbar, &ScreenshotToolBar::toolFontSizeChanged,
                settingsWindow, &SettingsWindow::onToolFontSizeChanged);
        connect(m_toolbar, &ScreenshotToolBar::toolEraserWidthChanged,
                settingsWindow, &SettingsWindow::onToolEraserWidthChanged);
        connect(m_toolbar, &ScreenshotToolBar::toolMosaicSizeChanged,
                settingsWindow, &SettingsWindow::onToolMosaicSizeChanged);
        LOG_INFO("Screenshot toolbar connected to SettingsWindow");
    }

    // 录屏工具栏 -> SettingsWindow
    if (m_recordingToolbar) {
        connect(m_recordingToolbar, &RecordingToolBar::toolPenWidthChanged,
                settingsWindow, &SettingsWindow::onToolPenWidthChanged);
        connect(m_recordingToolbar, &RecordingToolBar::toolFontSizeChanged,
                settingsWindow, &SettingsWindow::onToolFontSizeChanged);
        connect(m_recordingToolbar, &RecordingToolBar::toolEraserWidthChanged,
                settingsWindow, &SettingsWindow::onToolEraserWidthChanged);
        connect(m_recordingToolbar, &RecordingToolBar::toolMosaicSizeChanged,
                settingsWindow, &SettingsWindow::onToolMosaicSizeChanged);
        LOG_INFO("Recording toolbar connected to SettingsWindow");
    }
}
