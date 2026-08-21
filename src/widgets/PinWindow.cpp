#include "PinWindow.h"
#include "PinAnnotationToolBar.h"
#include "SettingsWindow.h"
#include "OverlayTextEdit.h"
#include "../core/StyleManager.h"
#include "../core/ConfigManager.h"
#include "../capture/Resizer.h"
#include "../capture/Annotation.h"
#include "../shortcut/AnnotationShortcutController.h"
#include "../ocr/OcrEngine.h"
#include "../ocr/OcrResultDialog.h"
#include "../translate/TranslateService.h"
#include "TranslateOverlayWindow.h"
#include "../translate/TranslateEngine.h"
#include "../utils/Utils.h"
#include "../history/HistoryManager.h"
#include <QCursor>
#include <QTimer>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QDateTime>
#include <QLabel>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QShortcut>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <memory>
#include <cmath>
#include <QApplication>

#include "../core/TranslationManager.h"
#include "../log/Logger.h"

/**
 * @brief 构造函数
 * @param pixmap 要显示的截图
 * @param parent 父对象
 * @author chiangyang
 */
// 静态变量，用于跟踪是否是第一次显示 PinWindow
static bool isFirstPinWindow = true;

// 静态注册表定义：所有存活的 PinWindow 实例
QSet<PinWindow*> PinWindow::s_instances;

/**
 * @brief 切换所有 PinWindow 的显隐
 *
 * 任一可见→全部隐藏（标记 m_hiddenByToggle，工具栏同步隐藏）；
 * 全部不可见→恢复显示曾被本方法隐藏的窗口（标注模式且工具栏存在则同步显示）。
 * @author chiangyang
 */
void PinWindow::toggleAll() {
    bool anyVisible = false;
    for (PinWindow *pw : s_instances) {
        if (pw->isVisible()) { anyVisible = true; break; }
    }
    if (anyVisible) {
        for (PinWindow *pw : s_instances) {
            if (pw->isVisible()) {
                pw->m_hiddenByToggle = true;
                pw->hide();
                if (pw->m_toolBar) pw->m_toolBar->hide();
            }
        }
        LOG_INFO(QString("[PinWindow] toggleAll: hidden %1 pin window(s)").arg(s_instances.size()));
    } else {
        int restored = 0;
        for (PinWindow *pw : s_instances) {
            if (pw->m_hiddenByToggle) {
                pw->m_hiddenByToggle = false;
                pw->show();
                pw->raise();
                pw->activateWindow();
                if (pw->m_toolBar && pw->m_annotationMode) pw->m_toolBar->show();
                ++restored;
            }
        }
        LOG_INFO(QString("[PinWindow] toggleAll: restored %1 pin window(s)").arg(restored));
    }
}

/**
 * @brief 获取当前存活的 PinWindow 数量
 * @return 窗口数量
 * @author chiangyang
 */
int PinWindow::instanceCount() {
    return s_instances.size();
}

PinWindow::PinWindow(const QPixmap &pixmap, QWidget *parent)
    : QWidget(parent), m_pixmap(pixmap), m_isMoving(false), m_isResizing(false), m_ocrLoadingLabel(nullptr) {
    s_instances.insert(this);  // 加入注册表，供 toggleAll 统一显隐
    LOG_INFO(QString("PinWindow instance created, pixmap px: %1x%2 (geometry set by caller after setScreen)")
             .arg(pixmap.width()).arg(pixmap.height()));

    // 验证 pixmap 有效性
    if (pixmap.isNull() || pixmap.width() <= 0 || pixmap.height() <= 0) {
        LOG_WARNING("PinWindow: invalid pixmap received, widget may not display correctly");
    }

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFocus(); // Actively get focus to make ESC key work immediately after creation

    // 设置最小尺寸，防止窗口过小导致 Qt 内部几何计算异常
    setMinimumSize(4, 4);
    
    // Start a short timer to try to get focus again after window is fully displayed
    // 分两步：先在事件循环中显示窗口，然后再激活
    QTimer::singleShot(100, this, [this]() {
        if (!isVisible()) return;
        
        setFocus();
        activateWindow();
        
        // 验证窗口几何有效性
        QRect geo = geometry();
        if (geo.width() < 2 || geo.height() < 2) {
            LOG_WARNING(QString("PinWindow: invalid geometry %1x%2, forcing minimum size")
                .arg(geo.width()).arg(geo.height()));
            resize(qMax(4, geo.width()), qMax(4, geo.height()));
        }
        
        LOG_INFO(QString("PinWindow activated and focused, geometry: %1x%2 at (%3,%4)")
            .arg(width()).arg(height()).arg(x()).arg(y()));

        // 第一次显示时添加提示
        if (isFirstPinWindow) {
            isFirstPinWindow = false;

            // 创建提示标签，支持多语言
            QString hintText = TranslationManager::instance()->get("pinwindow.hint", "Press ESC or left double-click to close");
            QLabel *hintLabel = new QLabel(hintText, this);
            hintLabel->setStyleSheet(StyleManager::getPinHintLabelStyle());
            hintLabel->setAlignment(Qt::AlignCenter);
            hintLabel->adjustSize();

            // 定位提示标签在窗口正中间，确保坐标有效
            int labelX = (width() - hintLabel->width()) / 2;
            int labelY = (height() - hintLabel->height()) / 2;
            labelX = qMax(0, labelX);
            labelY = qMax(0, labelY);
            hintLabel->move(labelX, labelY);
            hintLabel->show();

            // 3秒后自动隐藏提示
            QTimer::singleShot(5000, hintLabel, &QLabel::hide);

            LOG_INFO("PinWindow hint shown for first time");
        }
    });

    // 创建标注交互处理器，注入 PinWindow 特有的 Host 回调
    m_annotationHandler = std::make_unique<AnnotationInteractionHandler>();
    // PinWindow 默认字号 28（区别于 SnipScreen 的 16）
    m_annotationHandler->setFontSize(28);

    AnnotationInteractionHandler::Host host;
    // 坐标限制：将坐标限制在窗口矩形内（选区=窗口 rect）
    host.clampPos = [this](const QPoint &p) {
        QPoint clamped = p;
        QRect sel = rect();
        if (clamped.x() < sel.left()) clamped.setX(sel.left());
        if (clamped.x() > sel.right()) clamped.setX(sel.right());
        if (clamped.y() < sel.top()) clamped.setY(sel.top());
        if (clamped.y() > sel.bottom()) clamped.setY(sel.bottom());
        return clamped;
    };
    // PinWindow 选区=整个窗口，鼠标在窗口内即视为在选区内
    host.isInSelection = [](const QPoint &) { return true; };
    // 工具栏为独立顶层窗口，不在窗口坐标系内
    host.isInToolBar = [](const QPoint &) { return false; };
    host.selectionRect = [this]() { return rect(); };
    host.requestUpdate = [this]() { update(); };
    // PinWindow 无录屏同步需求
    host.syncOverlay = []() {};
    host.updateToolBarState = [this](bool, bool) { updateToolBarState(); };
    host.createTextEdit = [this](const QPoint &pos) { createTextEdit(pos); };
    host.finalizeTextEdit = [this]() { finalizeTextEdit(); };
    host.activeTextEdit = [this]() { return m_textEdit; };
    host.setCursor = [this](Qt::CursorShape s) { setCursor(s); };
    host.setBareKeysEnabled = [this](bool e) {
        if (m_annotationController) m_annotationController->setBareKeysEnabled(e);
    };
    m_annotationHandler->setHost(std::move(host));

    LOG_INFO("PinWindow initialized");
}

/**
 * @brief 绘制事件处理
 *
 * 绘制顺序：拉伸原图 → 标注（含马赛克离屏合成）→ 边框 → 调整大小手柄。
 * 标注坐标为窗口本地坐标，无需 translate。
 * @param event 绘制事件
 * @author chiangyang
 */
void PinWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    
    // 检查 pixmap 和窗口几何有效性
    if (m_pixmap.isNull() || width() < 1 || height() < 1) {
        return;
    }
    
    QPainter painter(this);
    // 物理像素图缩放到逻辑窗口：任意方向缩放（放大或缩小）都开启双线性平滑插值，
    // 避免最近邻缩放产生的锯齿/不清晰（缩小时最近邻降采样会丢细节并产生锯齿）；
    // 仅 1:1（窗口尺寸 == 原图尺寸、无缩放）时关闭以保持像素精确。
    bool scaled = (width() != m_pixmap.width()) || (height() != m_pixmap.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scaled);
    painter.drawPixmap(rect(), m_pixmap, QRectF(m_pixmap.rect()));

    // 绘制标注
    if (m_annotationHandler->manager().hasMosaicStrokes()) {
        // 有马赛克时：先渲染到离屏 canvas（拉伸原图 + 标注），再对 canvas 做马赛克像素化
        // background 使用 canvas 本身（所见即所得），offset 为零（马赛克笔迹已是本地坐标）
        QPixmap canvas(size());
        if (canvas.isNull()) {
            return;
        }
        canvas.fill(Qt::transparent);
        QPainter cp(&canvas);
        cp.setRenderHint(QPainter::SmoothPixmapTransform, scaled);
        cp.drawPixmap(rect(), m_pixmap, QRectF(m_pixmap.rect()));
        m_annotationHandler->drawWithMosaic(cp, canvas, AnnotationManager::kDefaultMosaicBlockSize, QPoint());
        cp.end();
        painter.drawPixmap(0, 0, canvas);
    } else if (m_annotationHandler->manager().hasAnnotations()) {
        // 无马赛克时：直接在窗口上绘制标注
        m_annotationHandler->drawAnnotations(painter, QPoint());
    }

    // Draw Border (Light Blue)
    QPen borderPen(QColor(100, 149, 237), 2); // CornflowerBlue
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);

    // Draw inside the rect to avoid clipping
    QRect borderRect = rect().adjusted(1, 1, -1, -1);
    // 确保调整后的矩形有效（窗口太小时 adjusted 可能导致 right < left）
    if (borderRect.width() > 0 && borderRect.height() > 0) {
        painter.drawRect(borderRect);
    }

    // Draw Resize Handle (Bottom-Right)
    painter.setPen(QPen(Qt::white, 2));
    painter.drawLine(width() - 10, height(), width(), height() - 10);
    painter.drawLine(width() - 5, height(), width(), height() - 5);
}

/**
 * @brief 检查鼠标是否在调整大小区域内
 * @param pos 鼠标位置
 * @return 是否在调整大小区域内
 * @author chiangyang
 */
bool PinWindow::isInResizeArea(const QPoint &pos) {
    return (pos.x() >= width() - 15 && pos.y() >= height() - 15);
}

/**
 * @brief 鼠标按下事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void PinWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    setFocus(); // 确保鼠标操作时重新获取焦点

    // 标注模式且选中工具：转发给标注交互处理器
    if (m_annotationMode && m_annotationToolSelected) {
        m_annotationHandler->handleMousePress(event->pos(), event->modifiers());
        return;
    }

    // 非标注模式：现有拖动/调整大小逻辑
    if (isInResizeArea(event->pos())) {
        m_isResizing = true;
    } else {
        m_isMoving = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

/**
 * @brief 鼠标移动事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void PinWindow::mouseMoveEvent(QMouseEvent *event) {
    QPoint pos = event->pos();

    // 标注模式且选中工具：转发给标注交互处理器
    if (m_annotationMode && m_annotationToolSelected) {
        m_annotationHandler->handleMouseMove(pos, event->modifiers());
        return;
    }

    // 非标注模式：现有逻辑
    if (isInResizeArea(pos)) {
        setCursor(getCursorByLocation(ResizerLocation::BR_ANCHOR));
    } else {
        setCursor(getCursorByLocation(ResizerLocation::EMPTY_INSIDE));
    }

    if (m_isResizing) {
        int newWidth = qMax(50, pos.x());
        int newHeight = qMax(50, pos.y());
        resize(newWidth, newHeight);
    } else if (m_isMoving) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    }
}

/**
 * @brief 鼠标释放事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void PinWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // 标注模式且选中工具：转发给标注交互处理器
    if (m_annotationMode && m_annotationToolSelected) {
        m_annotationHandler->handleMouseRelease();
        return;
    }

    m_isMoving = false;
    m_isResizing = false;
}

/**
 * @brief 鼠标双击事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void PinWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    // 标注模式禁用双击关闭
    if (m_annotationMode) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        event->accept(); // 标记事件已被处理，防止事件继续传播
        hide(); // 先隐藏窗口，使其不再接收或传递事件
        // 使用定时器延迟关闭窗口，确保事件处理完成后再关闭，彻底防止击穿现象
        QTimer::singleShot(50, this, &PinWindow::close);
    }
}

/**
 * @brief 鼠标滚轮事件处理
 * @param event 鼠标滚轮事件
 * @author chiangyang
 */
void PinWindow::wheelEvent(QWheelEvent *event) {
    // 每次滚动放大或缩小10%
    const double scaleFactor = 1.1;

    int newWidth, newHeight;
    if (event->angleDelta().y() > 0) {
        // 滚轮向上滚动，放大窗口
        newWidth = qRound(width() * scaleFactor);
        newHeight = qRound(height() * scaleFactor);
    } else {
        // 滚轮向下滚动，缩小窗口
        newWidth = qRound(width() / scaleFactor);
        newHeight = qRound(height() / scaleFactor);
        // 确保窗口大小不小于最小限制
        newWidth = qMax(50, newWidth);
        newHeight = qMax(50, newHeight);
    }

    // 按实际新旧尺寸计算缩放比，保证标注与图像同步缩放
    double sx = (width() > 0) ? (double)newWidth / width() : 1.0;
    double sy = (height() > 0) ? (double)newHeight / height() : 1.0;

    // 先缩放标注/笔迹坐标，再 resize：resize 触发重绘时标注已是新坐标，与缩放后图像对齐
    m_annotationHandler->manager().scaleAll(sx, sy);

    // 同步缩放活动文本编辑框（若存在）：位置与字号
    if (m_textEdit) {
        m_textEdit->move(qRound(m_textEdit->x() * sx), qRound(m_textEdit->y() * sy));
        m_textEdit->setFontSize(qMax(1, qRound(m_textEdit->fontSize() * (sx + sy) / 2.0)));
        m_textEdit->adjustSizeToContent();
    }

    resize(newWidth, newHeight);
}

/**
 * @brief 键盘按键事件处理
 *
 * 标注快捷键（ESC/Ctrl+C/S/Z/Y、数字键 1-8、Tab、[]、Delete/Backspace）
 * 已全部由 AnnotationShortcutController 通过 QShortcut 统一接管，
 * 此处仅保留非标注模式下的方向键移动窗口逻辑（Ctrl+方向键步长 10px，普通方向键 1px）。
 * @param event 键盘事件
 * @author chiangyang
 */
void PinWindow::keyPressEvent(QKeyEvent *event) {
    // 非标注模式：方向键移动窗口（Ctrl+方向键步长 10px，普通方向键 1px）
    if (!m_annotationMode) {
        int step = (event->modifiers() & Qt::ControlModifier) ? 10 : 1;
        switch (event->key()) {
            case Qt::Key_Left:  move(x() - step, y()); return;
            case Qt::Key_Right: move(x() + step, y()); return;
            case Qt::Key_Up:    move(x(), y() - step); return;
            case Qt::Key_Down:  move(x(), y() + step); return;
            default: break;
        }
    }

    QWidget::keyPressEvent(event);
}

/**
 * @brief 右键菜单事件处理
 * @param event 右键菜单事件
 * @author chiangyang
 */
void PinWindow::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    TranslationManager *tm = TranslationManager::instance();

    // 设置菜单样式
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(StyleManager::getMenuStyle());

    // 添加复制菜单项
    QAction *copyAction = menu.addAction(tm->get("pinwindow.copy", "Copy"));
    connect(copyAction, &QAction::triggered, this, [this]() {
        copyToClipboard();
    });

    // 添加标注菜单项（复制之后）：进入标注模式
    QAction *annotateAction = menu.addAction(tm->get("pinwindow.annotate", "Annotate"));
    connect(annotateAction, &QAction::triggered, this, [this]() {
        enterAnnotationMode();
    });

    // 添加 OCR 菜单项
    QAction *ocrAction = menu.addAction(tm->get("ocr.button"));
    connect(ocrAction, &QAction::triggered, this, [this, tm]() {
        // 显示加载提示
        if (m_ocrLoadingLabel) {
            m_ocrLoadingLabel->hide();
            m_ocrLoadingLabel->deleteLater();
        }
        m_ocrLoadingLabel = new QLabel(tm->get("ocr.recognizing"), this);
        m_ocrLoadingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
        m_ocrLoadingLabel->setAlignment(Qt::AlignCenter);
        m_ocrLoadingLabel->adjustSize();
        m_ocrLoadingLabel->move((width() - m_ocrLoadingLabel->width()) / 2, (height() - m_ocrLoadingLabel->height()) / 2);
        m_ocrLoadingLabel->show();

        // 异步执行 OCR
        auto *watcher = new QFutureWatcher<OcrEngine::OcrResult>(this);
        connect(watcher, &QFutureWatcher<OcrEngine::OcrResult>::finished, this, [this, watcher, tm]() {
            if (m_ocrLoadingLabel) {
                m_ocrLoadingLabel->hide();
                m_ocrLoadingLabel->deleteLater();
                m_ocrLoadingLabel = nullptr;
            }

            OcrEngine::OcrResult result = watcher->result();
            if (result.texts.isEmpty()) {
                QLabel *noText = new QLabel(tm->get("ocr.noText"), this);
                // padding 用 em、font-size 用 pt，随屏幕 DPI 自动缩放
                noText->setStyleSheet(
                    "QLabel { background-color: rgba(0,0,0,180); color: white; "
                    "padding: 0.6em 1em; border-radius: 3px; font-size: 10pt; }");
                noText->setAlignment(Qt::AlignCenter);
                noText->adjustSize();
                noText->move((width() - noText->width()) / 2, (height() - noText->height()) / 2);
                noText->show();
                QTimer::singleShot(2000, noText, &QWidget::deleteLater);
            } else {
                auto *dialog = new OcrResultDialog(result);
                dialog->adjustSize();
                dialog->move(x() + (width() - dialog->width()) / 2,
                             y() + (height() - dialog->height()) / 2);
                dialog->show();
            }
            watcher->deleteLater();
        });

        QImage image = m_pixmap.toImage();
        QFuture<OcrEngine::OcrResult> future = QtConcurrent::run([image]() {
            return OcrEngine::instance()->recognize(image);
        });
        watcher->setFuture(future);
    });

    // 添加翻译菜单项
    QAction *translateAction = menu.addAction(tm->get("translate.button"));
    connect(translateAction, &QAction::triggered, this, [this]() {
        performTranslate();
    });

    // 添加保存菜单项
    QAction *saveAction = menu.addAction(tm->get("pinwindow.save", "Save"));
    connect(saveAction, &QAction::triggered, this, [this]() {
        saveToFile();
    });

    // 显示菜单
    menu.exec(event->globalPos());
    }

/**
 * @brief 移动事件处理，标注工具栏跟随
 * @param event 移动事件
 * @author chiangyang
 */
void PinWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (m_toolBar && m_toolBar->isVisible()) {
        m_toolBar->positionNearPinWindow(this);
    }
}

/**
 * @brief 调整大小事件处理，标注工具栏跟随
 * @param event 调整大小事件
 * @author chiangyang
 */
void PinWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_toolBar && m_toolBar->isVisible()) {
        m_toolBar->positionNearPinWindow(this);
    }
}

/**
 * @brief 关闭事件处理，清理标注工具栏与文本编辑框
 * @param event 关闭事件
 * @author chiangyang
 */
void PinWindow::closeEvent(QCloseEvent *event) {
    s_instances.remove(this);  // 从注册表移除，避免 toggleAll 访问已关闭窗口
    QWidget::closeEvent(event);
    if (m_textEdit) {
        m_textEdit->hide();
        m_textEdit->deleteLater();
        m_textEdit = nullptr;
    }
    if (m_toolBar) {
        m_toolBar->close();
        m_toolBar->deleteLater();
        m_toolBar = nullptr;
    }
}

/**
 * @brief 复制图片到剪贴板（含标注）
 * @author chiangyang
 */
void PinWindow::copyToClipboard() {
    QPixmap pix = compositePixmap();
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setPixmap(pix);
    
    // 记录截图到历史（供 Alt+P 翻页）
    if (HistoryManager::instance()->isScreenshotEnabled() && !pix.isNull()) {
        HistoryManager::instance()->addScreenshotPixmap(pix, QApplication::applicationName());
    }
    
    LOG_INFO("Image copied to clipboard");
}

/**
 * @brief 保存图片到文件（含标注）
 * @author chiangyang
 */
void PinWindow::saveToFile() {
    // 生成默认文件名：QuickShot_Capture_yyyyMMdd_HHmmss.png
    QString defaultName = QString("QuickShot_Capture_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    Utils::savePixmapToFile(
        this, [this]() -> QPixmap { return compositePixmap(); }, defaultName,
        QStringLiteral("保存图片"),
        QStringLiteral("PNG图片 (*.png);;JPEG图片 (*.jpg);;所有文件 (*.*)"));
}

/**
 * @brief 执行翻译流程：OCR 识别 → 批量翻译 → 显示译文叠加窗口
 * @author chiangyang
 */
void PinWindow::performTranslate() {
    if (m_pixmap.isNull()) return;

    // 检查翻译功能是否启用 + 首次隐私提示
    if (!TranslateService::checkEnabledAndPrivacy(this)) {
        LOG_INFO("PinWindow: Translate aborted: disabled or declined privacy warning");
        return;
    }

    TranslationManager *tm = TranslationManager::instance();

    // 显示"识别中"加载提示（居中在 PinWindow）
    if (m_ocrLoadingLabel) {
        m_ocrLoadingLabel->hide();
        m_ocrLoadingLabel->deleteLater();
    }
    m_ocrLoadingLabel = new QLabel(tm->get("ocr.recognizing"), this);
    m_ocrLoadingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    m_ocrLoadingLabel->setAlignment(Qt::AlignCenter);
    m_ocrLoadingLabel->adjustSize();
    m_ocrLoadingLabel->move((width() - m_ocrLoadingLabel->width()) / 2,
                            (height() - m_ocrLoadingLabel->height()) / 2);
    m_ocrLoadingLabel->show();

    LOG_INFO("PinWindow: Translate requested, starting OCR");

    // 异步执行 OCR 识别
    auto *watcher = new QFutureWatcher<OcrEngine::OcrResult>(this);
    connect(watcher, &QFutureWatcher<OcrEngine::OcrResult>::finished, this, [this, watcher]() {
        if (m_ocrLoadingLabel) {
            m_ocrLoadingLabel->hide();
            m_ocrLoadingLabel->deleteLater();
            m_ocrLoadingLabel = nullptr;
        }

        OcrEngine::OcrResult result = watcher->result();
        // 释放 OCR 模型资源，下次识别时重新初始化
        OcrEngine::instance()->release();

        if (result.texts.isEmpty()) {
            // 无识别文本
            QLabel *noText = new QLabel(TranslationManager::instance()->get("ocr.noText"), this);
            noText->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
            noText->setAlignment(Qt::AlignCenter);
            noText->adjustSize();
            noText->move((width() - noText->width()) / 2, (height() - noText->height()) / 2);
            noText->show();
            QTimer::singleShot(2000, noText, &QWidget::deleteLater);
            watcher->deleteLater();
            return;
        }

        // 批量翻译并显示译文叠加窗口（封装了标签提示、信号连接、错误处理）
        TranslateOverlayWindow::translateAndShow(
            this, m_pixmap, result.texts, result.polygons,
            pos(), rect());

        LOG_INFO(QString("PinWindow: OCR done, %1 segments, starting batch translation")
                     .arg(result.texts.size()));

        watcher->deleteLater();
    });

    QImage image = m_pixmap.toImage();
    QFuture<OcrEngine::OcrResult> future = QtConcurrent::run([image]() {
        return OcrEngine::instance()->recognize(image);
    });
    watcher->setFuture(future);
}

// ============================================================
// 标注模式
// ============================================================

/**
 * @brief 进入标注模式
 *
 * 懒创建标注工具栏并显示在 PinWindow 下边界贴边位置，连接工具栏信号，
 * 并注册 ESC 快捷键（工具栏获得焦点时也能退出标注模式）。
 * @author chiangyang
 */
void PinWindow::enterAnnotationMode() {
    if (m_annotationMode) {
        // 已在标注模式：确保工具栏可见并重新定位
        if (m_toolBar) {
            m_toolBar->positionNearPinWindow(this);
            m_toolBar->show();
            m_toolBar->raise();
        }
        return;
    }
    m_annotationMode = true;

    if (!m_toolBar) {
        m_toolBar = new PinAnnotationToolBar(nullptr); // 独立顶层窗口
        connectToolBarSignals();
    }

    // 创建标注快捷键控制器（策略模式：PinWindow 作为 IShortcutHandler 实现）
    // 统一用 QShortcut 注册标注快捷键，彻底解决 keyPressEvent 焦点时序 bug
    // （原先工具栏 activateWindow 后数字键 1-8/Tab 等不触发 PinWindow::keyPressEvent）
    if (!m_annotationController) {
        m_annotationController = new AnnotationShortcutController(this, this);
    }

    m_toolBar->positionNearPinWindow(this);
    m_toolBar->show();
    m_toolBar->raise();
    m_toolBar->activateWindow();
    // 工具栏 activateWindow 后活动窗口变为工具栏（独立顶层窗口），
    // 导致 PinWindow 的 QShortcut（WindowShortcut context）失效，
    // 数字键 1-8/Tab 等无法触发。延迟到事件循环恢复后再激活 PinWindow 焦点，
    // 使 PinWindow 重新成为活动窗口，QShortcut 生效。
    QTimer::singleShot(0, this, [this]() {
        this->activateWindow();
        this->setFocus();
    });

    LOG_INFO("PinWindow: Entered annotation mode");
    update();
}

/**
 * @brief 退出标注模式
 *
 * 隐藏标注工具栏与子工具栏，取消所有工具按钮选中状态。
 * 标注痕迹保留，paintEvent 继续绘制；复制/保存仍含标注。
 * @author chiangyang
 */
void PinWindow::exitAnnotationMode() {
    if (!m_annotationMode) {
        return;
    }
    m_annotationMode = false;
    m_annotationToolSelected = false;

    // 完成可能正在进行的文本编辑
    if (m_textEdit) {
        finalizeTextEdit();
    }

    if (m_toolBar) {
        // 隐藏子工具栏（取消选中按钮时不会发信号，需显式隐藏）
        QWidget *sub = m_toolBar->getSubToolbarWindow();
        if (sub) {
            sub->hide();
        }
        m_toolBar->uncheckAllAnnotationBtns();
        m_toolBar->hide();
    }

    // 销毁标注快捷键控制器（QShortcut 随父对象 this 自动释放，这里只删控制器本身）
    if (m_annotationController) {
        delete m_annotationController;
        m_annotationController = nullptr;
    }

    // 重置标注交互状态
    m_annotationHandler->exitAnnotation();
    setCursor(Qt::ArrowCursor);

    LOG_INFO("PinWindow: Exited annotation mode (annotations retained)");
    update();
}

/**
 * @brief 连接标注工具栏信号（仅在工具栏首次创建时调用一次）
 * @author chiangyang
 */
void PinWindow::connectToolBarSignals() {
    // 工具选择
    connect(m_toolBar, &PinAnnotationToolBar::toolSelected, this, [this](int toolId) {
        m_annotationHandler->setTool(static_cast<AnnotationType>(toolId));
        m_annotationToolSelected = true;
        LOG_INFO(QString("[PinWindow] Annotation tool selected: ToolId=%1").arg(toolId));
        // 完成可能正在进行的文本编辑
        if (m_textEdit) {
            finalizeTextEdit();
        }
        // 重新聚焦 PinWindow 以便绘制
        setFocus();
    });
    connect(m_toolBar, &PinAnnotationToolBar::annotationToolDeselected, this, [this]() {
        m_annotationToolSelected = false;
        if (m_textEdit) {
            finalizeTextEdit();
        }
        setCursor(Qt::ArrowCursor);
        LOG_INFO("[PinWindow] Annotation tool deselected");
    });

    // 画笔样式
    connect(m_toolBar, &PinAnnotationToolBar::shapeTypeChanged, this, [this](int type) {
        m_annotationHandler->setShapeType(type);
        LOG_INFO(QString("[PinWindow] Shape type changed: %1").arg(type));
    });
    connect(m_toolBar, &PinAnnotationToolBar::penColorChanged, this, [this](const QColor &color) {
        m_annotationHandler->setColor(color);
    });
    connect(m_toolBar, &PinAnnotationToolBar::penWidthChanged, this, [this](int width) {
        m_annotationHandler->setPenWidth(width);
    });
    connect(m_toolBar, &PinAnnotationToolBar::fontSizeChanged, this, [this](int size) {
        m_annotationHandler->setFontSize(size);
    });

    // 连接 SettingsWindow 的默认值变更信号（设置窗口 -> 工具栏）
    SettingsWindow *settingsWindow = qApp->findChild<SettingsWindow*>();
    if (settingsWindow) {
        connect(settingsWindow, &SettingsWindow::defaultPenWidthChanged,
                m_toolBar, &BaseToolBar::refreshDefaultValues);
        connect(settingsWindow, &SettingsWindow::defaultFontSizeChanged,
                m_toolBar, &BaseToolBar::refreshDefaultValues);
        connect(settingsWindow, &SettingsWindow::defaultEraserWidthChanged,
                m_toolBar, &BaseToolBar::refreshDefaultValues);
        connect(settingsWindow, &SettingsWindow::defaultMosaicSizeChanged,
                m_toolBar, &BaseToolBar::refreshDefaultValues);

        // 工具栏 -> 设置窗口：同步工具栏滑块变更到设置窗口显示
        connect(m_toolBar, &PinAnnotationToolBar::toolPenWidthChanged,
                settingsWindow, &SettingsWindow::onToolPenWidthChanged);
        connect(m_toolBar, &PinAnnotationToolBar::toolFontSizeChanged,
                settingsWindow, &SettingsWindow::onToolFontSizeChanged);
        connect(m_toolBar, &PinAnnotationToolBar::toolEraserWidthChanged,
                settingsWindow, &SettingsWindow::onToolEraserWidthChanged);
        connect(m_toolBar, &PinAnnotationToolBar::toolMosaicSizeChanged,
                settingsWindow, &SettingsWindow::onToolMosaicSizeChanged);

        LOG_INFO("PinWindow: Connected to SettingsWindow default value signals");
    }

    // 撤销/重做/清除
    connect(m_toolBar, &PinAnnotationToolBar::undoRequested, this, [this]() {
        m_annotationHandler->manager().undo();
        updateToolBarState();
        update();
        LOG_INFO("[PinWindow] Annotation undo");
    });
    connect(m_toolBar, &PinAnnotationToolBar::redoRequested, this, [this]() {
        m_annotationHandler->manager().redo();
        updateToolBarState();
        update();
        LOG_INFO("[PinWindow] Annotation redo");
    });
    connect(m_toolBar, &PinAnnotationToolBar::clearRequested, this, [this]() {
        m_annotationHandler->manager().clear();
        updateToolBarState();
        update();
        LOG_INFO("[PinWindow] Annotations cleared");
    });

    // 复制/保存/取消
    connect(m_toolBar, &PinAnnotationToolBar::copyRequested, this, [this]() {
        copyToClipboard();
    });
    connect(m_toolBar, &PinAnnotationToolBar::saveRequested, this, [this]() {
        saveToFile();
    });
    connect(m_toolBar, &PinAnnotationToolBar::cancelRequested, this, [this]() {
        exitAnnotationMode();
    });

    // 语言切换时更新工具栏文字（context 为 PinWindow，窗口销毁时自动断开）
    connect(TranslationManager::instance(), &TranslationManager::languageChanged,
            this, [this](const QString &) {
        if (m_toolBar) {
            m_toolBar->retranslateUi();
        }
    });
}

/**
 * @brief 创建文本编辑框
 *
 * 在指定位置（窗口本地坐标）创建 OverlayTextEdit 用于输入文本标注。
 * OverlayTextEdit 作为 PinWindow 子控件，pos() 直接返回窗口本地坐标。
 * @param pos 文本框左上角位置（窗口本地坐标）
 * @author chiangyang
 */
void PinWindow::createTextEdit(const QPoint &pos) {
    m_textEdit = new OverlayTextEdit(this);
    m_textEdit->setGeometry(QRect(pos, QSize(200, 50)));
    m_textEdit->setEditorColor(m_annotationHandler->color());
    m_textEdit->setFontSize(m_annotationHandler->fontSize());
    // 限制文本框移动和缩放范围在 PinWindow 内
    m_textEdit->setBoundaryRect(rect());
    connect(m_textEdit, &OverlayTextEdit::closeRequested, this, [this]() {
        finalizeTextEdit();
        update();
    });
    m_textEdit->show();
    m_textEdit->setFocus();
    // 文本编辑框获得焦点：禁用与文本输入冲突的标注快捷键（数字键/[]/Tab/Delete等裸键）
    if (m_annotationController) {
        m_annotationController->setBareKeysEnabled(false);
    }
}

/**
 * @brief 完成文本编辑，将文本内容创建为标注
 *
 * 与 SnipScreen 版本的差异：widgetPos 直接使用 m_textEdit->pos()（已是窗口本地坐标，
 * 无需 + m_virtualGeometry.topLeft()）。若文本为空则不创建标注。
 * @author chiangyang
 */
void PinWindow::finalizeTextEdit() {
    if (!m_textEdit) {
        return;
    }

    QString text = m_textEdit->toPlainText().trimmed();
    LOG_INFO(QString("[PinWindow] finalizeTextEdit: text='%1', isEmpty=%2")
        .arg(text).arg(text.isEmpty()));

    if (!text.isEmpty()) {
        // 计算文本在 OverlayTextEdit 中的实际渲染位置（基线坐标）
        // OverlayTextEdit 是 PinWindow 的子控件，pos() 返回窗口本地坐标
        QPoint widgetPos = m_textEdit->pos();
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
        LOG_INFO(QString("[PinWindow] Text rotation: %1 degrees").arg(rotation));

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

            // 旋转后的窗口本地坐标
            textPos = widgetCenter + QPoint(rotatedDx, rotatedDy);
        } else {
            // 无旋转时的正常位置
            textPos = QPoint(widgetPos.x() + textOffsetX, widgetPos.y() + textOffsetY);
        }

        LOG_INFO(QString("[PinWindow] Text annotation pos: widget=(%1,%2) offset=(%3,%4) rotation=%5 -> final=(%6,%7)")
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
        updateToolBarState();
    }

    m_textEdit->hide();
    m_textEdit->deleteLater();
    m_textEdit = nullptr;
    // 文本编辑框关闭：恢复标注快捷键
    if (m_annotationController) {
        m_annotationController->setBareKeysEnabled(true);
    }
    update();
    LOG_INFO("[PinWindow] Text annotation finalized");
}

/**
 * @brief 合成"所见即所得"图（窗口尺寸画拉伸原图 + 标注 + 马赛克）
 *
 * 无标注时返回原图（最高质量）；有标注时返回窗口尺寸的合成图，
 * 保证复制/保存结果与屏幕显示一致。
 * @return 合成图
 * @author chiangyang
 */
QPixmap PinWindow::compositePixmap() {
    // 无标注：返回原图（最高质量）
    if (!m_annotationHandler->manager().hasAnnotations() && !m_annotationHandler->manager().hasMosaicStrokes()) {
        return m_pixmap;
    }

    // 有标注：合成窗口尺寸的所见即所得图
    QPixmap result(size());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    bool scaled = (width() != m_pixmap.width()) || (height() != m_pixmap.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scaled);
    painter.drawPixmap(rect(), m_pixmap, QRectF(m_pixmap.rect()));

    if (m_annotationHandler->manager().hasMosaicStrokes()) {
        // 有马赛克：离屏 canvas（拉伸原图 + 标注）再像素化马赛克区域
        QPixmap canvas(size());
        canvas.fill(Qt::transparent);
        QPainter cp(&canvas);
        cp.setRenderHint(QPainter::SmoothPixmapTransform, scaled);
        cp.drawPixmap(rect(), m_pixmap, QRectF(m_pixmap.rect()));
        if (m_annotationHandler->manager().hasAnnotations()) {
            m_annotationHandler->manager().draw(cp);
        }
        m_annotationHandler->manager().drawMosaic(cp, canvas, AnnotationManager::kDefaultMosaicBlockSize, QPoint());
        cp.end();
        painter.drawPixmap(0, 0, canvas);
    } else {
        // 无马赛克：直接绘制标注
        m_annotationHandler->manager().draw(painter);
    }
    painter.end();
    return result;
}

/**
 * @brief 更新标注工具栏按钮状态（撤销/重做/清除启用状态）
 * @author chiangyang
 */
void PinWindow::updateToolBarState() {
    if (m_toolBar) {
        m_toolBar->updateState(true, m_annotationHandler->manager().canUndo(), m_annotationHandler->manager().canRedo());
    }
}

// ============================================================
// IShortcutHandler 接口实现（标注快捷键策略）
// ============================================================
// 原先 keyPressEvent 中的标注分支（Ctrl+C/S/Z/Y、数字键 1-8、Tab、[]、Delete/Backspace、ESC）
// 已全部删除，由 AnnotationShortcutController 通过 QShortcut 统一接管，
// 彻底解决工具栏 activateWindow 后焦点不在 PinWindow 导致快捷键失效的时序 bug。

/**
 * @brief 判断当前是否可进行标注操作
 * @return 处于标注模式时返回 true
 * @author chiangyang
 */
bool PinWindow::canAnnotate() const {
    return m_annotationMode;
}

/**
 * @brief 切换标注工具（数字键 1-8 触发）
 * @param toolId 工具 ID（0-7，对应 AnnotationType 枚举值）
 * @author chiangyang
 */
void PinWindow::onToolSwitch(int toolId) {
    if (m_toolBar) {
        m_toolBar->selectAnnotationTool(toolId);
    }
}

/**
 * @brief 复制到剪贴板（Ctrl+C 触发）
 * @author chiangyang
 */
void PinWindow::onCopy() {
    copyToClipboard();
}

/**
 * @brief 保存到文件（Ctrl+S 触发）
 * @author chiangyang
 */
void PinWindow::onSave() {
    saveToFile();
}

/**
 * @brief 撤销标注（Ctrl+Z 触发）
 * @author chiangyang
 */
void PinWindow::onUndo() {
    m_annotationHandler->manager().undo();
    updateToolBarState();
    update();
}

/**
 * @brief 重做标注（Ctrl+Y / Ctrl+Shift+Z 触发）
 * @author chiangyang
 */
void PinWindow::onRedo() {
    m_annotationHandler->manager().redo();
    updateToolBarState();
    update();
}

/**
 * @brief 清除所有标注（Delete / Backspace 触发）
 * @author chiangyang
 */
void PinWindow::onClear() {
    m_annotationHandler->manager().clear();
    updateToolBarState();
    update();
}

/**
 * @brief 调整画笔宽度（[ / ] 触发，范围 1-20）
 * @param delta 宽度增量（+1 增加，-1 减少）
 * @author chiangyang
 */
void PinWindow::onPenWidthChange(int delta) {
    if (m_toolBar) {
        m_toolBar->setCurrentPenWidth(qBound(AnnotationShortcutController::kMinPenWidth,
                                             m_annotationHandler->penWidth() + delta,
                                             AnnotationShortcutController::kMaxPenWidth));
    }
}

/**
 * @brief 循环切换颜色（Tab 触发）
 * @author chiangyang
 */
void PinWindow::onCycleColor() {
    if (m_toolBar) {
        m_toolBar->selectNextColor();
    }
}

/**
 * @brief 取消当前操作（Esc 触发）
 *
 * 优先级：取消文本编辑 → 退出标注模式 → 关闭窗口
 * @author chiangyang
 */
void PinWindow::onCancel() {
    if (m_textEdit) {
        // 取消文本编辑，不创建标注
        finalizeTextEdit();
        update();
    } else if (m_annotationMode) {
        // 标注模式：ESC 退出标注模式（窗口不关）
        exitAnnotationMode();
    } else {
        // 非标注模式：ESC 关闭窗口
        close();
    }
}
