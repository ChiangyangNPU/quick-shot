#include "RecordingSession.h"
#include "SnipScreen.h"
#include "Selector.h"
#include "AnnotationInteractionHandler.h"
#include "Annotation.h"
#include "RecordingToolBar.h"
#include "RecordingControlWindow.h"
#include "ScreenRecorder.h"
#include "CaptureSession.h"
#include "../core/StyleManager.h"
#include "../log/Logger.h"

#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QTimer>
#include <QTransform>
#include <QWidget>

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

RecordingSession::RecordingSession(SnipScreen *host, QObject *parent)
    : QObject(parent)
    , m_host(host) {
}

// ============================================================
// 组件创建（原 SnipScreen::createComponents 录屏段）
// ============================================================

void RecordingSession::createComponents() {
    m_toolbar = new RecordingToolBar(m_host);
    m_toolbar->hide();

    m_control = new RecordingControlWindow(m_host);
    m_control->hide();

    m_recorder = new ScreenRecorder(m_host);
}

// ============================================================
// 信号接线（原 SnipScreen::connectRecordingSignals +
// connectRecordingToolbarSignals）
// ============================================================

void RecordingSession::connectSignals() {
    // ---------- 录制控制栏 ----------
    connect(m_control, &RecordingControlWindow::startRequested, [this]() {
        QRect selection = m_host->selector()->selected();
        // 减去边框宽度，确保边框不被录入视频
        int borderWidth = StyleManager::SNIP_BORDER_WIDTH;
        QRect captureRect = selection.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
        // 使用实际捕获大小作为输出分辨率
        QSize resolution = captureRect.size();

        // 生成默认保存路径（GIF 模式使用 .gif 扩展名与 10fps）
        const bool gifFormat = (m_control->selectedFormat() == QLatin1String("gif"));
        const QString extension = gifFormat ? QStringLiteral(".gif") : QStringLiteral(".mp4");
        const int recordFps = gifFormat ? 10 : 30;
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
            + "/QuickShot_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss") + extension;

        if (gifFormat) {
            // GIF 无音轨，强制关闭音频采集
            m_recorder->setAudioEnabled(false);
            m_recorder->setMicrophoneEnabled(false);
        }

        bool ok = m_recorder->start(captureRect, resolution, defaultPath, recordFps,
                                    gifFormat ? ScreenRecorder::OutputFormat::Gif
                                              : ScreenRecorder::OutputFormat::Video);
        if (ok) {
            m_control->setRecording(true);
            m_control->setPaused(false);
            // 录制中锁定选区，禁止移动/调整
            m_host->selector()->setStatus(SelectorStatus::Locked);
            // 更新信息标签并显示录制时间
            m_host->selector()->updateInfoLabel();
            m_host->selector()->showTimerLabel();
            m_toolbar->startTimer();
#ifdef Q_OS_WIN
            // 将本窗口排除出录屏捕获：BitBlt(CAPTUREBLT) 会捕获到 SnipScreen 已绘制的标注，
            // 若不排除，再叠加 overlay 会导致视频中出现重复标注。排除后标注仅由 overlay 合成。
            if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(m_host->winId()), WDA_EXCLUDEFROMCAPTURE)) {
                LOG_WARNING("[SnipScreen] SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed, "
                            "recorded video may contain duplicate annotations");
            }
#endif
            pushAnnotationOverlay();
            updateInputMask();
            // 录屏开始后禁用录屏工具栏上的截图与 GIF 按钮
            m_toolbar->setScreenshotButtonEnabled(false);
            m_toolbar->setGifButtonEnabled(false);
            LOG_INFO(QString("[SnipScreen] Recording started: %1").arg(defaultPath));
        }
    });
    connect(m_control, &RecordingControlWindow::pauseRequested, [this]() {
        m_recorder->pause();
        m_control->setPaused(true);
        m_toolbar->pauseTimer();
    });
    connect(m_control, &RecordingControlWindow::resumeRequested, [this]() {
        m_recorder->resume();
        m_control->setPaused(false);
        m_toolbar->resumeTimer();
    });
    connect(m_control, &RecordingControlWindow::stopRequested, [this]() {
        // 不 clearMask：录制收尾（Finalize）期间保持输入遮罩与工具栏可见，
        // 等待 stopped 信号触发宿主 exit() 统一清理
        resetUi();
        m_recorder->stop();
        // 不在此处 exit()，等待 ScreenRecorder::stopped 信号（Finalize 完成后）
    });
    connect(m_control, &RecordingControlWindow::systemAudioToggled, [this](bool enabled) {
        m_recorder->setAudioEnabled(enabled);
    });
    connect(m_control, &RecordingControlWindow::microphoneToggled, [this](bool enabled) {
        m_recorder->setMicrophoneEnabled(enabled);
    });

    // ---------- 录制器 ----------
    connect(m_recorder, &ScreenRecorder::stopped, [this](const QString &outputFilePath) {
#ifdef Q_OS_WIN
        // 录制结束，恢复窗口捕获属性
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(m_host->winId()), WDA_NONE);
#endif
        // 录制结束（正常停止或取消），重新启用截图按钮
        m_toolbar->setScreenshotButtonEnabled(true);
        m_toolbar->setGifButtonEnabled(true);
        if (m_isRecordingCanceled) {
            LOG_INFO(QString("[SnipScreen] Recording canceled, output file: %1").arg(outputFilePath));
            // 录制被取消，不退出截图，重置取消标志
            m_isRecordingCanceled = false;
        } else {
            LOG_INFO(QString("[SnipScreen] Recording saved: %1").arg(outputFilePath));
            m_host->exit();
        }
    });
    connect(m_recorder, &ScreenRecorder::errorOccurred, [this](const QString &message) {
#ifdef Q_OS_WIN
        // 录制出错，恢复窗口捕获属性
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(m_host->winId()), WDA_NONE);
#endif
        LOG_INFO(QString("[SnipScreen] Recording error: %1").arg(message));
        m_control->setRecording(false);
        m_control->setPaused(false);
        // 录制出错，重新启用截图按钮
        m_toolbar->setScreenshotButtonEnabled(true);
        m_toolbar->setGifButtonEnabled(true);
    });

    // ---------- 录屏工具栏：标注工具 ----------
    connect(m_toolbar, &RecordingToolBar::toolSelected, [this](int toolId) {
        m_host->annotationHandler()->setTool(static_cast<AnnotationType>(toolId));
        m_host->setAnnotationToolSelected(true);
        m_host->selector()->setAnnotationMode(true);
        updateInputMask();
        LOG_INFO(QString("[SnipScreen] Recording: Annotation tool selected: ToolId=%1").arg(toolId));
        // 标注工具选择后，子工具栏显示完成，重新定位工具栏
        QTimer::singleShot(0, m_host, [this]() {
            m_host->updateMenuPosition();
        });
    });
    connect(m_toolbar, &RecordingToolBar::annotationToolDeselected, [this]() {
        m_host->setAnnotationToolSelected(false);
        m_host->selector()->setAnnotationMode(false);
        // 子工具栏隐藏后，重新定位控制栏
        m_host->updateMenuPosition();
        updateInputMask();
        LOG_INFO("[SnipScreen] Recording: Annotation tool deselected");
    });
    connect(m_toolbar, &RecordingToolBar::shapeTypeChanged, [this](int type) {
        m_host->annotationHandler()->setShapeType(type);
        LOG_INFO(QString("[SnipScreen] Recording: Shape type changed: %1").arg(type));
    });
    connect(m_toolbar, &RecordingToolBar::penColorChanged, [this](const QColor &color) {
        m_host->annotationHandler()->setColor(color);
    });
    connect(m_toolbar, &RecordingToolBar::penWidthChanged, [this](int width) {
        m_host->annotationHandler()->setPenWidth(width);
    });
    connect(m_toolbar, &RecordingToolBar::penStyleChanged, [this](bool dashed) {
        m_host->annotationHandler()->setDashed(dashed);
        LOG_INFO(QString("[RecordingSession] Pen style changed: %1").arg(dashed ? "Dashed" : "Solid"));
    });
    connect(m_toolbar, &RecordingToolBar::fontSizeChanged, [this](int size) {
        m_host->annotationHandler()->setFontSize(size);
    });
    // 标注操作信号：直接复用宿主公共槽
    connect(m_toolbar, &RecordingToolBar::undoRequested, m_host, &SnipScreen::undoAnnotation);
    connect(m_toolbar, &RecordingToolBar::redoRequested, m_host, &SnipScreen::redoAnnotation);
    connect(m_toolbar, &RecordingToolBar::clearRequested, m_host, &SnipScreen::clearAnnotations);

    // ---------- 录屏工具栏：模式与操作 ----------
    connect(m_toolbar, &RecordingToolBar::screenshotRequested, m_host, &SnipScreen::switchToScreenshotMode);
    connect(m_toolbar, &RecordingToolBar::cancelRecordRequested, [this]() {
        // 如果正在录制，取消录制（删除视频文件）
        if (m_recorder && m_recorder->isRecording()) {
            LOG_INFO("[SnipScreen] Cancel button pressed during recording, canceling...");
            cancel();
        } else {
            m_host->exit();
        }
    });
    connect(m_toolbar, &RecordingToolBar::showControlRequested, [this](bool show) {
        if (show) {
            m_control->updateButtonStyles();
            m_control->show();
            m_control->raise();
            // 使用统一位置计算方法
            m_host->updateMenuPosition();
            updateInputMask();
        } else {
            m_control->hide();
            updateInputMask();
        }
    });
    connect(m_toolbar, &RecordingToolBar::gifRecordRequested, [this]() {
        showControlForGif();
    });
    connect(m_toolbar, &RecordingToolBar::snapshotRequested, [this]() {
        // 快照：复制当前选区画面（含标注）到剪贴板，不影响录屏状态
        QPixmap pixmap = m_host->captureSelectionForOcr();
        if (!pixmap.isNull()) {
            CaptureSession::publish(pixmap);
            LOG_INFO("[SnipScreen] Snapshot copied to clipboard");
        }
    });
    connect(m_toolbar, &RecordingToolBar::ocrRequested, [this]() {
        m_host->performOcr(m_host->captureSelectionForOcr());
    });
    connect(m_toolbar, &RecordingToolBar::translateRequested, m_host, &SnipScreen::performTranslate);
    connect(m_toolbar, &RecordingToolBar::timerUpdated, [this](const QString &text) {
        m_host->selector()->updateTimerText(text);
        updateInputMask();
    });
}

// ============================================================
// 模式与状态
// ============================================================

bool RecordingSession::isMode() const {
    return m_isMode;
}

void RecordingSession::setMode(bool recording) {
    m_isMode = recording;
}

bool RecordingSession::isRecording() const {
    return m_recorder && m_recorder->isRecording();
}

RecordingToolBar *RecordingSession::toolbar() const {
    return m_toolbar;
}

RecordingControlWindow *RecordingSession::control() const {
    return m_control;
}

ScreenRecorder *RecordingSession::recorder() const {
    return m_recorder;
}

// ============================================================
// 供 SnipScreen 委托的批处理
// ============================================================

void RecordingSession::uncheckAllAnnotationBtns() {
    if (m_toolbar) m_toolbar->uncheckAllAnnotationBtns();
}

void RecordingSession::retranslateUi() {
    if (m_toolbar) m_toolbar->retranslateUi();
    if (m_control) m_control->retranslateUi();
}

void RecordingSession::refreshDefaultValues() {
    if (m_toolbar) m_toolbar->refreshDefaultValues();
}

void RecordingSession::updateToolBarState(bool hasSelection, bool canUndo, bool canRedo) {
    if (m_toolbar) m_toolbar->updateState(hasSelection, canUndo, canRedo);
}

bool RecordingSession::isMouseInToolbars(const QPoint &globalPos) const {
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

    if (m_control && m_control->isVisible()) {
        QPoint controlPos = m_control->mapFromGlobal(globalPos);
        if (m_control->rect().contains(controlPos))
            return true;
    }

    return false;
}

// ============================================================
// 工具栏显隐（原 showRecordingToolBar / hideRecordingToolBar）
// ============================================================

void RecordingSession::showToolBar() {
    if (!m_toolbar || !m_host->selector())
        return;

    QRect selection = m_host->selector()->selected();
    if (selection.isEmpty())
        return;

    m_toolbar->updateButtonStyles();
    m_toolbar->positionNearSelection(selection);
    m_toolbar->show();
    m_toolbar->raise();
    m_toolbar->update();
    updateInputMask();

    // 录制控制栏不自动显示，由"录屏"按钮控制

    LOG_INFO(QString("[SnipScreen] Recording toolbar shown at (%1, %2)")
        .arg(m_toolbar->x()).arg(m_toolbar->y()));
}

/**
 * @brief 打开录制控制栏并预选 GIF 格式
 *
 * 截屏工具栏与录屏工具栏的 GIF 按钮共用入口。打开后用户点击控制栏里的
 * 「开始」即按 GIF 格式录制。
 * @author chiangyang
 */
void RecordingSession::showControlForGif() {
    if (!m_control || !m_toolbar) {
        return;
    }
    m_control->setSelectedFormat(QStringLiteral("gif"));
    // 录屏按钮置为选中（已选中时不会重复触发），保持工具栏状态一致
    m_toolbar->setRecordButtonChecked(true);
    m_control->updateButtonStyles();
    m_control->show();
    m_control->raise();
    m_host->updateMenuPosition();
    updateInputMask();
    LOG_INFO("[SnipScreen] GIF recording control opened");
}

void RecordingSession::hideToolBar() {
    if (m_toolbar) {
        m_toolbar->hide();
        // 工具栏隐藏时控制栏同步关闭，录屏按钮必须一并恢复未选中：
        // 否则按钮保持高亮，下次进入录屏模式时 toggled 不会再触发，
        // 点击该按钮也只会先取消选中、再点一次才能重新打开控制栏
        m_toolbar->resetRecordBtn();
        QWidget *subToolbar = m_toolbar->getSubToolbarWindow();
        if (subToolbar) {
            subToolbar->hide();
        }
    }
    if (m_control) {
        m_control->hide();
    }
    updateInputMask();
}

// ============================================================
// 录制生命周期（原 togglePauseRecording / stopRecording /
// onCancel 录制分支 / cancelRecordRequested 公共路径）
// ============================================================

void RecordingSession::togglePause() {
    if (!m_recorder || !m_recorder->isRecording()) {
        LOG_INFO("[SnipScreen] togglePauseRecording: not recording, ignored");
        return;
    }
    if (m_recorder->isPaused()) {
        m_recorder->resume();
        m_toolbar->resumeTimer();
        m_control->setPaused(false);
        LOG_INFO("[SnipScreen] Recording resumed by global hotkey");
    } else {
        m_recorder->pause();
        m_toolbar->pauseTimer();
        m_control->setPaused(true);
        LOG_INFO("[SnipScreen] Recording paused by global hotkey");
    }
}

void RecordingSession::stop() {
    if (!m_recorder || !m_recorder->isRecording()) {
        LOG_INFO("[SnipScreen] stopRecording: not recording, ignored");
        return;
    }
    // 走正常停止路径（保留文件），区别于 ESC 的 cancel 路径（删除文件）
    m_isRecordingCanceled = false;
    resetUi();
    m_host->clearMask();
    m_recorder->stop();
    LOG_INFO("[SnipScreen] Recording stopped by global hotkey");
}

void RecordingSession::cancel() {
    // 取消录制（删除视频文件）：置取消标志，stopped 信号处理时不退出截图
    m_isRecordingCanceled = true;
    resetUi();
    m_host->clearMask();
    m_recorder->cancel();
}

void RecordingSession::resetUi() {
    m_toolbar->stopTimer();
    m_toolbar->resetRecordBtn();
    m_host->selector()->hideTimerLabel();
    m_control->setRecording(false);
    m_control->setPaused(false);
    m_control->hide();
}

// ============================================================
// 标注 overlay 渲染与推送（原 renderAnnotationOverlay /
// pushAnnotationOverlay）
// ============================================================

QImage RecordingSession::renderAnnotationOverlay() const {
    const AnnotationInteractionHandler *handler = m_host->annotationHandler();
    if (!handler->manager().hasAnnotations() && !handler->manager().hasMosaicStrokes())
        return QImage();

    // 使用与录屏捕获区域一致的矩形（选区减去边框），确保 overlay 坐标与捕获帧对齐，
    // 否则 overlay 标注会相对捕获画面偏移一个边框宽度
    const int overlayBw = StyleManager::SNIP_BORDER_WIDTH;
    const QRect cap = m_host->selector()->selected().adjusted(overlayBw, overlayBw, -overlayBw, -overlayBw);
    if (cap.isEmpty()) return QImage();

    if (!m_recorder) return QImage();
    const QSize outSize = m_recorder->outputSize();
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

    if (handler->manager().hasAnnotations())
        handler->manager().draw(painter);

    // 马赛克：基于选区背景做真实像素化，叠加在最上层遮挡内容
    if (handler->manager().hasMosaicStrokes() && !m_host->background().isNull()) {
        painter.save();
        // 取消外层的坐标变换，直接在输出尺寸像素坐标系下操作
        painter.setWorldTransform(QTransform());

        // 1. 从全屏背景裁剪出选区，缩放到录屏输出尺寸
        QPixmap bgSelection = m_host->background().copy(cap);
        QPixmap bgScaled = bgSelection.scaled(outSize,
                                              Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);

        // 2. 整体马赛克预处理：先 1/块大小 缩小再放大
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
        const std::vector<QRect> strokes = handler->manager().mosaicStrokes();
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

void RecordingSession::pushAnnotationOverlay() {
    if (!m_recorder || !m_recorder->isRecording())
        return;
    m_recorder->setAnnotationOverlay(renderAnnotationOverlay());
}

// ============================================================
// 输入遮罩（原 updateInputMask）
// ============================================================

void RecordingSession::updateInputMask() {
    // 仅在录屏中且未选中标注工具时限制输入区域
    bool passthrough = isMode() && m_recorder
                       && m_recorder->isRecording()
                       && !m_host->annotationToolSelected();

    if (!passthrough) {
        m_host->clearMask();
        return;
    }

    QRegion mask;
    QPoint localOffset = -m_host->virtualGeometry().topLeft();

    // 工具栏区域（子控件 geometry() 已是父相对坐标）
    if (m_toolbar && m_toolbar->isVisible()) {
        mask += m_toolbar->geometry();
        QWidget *sub = m_toolbar->getSubToolbarWindow();
        if (sub && sub->isVisible())
            mask += sub->geometry();
    }

    // 录制控制栏区域
    if (m_control && m_control->isVisible()) {
        mask += m_control->geometry();
    }

    // 选区边框（selected() 返回屏幕坐标，需转为本地坐标）
    // 挖孔带宽度必须与实际绘制的边框一致（StyleManager::SNIP_BORDER_WIDTH），
    // 否则边框上会留下一条隐形的事件捕获带
    if (Selector *sel = m_host->selector()) {
        QRect selRect = sel->selected();
        if (selRect.isValid() && !selRect.isEmpty()) {
            QRect localSel = selRect.translated(localOffset);
            const int bw = StyleManager::SNIP_BORDER_WIDTH;
            mask += QRegion(localSel).subtracted(localSel.adjusted(bw, bw, -bw, -bw));
        }

        // 信息标签和时间标签区域
        QRegion labelGeo = sel->getLabelGeometry();
        if (!labelGeo.isEmpty()) {
            mask += labelGeo;
        }
    }

    m_host->setMask(mask);
}
