#include "TranslateOverlayWindow.h"
#include "../translate/TranslateService.h"
#include "../translate/TranslateEngine.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../capture/Resizer.h"
#include "../utils/Utils.h"
#include "../log/Logger.h"
#include <QPainter>
#include <QFont>
#include <QTextOption>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QDateTime>
#include <QTimer>
#include <QLabel>
#include <QTextEdit>
#include <QResizeEvent>
#include <memory>

/**
 * @brief 一站式批量翻译并显示译文叠加窗口
 * @param parent 父窗口（用于创建标签和信号接收者）
 * @param pixmap 截图原图（用于 Overlay 背景显示）
 * @param texts 原文文本列表
 * @param polygons 原文区域多边形（像素坐标，相对截图图像）
 * @param overlayPos Overlay 窗口定位点（全局坐标）
 * @param labelRect 加载/错误标签的居中区域（parent 相对坐标）
 * @param onOverlayShown 翻译成功并显示 Overlay 后的回调（可选，如 SnipScreen 退出截图框）
 * @author chiangyang
 */
void TranslateOverlayWindow::translateAndShow(QWidget *parent,
                                              const QPixmap &pixmap,
                                              const QStringList &texts,
                                              const QVector<QPolygonF> &polygons,
                                              const QPoint &overlayPos,
                                              const QRect &labelRect,
                                              std::function<void()> onOverlayShown) {
    if (!parent) return;

    TranslationManager *tm = TranslationManager::instance();

    // 创建"翻译中"加载提示标签（居中在 labelRect）
    QLabel *translatingLabel = new QLabel(tm->get("translate.translating", "Translating..."), parent);
    translatingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    translatingLabel->setAlignment(Qt::AlignCenter);
    translatingLabel->adjustSize();
    translatingLabel->move(labelRect.x() + (labelRect.width() - translatingLabel->width()) / 2,
                           labelRect.y() + (labelRect.height() - translatingLabel->height()) / 2);
    translatingLabel->show();

    LOG_INFO(QString("TranslateOverlayWindow: batch translation started, %1 segments").arg(texts.size()));

    // 连接 TranslateService 信号（一次性连接，任一信号触发后自动断开两者）
    TranslateService *ts = TranslateService::instance();
    auto connFinish = std::make_shared<QMetaObject::Connection>();
    auto connFail = std::make_shared<QMetaObject::Connection>();

    *connFinish = QObject::connect(ts, &TranslateService::batchFinished, parent,
            [pixmap, texts, polygons, overlayPos, labelRect, translatingLabel, connFinish, connFail, onOverlayShown](
                const QStringList &translatedTexts) {
        QObject::disconnect(*connFinish);
        QObject::disconnect(*connFail);

        translatingLabel->hide();
        translatingLabel->deleteLater();

        auto *overlay = new TranslateOverlayWindow(pixmap, texts, polygons, translatedTexts);
        overlay->move(overlayPos);
        overlay->show();

        LOG_INFO("TranslateOverlayWindow: overlay shown after batch translation");

        // 翻译成功显示 Overlay 后触发回调（如 SnipScreen 退出截图框，类似贴图完成后销毁截图框）
        if (onOverlayShown) {
            onOverlayShown();
        }
    });

    *connFail = QObject::connect(ts, &TranslateService::failed, parent,
            [parent, labelRect, translatingLabel, connFinish, connFail](
                TranslateEngine::TranslateError code, const QString &detail) {
        QObject::disconnect(*connFinish);
        QObject::disconnect(*connFail);

        translatingLabel->hide();
        translatingLabel->deleteLater();

        // 根据错误码显示本地化错误提示（3秒后自动消失）
        QString errMsg = TranslateService::errorMessage(code);

        QLabel *errLabel = new QLabel(errMsg, parent);
        errLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
        errLabel->setAlignment(Qt::AlignCenter);
        errLabel->adjustSize();
        errLabel->move(labelRect.x() + (labelRect.width() - errLabel->width()) / 2,
                       labelRect.y() + (labelRect.height() - errLabel->height()) / 2);
        errLabel->show();
        QTimer::singleShot(3000, errLabel, &QWidget::deleteLater);

        LOG_WARNING(QString("TranslateOverlayWindow: translate failed, code=%1, detail=%2")
                        .arg(static_cast<int>(code)).arg(detail));
    });

    ts->translateBatch(texts);
}

/**
 * @brief 构造函数
 * @param pixmap 截图原图
 * @param texts 原文文本列表
 * @param polygons 原文区域多边形
 * @param translatedTexts 译文文本列表
 * @param parent 父对象
 * @author chiangyang
 */
TranslateOverlayWindow::TranslateOverlayWindow(const QPixmap &pixmap,
                                               const QStringList &texts,
                                               const QVector<QPolygonF> &polygons,
                                               const QStringList &translatedTexts,
                                               QWidget *parent)
    : QWidget(parent)
    , m_pixmap(pixmap)
    , m_texts(texts)
    , m_polygons(polygons)
    , m_translatedTexts(translatedTexts)
    , m_viewMode(Translation) {
    LOG_INFO(QString("TranslateOverlayWindow created, pixmap: %1x%2, segments: %3")
                 .arg(pixmap.width()).arg(pixmap.height()).arg(texts.size()));

    // 窗口标志：无边框、置顶、工具窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFocus();

    // 窗口初始大小等于原图大小
    resize(pixmap.size());
}

/**
 * @brief 设置视图模式
 * @param mode 视图模式
 * @author chiangyang
 */
void TranslateOverlayWindow::setViewMode(ViewMode mode) {
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    update();
    LOG_INFO(QString("TranslateOverlayWindow: view mode changed to %1").arg(static_cast<int>(mode)));
}

/**
 * @brief 获取当前视图模式
 * @return 当前视图模式
 * @author chiangyang
 */
TranslateOverlayWindow::ViewMode TranslateOverlayWindow::viewMode() const {
    return m_viewMode;
}

/**
 * @brief 切换文字选择模式
 *
 * 开启时创建一个覆盖整个窗口的只读 QTextEdit，包含所有译文文本，
 * 用户可自由跨行跨段选择文字并 Ctrl+C 复制。
 * 关闭时移除编辑器，恢复 QPainter 绘制。
 * @param enabled true 进入选择模式，false 退出
 * @author chiangyang
 */
void TranslateOverlayWindow::setTextSelectionMode(bool enabled) {
    if (m_textSelectionMode == enabled) return;

    m_textSelectionMode = enabled;

    if (enabled) {
        // Original 模式下没有译文可选，自动切换到 Translation 模式
        if (m_viewMode == Original) {
            m_viewMode = Translation;
        }

        // 创建覆盖整个窗口的只读 QTextEdit，所有译文合在一起，支持跨段选择
        m_selectEditor = new QTextEdit(this);
        m_selectEditor->setReadOnly(true);
        m_selectEditor->setTextInteractionFlags(
            Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_selectEditor->setFocusPolicy(Qt::StrongFocus);
        m_selectEditor->setGeometry(rect());
        // 安装事件过滤器：拦截 QTextEdit viewport 的右键菜单，转发本窗口的完整右键菜单
        // （QAbstractScrollArea 的右键事件发送给 viewport() 而非 QTextEdit 本身，
        //  否则进入选字模式后无法呼出"退出选字模式"选项）
        m_selectEditor->viewport()->installEventFilter(this);

        // 所有译文按段拼接，段间空行分隔
        m_selectEditor->setPlainText(m_translatedTexts.join("\n\n"));

        // 半透明白色背景 + 深色文字
        // padding 用 em、font-size 用 pt，随屏幕 DPI 自动缩放
        m_selectEditor->setStyleSheet(
            "QTextEdit { background-color: rgba(255, 255, 255, 230); "
            "color: rgb(30, 30, 30); border: none; padding: 0.5em; "
            "font-size: 11pt; }");

        m_selectEditor->show();
        m_selectEditor->setFocus();

        LOG_INFO(QString("TranslateOverlayWindow: text selection mode enabled, %1 segments")
                     .arg(m_translatedTexts.size()));
    } else {
        if (m_selectEditor) {
            m_selectEditor->hide();
            m_selectEditor->deleteLater();
            m_selectEditor = nullptr;
        }

        LOG_INFO("TranslateOverlayWindow: text selection mode disabled");
    }

    update();
}

/**
 * @brief 更新选择模式编辑器的大小（窗口缩放后调用）
 * @author chiangyang
 */
void TranslateOverlayWindow::updateSelectEditorGeometry() {
    if (m_selectEditor) {
        m_selectEditor->setGeometry(rect());
    }
}

/**
 * @brief 窗口大小变化事件：重新定位选择模式标签
 * @param event 大小变化事件
 * @author chiangyang
 */
void TranslateOverlayWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateSelectEditorGeometry();
}

/**
 * @brief 绘制事件：绘制原图与译文叠加层
 * @param event 绘制事件
 * @author chiangyang
 */
void TranslateOverlayWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::Antialiasing);

    // 将坐标系缩放到 pixmap 坐标系，使 polygon 坐标可直接使用
    const qreal sx = qreal(width()) / qreal(m_pixmap.width());
    const qreal sy = qreal(height()) / qreal(m_pixmap.height());

    painter.save();
    painter.scale(sx, sy);
    // 绘制原图
    painter.drawPixmap(0, 0, m_pixmap);
    // 选择模式下由 QLabel 显示译文，跳过 QPainter 叠加层
    if (!m_textSelectionMode) {
        drawOverlay(painter);
    }
    painter.restore();

    // 绘制边框（窗口坐标系，不缩放）
    QPen borderPen(QColor(100, 149, 237), 2);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    QRect borderRect = rect().adjusted(1, 1, -1, -1);
    if (borderRect.width() > 0 && borderRect.height() > 0) {
        painter.drawRect(borderRect);
    }

    // 绘制右下角调整大小手柄
    painter.setPen(QPen(Qt::white, 2));
    painter.drawLine(width() - 10, height(), width(), height() - 10);
    painter.drawLine(width() - 5, height(), width(), height() - 5);
}

/**
 * @brief 绘制译文叠加层（根据当前视图模式）
 * @param painter 画笔（需已设置到 pixmap 坐标系）
 * @author chiangyang
 */
void TranslateOverlayWindow::drawOverlay(QPainter &painter) {
    if (m_viewMode == Original) {
        // 原文模式：仅显示原图，不叠加
        return;
    }

    const int count = qMin(m_polygons.size(), m_translatedTexts.size());
    for (int i = 0; i < count; ++i) {
        const QRectF br = m_polygons[i].boundingRect();
        if (br.isEmpty()) continue;

        if (m_viewMode == Translation) {
            // 译文模式：在原文位置遮盖并显示译文
            drawTranslatedText(painter, m_translatedTexts[i], br);
        } else if (m_viewMode == Both) {
            // 对照模式：在原文下方追加显示译文
            drawTranslatedText(painter, m_translatedTexts[i], br.translated(0, br.height() + 2));
        }
    }
}

/**
 * @brief 在指定矩形区域绘制译文（半透明背景 + 自动换行 + 缩放字号）
 * @param painter 画笔
 * @param text 译文文本
 * @param rect 目标矩形区域
 * @author chiangyang
 */
void TranslateOverlayWindow::drawTranslatedText(QPainter &painter, const QString &text, const QRectF &rect) {
    if (text.isEmpty() || rect.isEmpty()) return;

    // 半透明白色背景遮盖原文
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawRoundedRect(rect, 2, 2);

    // 译文文字：深色，字号根据区域高度自动缩放
    painter.setPen(QColor(30, 30, 30));
    QFont font = painter.font();
    const int fontSize = qBound(8, int(rect.height() * 0.75), 22);
    font.setPixelSize(fontSize);
    painter.setFont(font);

    // 在矩形内居中绘制，自动换行
    const QRectF textRect = rect.adjusted(2, 1, -2, -1);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignCenter);
    painter.drawText(textRect, text, option);
}

/**
 * @brief 检查鼠标是否在右下角调整大小区域内
 * @param pos 鼠标位置（相对窗口）
 * @return 是否在调整大小区域内
 * @author chiangyang
 */
bool TranslateOverlayWindow::isInResizeArea(const QPoint &pos) const {
    return (pos.x() >= width() - 15 && pos.y() >= height() - 15);
}

/**
 * @brief 鼠标按下事件：开始拖动或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void TranslateOverlayWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        setFocus();
        if (isInResizeArea(event->pos())) {
            m_isResizing = true;
        } else if (!m_textSelectionMode) {
            // 选择模式下不启动窗口拖动，让 QLabel 处理鼠标事件
            m_isMoving = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
    }
}

/**
 * @brief 鼠标移动事件：拖动窗口或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void TranslateOverlayWindow::mouseMoveEvent(QMouseEvent *event) {
    const QPoint pos = event->pos();

    // 更新鼠标光标
    if (isInResizeArea(pos)) {
        setCursor(getCursorByLocation(ResizerLocation::BR_ANCHOR));
    } else {
        setCursor(getCursorByLocation(ResizerLocation::EMPTY_INSIDE));
    }

    if (m_isResizing) {
        const int newWidth = qMax(50, pos.x());
        const int newHeight = qMax(50, pos.y());
        resize(newWidth, newHeight);
    } else if (m_isMoving) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    }
}

/**
 * @brief 鼠标释放事件：结束拖动或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void TranslateOverlayWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isMoving = false;
        m_isResizing = false;
    }
}

/**
 * @brief 鼠标双击事件：关闭窗口
 * @param event 鼠标事件
 * @author chiangyang
 */
void TranslateOverlayWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        event->accept();
        hide();
        QTimer::singleShot(50, this, &TranslateOverlayWindow::close);
    }
}

/**
 * @brief 鼠标滚轮事件：缩放窗口
 * @param event 滚轮事件
 * @author chiangyang
 */
void TranslateOverlayWindow::wheelEvent(QWheelEvent *event) {
    const double scaleFactor = 1.1;

    if (event->angleDelta().y() > 0) {
        resize(int(width() * scaleFactor), int(height() * scaleFactor));
    } else {
        int newWidth = int(width() / scaleFactor);
        int newHeight = int(height() / scaleFactor);
        newWidth = qMax(50, newWidth);
        newHeight = qMax(50, newHeight);
        resize(newWidth, newHeight);
    }
    // 缩放后更新选择模式编辑器大小（resizeEvent 也会触发，此处冗余但确保即时更新）
    updateSelectEditorGeometry();
}

/**
 * @brief 键盘按键事件：ESC 关闭窗口
 * @param event 键盘事件
 * @author chiangyang
 */
void TranslateOverlayWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_textSelectionMode) {
            // 选择模式下 ESC 先退出选择模式，不关闭窗口
            setTextSelectionMode(false);
        } else {
            close();
        }
    }
}

/**
 * @brief 右键菜单事件：视图切换、复制、另存、关闭
 * @param event 右键菜单事件
 * @author chiangyang
 */
void TranslateOverlayWindow::contextMenuEvent(QContextMenuEvent *event) {
    TranslationManager *tm = TranslationManager::instance();

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(StyleManager::getMenuStyle());

    // 视图模式切换（单选）
    QMenu *viewMenu = menu.addMenu(tm->get("translate.viewMode", "View"));
    QActionGroup *viewGroup = new QActionGroup(&menu);
    viewGroup->setExclusive(true);

    QAction *actOriginal = viewMenu->addAction(tm->get("translate.viewOriginal"));
    actOriginal->setCheckable(true);
    actOriginal->setChecked(m_viewMode == Original);
    viewGroup->addAction(actOriginal);

    QAction *actTranslation = viewMenu->addAction(tm->get("translate.viewTranslation"));
    actTranslation->setCheckable(true);
    actTranslation->setChecked(m_viewMode == Translation);
    viewGroup->addAction(actTranslation);

    QAction *actBoth = viewMenu->addAction(tm->get("translate.viewBoth"));
    actBoth->setCheckable(true);
    actBoth->setChecked(m_viewMode == Both);
    viewGroup->addAction(actBoth);

    connect(actOriginal, &QAction::triggered, this, [this]() { setViewMode(Original); });
    connect(actTranslation, &QAction::triggered, this, [this]() { setViewMode(Translation); });
    connect(actBoth, &QAction::triggered, this, [this]() { setViewMode(Both); });

    menu.addSeparator();

    // 选择文字模式（可勾选）
    QAction *selectTextAction = menu.addAction(tm->get("translate.selectText", "Select Text"));
    selectTextAction->setCheckable(true);
    selectTextAction->setChecked(m_textSelectionMode);
    connect(selectTextAction, &QAction::triggered, this, [this](bool checked) {
        setTextSelectionMode(checked);
    });

    menu.addSeparator();

    // 复制原文
    QAction *copyOriginalAction = menu.addAction(tm->get("translate.copyOriginal", "Copy Original"));
    connect(copyOriginalAction, &QAction::triggered, this, &TranslateOverlayWindow::copyOriginal);

    // 复制译文
    QAction *copyTranslationAction = menu.addAction(tm->get("translate.copyTranslation"));
    connect(copyTranslationAction, &QAction::triggered, this, &TranslateOverlayWindow::copyTranslation);

    // 另存为图片
    QAction *saveAction = menu.addAction(tm->get("translate.saveAsImage", "Save as Image"));
    connect(saveAction, &QAction::triggered, this, &TranslateOverlayWindow::saveAsImage);

    menu.addSeparator();

    // 关闭
    QAction *closeAction = menu.addAction(tm->get("cancel"));
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    menu.exec(event->globalPos());
}

/**
 * @brief 事件过滤器：为选择模式编辑器的右键菜单做本地化并追加退出选项
 *
 * 选字模式下覆盖窗口的 QTextEdit 默认右键菜单只有 Copy/Select All（且跟随
 * Qt 框架语言，可能显示英文），缺少退出选字模式的入口。由于 QTextEdit 继承
 * 自 QAbstractScrollArea，右键事件发送给其 viewport() 而非 QTextEdit 本身，
 * 因此过滤器需安装在 viewport() 上。此处拦截 QEvent::ContextMenu，基于
 * createStandardContextMenu() 保留原生 Copy/Select All（可复制用户选中的文字）
 * 并按快捷键识别这两项做本地化替换，再追加"文字选择模式"勾选项（取消勾选
 * 退出选字模式回到 Overlay 绘制界面）和"取消"项（退出选字模式并关闭 Overlay）。
 * @param watched 被监听的对象（m_selectEditor->viewport()）
 * @param event 事件
 * @return true 表示已处理（仅对 ContextMenu 事件），false 表示继续传递
 * @author chiangyang
 */
bool TranslateOverlayWindow::eventFilter(QObject *watched, QEvent *event) {
    if (m_selectEditor && watched == m_selectEditor->viewport()
        && event->type() == QEvent::ContextMenu) {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        // 基于 QTextEdit 标准菜单（Copy/Select All 等，可复制用户选中的文字）
        QMenu *menu = m_selectEditor->createStandardContextMenu();
        menu->setStyleSheet(StyleManager::getMenuStyle());
        TranslationManager *tm = TranslationManager::instance();

        // 按快捷键识别 Copy / Select All 并替换为本地化文本
        const QKeySequence copySeq(QKeySequence::Copy);
        const QKeySequence selectAllSeq(QKeySequence::SelectAll);
        for (QAction *a : menu->actions()) {
            const QKeySequence sc = a->shortcut();
            if (sc.matches(copySeq) == QKeySequence::ExactMatch) {
                a->setText(tm->get("translate.copy", "Copy"));
            } else if (sc.matches(selectAllSeq) == QKeySequence::ExactMatch) {
                a->setText(tm->get("translate.selectAll", "Select All"));
            }
        }

        // 追加"文字选择模式"勾选项，取消勾选即退出选字模式回到 Overlay 绘制界面
        menu->addSeparator();
        QAction *selectTextAction = menu->addAction(tm->get("translate.selectText", "Text Selection Mode"));
        selectTextAction->setCheckable(true);
        selectTextAction->setChecked(true);  // 当前处于选字模式
        connect(selectTextAction, &QAction::triggered, this, [this](bool checked) {
            setTextSelectionMode(checked);
        });

        // 追加"取消"项，退出选字模式并关闭翻译 Overlay 窗口
        QAction *cancelAction = menu->addAction(tm->get("cancel", "Cancel"));
        connect(cancelAction, &QAction::triggered, this, [this]() {
            setTextSelectionMode(false);
            close();
        });

        menu->exec(contextEvent->globalPos());
        delete menu;
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief 复制原文到剪贴板
 * @author chiangyang
 */
void TranslateOverlayWindow::copyOriginal() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_texts.join("\n"));
    LOG_INFO("TranslateOverlayWindow: original text copied to clipboard");
}

/**
 * @brief 复制译文到剪贴板
 * @author chiangyang
 */
void TranslateOverlayWindow::copyTranslation() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_translatedTexts.join("\n"));
    LOG_INFO("TranslateOverlayWindow: translated text copied to clipboard");
}

/**
 * @brief 另存为图片文件
 * @author chiangyang
 */
void TranslateOverlayWindow::saveAsImage() {
    // 将当前视图（含叠加层）渲染到 pixmap
    QPixmap result(m_pixmap.size());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawPixmap(0, 0, m_pixmap);
    drawOverlay(painter);
    painter.end();

    const QString defaultName = QString("QuickShot_Translate_%1.png")
                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    TranslationManager *tm = TranslationManager::instance();
    Utils::savePixmapToFile(
        this, [result]() -> QPixmap { return result; }, defaultName,
        tm->get("translate.saveAsImage", "Save as Image"),
        QStringLiteral("PNG图片 (*.png);;JPEG图片 (*.jpg);;所有文件 (*.*)"));

    LOG_INFO("TranslateOverlayWindow: image saved");
}
