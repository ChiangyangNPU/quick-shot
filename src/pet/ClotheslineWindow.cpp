#include "ClotheslineWindow.h"
#include "PetWindow.h"
#include "DesktopPetManager.h"
#include "../history/HistoryManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QScreen>
#include <QApplication>
#include <QTimer>
#include <QHash>
#include <QTextOption>
#include <QtMath>

namespace {
// 最多展示的卡片数量
constexpr int kMaxCards = 12;
// 卡片尺寸与排布
constexpr int kCardW = 104;
constexpr int kCardH = 96;
constexpr int kCardGap = 16;
// 绳子与窗口
constexpr int kRopeY = 30;
constexpr int kRopeSag = 16;    // 悬垂深度
constexpr int kPinLen = 10;     // 衣夹长度（锚点到卡片顶）
constexpr int kWindowH = 168;
// 边缘调整与关闭按钮
constexpr int kResizeMargin = 6;    // 上下边界调整区厚度
constexpr int kCornerSize = 18;     // 角落调整区边长
constexpr int kCloseBtnSize = 24;   // 关闭按钮边长
constexpr int kMinW = 360;          // 窗口最小宽度
constexpr int kMinH = 150;          // 窗口最小高度
// 动画
constexpr int kTickMs = 33;
constexpr qreal kSwayDeg = 3.0; // 摆动幅度（度）
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
ClotheslineWindow::ClotheslineWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(kMinW, kMinH);
    // 需要悬停光标反馈（调整区/卡片/空白区域不同光标），开启鼠标追踪
    setMouseTracking(true);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(kTickMs);
    connect(m_animTimer, &QTimer::timeout, this, &ClotheslineWindow::onTick);

    // 历史增删时实时刷新卡片
    HistoryManager *history = HistoryManager::instance();
    connect(history, &HistoryManager::itemAdded, this, &ClotheslineWindow::reload);
    connect(history, &HistoryManager::itemRemoved, this, &ClotheslineWindow::reload);
    connect(history, &HistoryManager::cleared, this, &ClotheslineWindow::reload);
}

/**
 * @brief 以主屏幕全宽显示在屏幕上方（顶部往下 1/4 高度处）
 * @author chiangyang
 */
void ClotheslineWindow::showFullWidth() {
    // 默认几何：宽度铺满主屏幕，位置在顶部往下 1/4 高度处（靠屏幕上边界）
    // 重新打开时恢复默认几何；显示期间历史刷新则保留用户拖动/调整后的几何
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect screenGeo = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    setGeometry(screenGeo.left(), screenGeo.top() + screenGeo.height() / 4,
                screenGeo.width(), kWindowH);

    reload();

    show();
    raise();
    activateWindow();

    m_showElapsed.start();
    m_animTimer->start();

    // 点击窗口外（宠物除外）关闭
    qApp->installEventFilter(this);

    LOG_INFO(QString("ClotheslineWindow: shown with %1 card(s)").arg(m_cards.size()));
}

/**
 * @brief 从 HistoryManager 重新加载最近记录并重建卡片布局
 * @author chiangyang
 */
void ClotheslineWindow::reload() {
    const QList<HistoryItem> items = HistoryManager::instance()->getItems(
        HistoryType::All, 0, kMaxCards);

    // 保留已有卡片的入场进度，避免刷新时重新播放入场动画
    QHash<qint64, qreal> oldEntrance;
    for (const Card &c : m_cards) {
        oldEntrance.insert(c.id, c.entrance);
    }

    m_cards.clear();
    for (const HistoryItem &item : items) {
        Card c;
        c.id = item.id;
        c.isImage = item.isScreenshot();
        if (c.isImage) {
            c.thumb = QPixmap(item.thumbnailPath);
            if (c.thumb.isNull()) {
                c.thumb = QPixmap(item.content);
            }
            c.text = item.content; // 原图路径，点击复制用
        } else {
            c.text = item.content;
        }
        c.entrance = oldEntrance.value(c.id, 0.0);
        m_cards.append(c);
    }

    // 布局卡片并刷新（几何由 showFullWidth()/resizeEvent 管理）
    layoutCards();
    update();
}

/**
 * @brief 重建卡片布局（锚点由绳子形状决定，依赖当前窗口宽度）
 * @author chiangyang
 */
void ClotheslineWindow::layoutCards() {
    const int cardCount = m_cards.size();
    if (cardCount <= 0) {
        return;
    }
    const qreal totalW = cardCount * kCardW + (cardCount - 1) * kCardGap;
    qreal x = (width() - totalW) / 2.0;
    for (int i = 0; i < cardCount; ++i) {
        Card &c = m_cards[i];
        const qreal cx = x + kCardW / 2.0;
        c.anchor = QPointF(cx, ropeYAt(cx));
        c.rect = QRect(int(x), int(c.anchor.y() + kPinLen), kCardW, kCardH);
        x += kCardW + kCardGap;
    }
}

/**
 * @brief 计算晾衣绳在 x 处的高度（二次贝塞尔悬垂曲线）
 * @author chiangyang
 */
qreal ClotheslineWindow::ropeYAt(qreal x) const {
    const qreal x0 = 12.0, x1 = width() - 12.0;
    const qreal t = qBound(0.0, (x - x0) / (x1 - x0), 1.0);
    // P0=(x0,kRopeY) C=(mid,kRopeY+kRopeSag*2) P1=(x1,kRopeY)
    const qreal y = (1 - t) * (1 - t) * kRopeY
                  + 2 * (1 - t) * t * (kRopeY + kRopeSag * 2)
                  + t * t * kRopeY;
    return y;
}

/**
 * @brief 动画 tick：推进摆动相位与入场进度
 * @author chiangyang
 */
void ClotheslineWindow::onTick() {
    m_phase += 0.055;

    for (int i = 0; i < m_cards.size(); ++i) {
        // 入场：逐张延迟下落，300ms 完成
        const qreal target = qBound(0.0, (m_showElapsed.elapsed() - i * 70) / 300.0, 1.0);
        if (m_cards[i].entrance < target) {
            m_cards[i].entrance = target;
        }
    }

    update();
}

/**
 * @brief 点击卡片：复制内容到剪贴板并触发弹跳反馈
 * @author chiangyang
 */
void ClotheslineWindow::activateCard(int index) {
    if (index < 0 || index >= m_cards.size()) {
        return;
    }
    const Card &c = m_cards[index];
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (c.isImage) {
        QPixmap pix(c.text); // text 字段存原图路径
        if (!pix.isNull()) {
            clipboard->setPixmap(pix);
        }
    } else {
        clipboard->setText(c.text);
    }
    LOG_INFO(QString("ClotheslineWindow: copied history item id=%1 to clipboard").arg(c.id));

    m_bounceIndex = index;
    m_bounceTimer.start();
}

/**
 * @brief 绘制晾衣绳与卡片
 * @author chiangyang
 */
void ClotheslineWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 半透明底板，保证绳子/卡片在复杂桌面背景上可读
    QPainterPath bg;
    bg.addRoundedRect(rect().adjusted(2, 2, -2, -2), 12, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 34, 150));
    p.drawPath(bg);

    if (m_cards.isEmpty()) {
        // 空历史提示
        p.setPen(QPen(QColor(255, 255, 255, 220)));
        QFont f = p.font();
        f.setPointSize(10);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   TranslationManager::instance()->get("pet.empty", "No history yet"));
    } else {

    // 晾衣绳（悬垂曲线 + 两端木桩感端点）
    QPainterPath rope;
    rope.moveTo(12, kRopeY);
    rope.quadTo(width() / 2.0, kRopeY + kRopeSag * 2, width() - 12, kRopeY);
    p.setPen(QPen(QColor(214, 178, 128), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(rope);

    // 卡片
    for (int i = 0; i < m_cards.size(); ++i) {
        const Card &c = m_cards[i];
        // smoothstep 入场缓动
        const qreal t = c.entrance;
        const qreal eased = t * t * (3 - 2 * t);

        p.save();
        p.setOpacity(eased);

        // 摆动：绕锚点旋转
        const qreal angle = kSwayDeg * qSin(m_phase + i * 0.7);
        p.translate(c.anchor);
        p.rotate(angle);
        p.translate(-c.anchor);
        // 入场下落偏移
        p.translate(0, (1 - eased) * -24);

        // 点击弹跳：绕卡片中心缩放
        if (i == m_bounceIndex) {
            const qreal b = m_bounceTimer.elapsed() / 260.0;
            if (b >= 1.0) {
                m_bounceIndex = -1;
            } else {
                const qreal s = 1.0 + 0.18 * qSin(b * M_PI);
                p.translate(c.rect.center());
                p.scale(s, s);
                p.translate(-c.rect.center());
            }
        }

        // 衣夹
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(120, 84, 52));
        p.drawRoundedRect(QRectF(c.anchor.x() - 3, c.anchor.y() - 2, 6, kPinLen + 4), 2, 2);

        // 卡纸
        QPainterPath cardPath;
        cardPath.addRoundedRect(c.rect, 8, 8);
        p.setPen(QPen(QColor(255, 255, 255, 60), 1));
        p.setBrush(QColor(250, 250, 250, 235));
        p.drawPath(cardPath);

        const QRectF content = QRectF(c.rect).adjusted(6, 6, -6, -6);
        if (c.isImage) {
            if (!c.thumb.isNull()) {
                p.setClipPath(cardPath);
                const QSize scaled = c.thumb.size().scaled(content.size().toSize(), Qt::KeepAspectRatio);
                const QRectF target(QPointF(c.rect.center().x() - scaled.width() / 2.0,
                                            c.rect.center().y() - scaled.height() / 2.0),
                                    scaled);
                p.drawPixmap(target.toRect(), c.thumb);
                p.setClipping(false);
            }
        } else {
            // 文本卡片：前 3 行摘要
            p.setPen(QColor(60, 60, 66));
            QFont f = p.font();
            f.setPointSize(8);
            p.setFont(f);
            QTextOption opt;
            opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            opt.setAlignment(Qt::AlignTop | Qt::AlignHCenter);
            p.drawText(content, c.text.left(120), opt);
        }

        p.restore();
        }
    }

    // 右上角关闭按钮（×）
    const QRect btn = closeButtonRect();
    p.setPen(Qt::NoPen);
    p.setBrush(m_closeHovered ? QColor(232, 17, 35, 200) : QColor(255, 255, 255, 50));
    p.drawEllipse(btn);
    p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
    const QPointF bc = btn.center();
    constexpr qreal r = 5.0;
    p.drawLine(QPointF(bc.x() - r, bc.y() - r), QPointF(bc.x() + r, bc.y() + r));
    p.drawLine(QPointF(bc.x() - r, bc.y() + r), QPointF(bc.x() + r, bc.y() - r));
}

/**
 * @brief 命中检测：判断 pos 落在哪个边缘/角落调整区
 * @author chiangyang
 */
ClotheslineWindow::ResizeMode ClotheslineWindow::hitTestResize(const QPoint &pos) const {
    const int w = width(), h = height();
    const bool left = pos.x() <= kCornerSize;
    const bool right = pos.x() >= w - kCornerSize;
    const bool topCorner = pos.y() <= kCornerSize;
    const bool bottomCorner = pos.y() >= h - kCornerSize;
    // 角落优先
    if (topCorner && left) return ResizeTopLeft;
    if (topCorner && right) return ResizeTopRight;
    if (bottomCorner && left) return ResizeBottomLeft;
    if (bottomCorner && right) return ResizeBottomRight;
    // 上/下边界
    if (pos.y() <= kResizeMargin) return ResizeTop;
    if (pos.y() >= h - kResizeMargin) return ResizeBottom;
    return ResizeNone;
}

/**
 * @brief 右上角关闭按钮（×）的矩形区域
 * @author chiangyang
 */
QRect ClotheslineWindow::closeButtonRect() const {
    return QRect(width() - kCloseBtnSize - 8, 6, kCloseBtnSize, kCloseBtnSize);
}

/**
 * @brief 更新悬停光标（关闭按钮/调整区/卡片/空白区域各自的光标形状）
 * @author chiangyang
 */
void ClotheslineWindow::updateHoverCursor(const QPoint &pos) {
    const bool overClose = closeButtonRect().contains(pos);
    if (overClose != m_closeHovered) {
        m_closeHovered = overClose;
        update();
    }
    if (overClose) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    switch (hitTestResize(pos)) {
    case ResizeTopLeft:
    case ResizeBottomRight:
        setCursor(Qt::SizeFDiagCursor);
        return;
    case ResizeTopRight:
    case ResizeBottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        return;
    case ResizeTop:
    case ResizeBottom:
        setCursor(Qt::SizeVerCursor);
        return;
    default:
        break;
    }
    for (const Card &c : m_cards) {
        if (c.rect.contains(pos)) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    setCursor(Qt::OpenHandCursor);
}

/**
 * @brief 鼠标按下：关闭按钮 > 边缘调整 > 卡片复制 > 空白拖动
 * @author chiangyang
 */
void ClotheslineWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // 关闭按钮
        if (closeButtonRect().contains(event->pos())) {
            close();
            return;
        }
        // 边缘/角落调整区
        const ResizeMode mode = hitTestResize(event->pos());
        if (mode != ResizeNone) {
            m_resizeMode = mode;
            m_resizeStartGlobal = event->globalPosition().toPoint();
            m_resizeStartGeo = geometry();
            return;
        }
        // 卡片
        for (int i = 0; i < m_cards.size(); ++i) {
            if (m_cards[i].rect.contains(event->pos())) {
                activateCard(i);
                return;
            }
        }
        // 空白区域：进入拖动
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

/**
 * @brief 鼠标移动：处理边缘调整与窗口拖动；空闲时更新悬停光标
 * @author chiangyang
 */
void ClotheslineWindow::mouseMoveEvent(QMouseEvent *event) {
    // 边缘/角落调整大小
    if (m_resizeMode != ResizeNone && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->globalPosition().toPoint() - m_resizeStartGlobal;
        QRect geo = m_resizeStartGeo;
        switch (m_resizeMode) {
        case ResizeTopLeft:     geo.setTopLeft(geo.topLeft() + delta); break;
        case ResizeTopRight:    geo.setTopRight(geo.topRight() + delta); break;
        case ResizeBottomLeft:  geo.setBottomLeft(geo.bottomLeft() + delta); break;
        case ResizeBottomRight: geo.setBottomRight(geo.bottomRight() + delta); break;
        case ResizeTop:         geo.setTop(geo.top() + delta.y()); break;
        case ResizeBottom:      geo.setBottom(geo.bottom() + delta.y()); break;
        default: break;
        }
        // 最小尺寸约束（按拖拽方向夹取，避免边反转）
        const bool fromLeft = (m_resizeMode == ResizeTopLeft || m_resizeMode == ResizeBottomLeft);
        const bool fromTop = (m_resizeMode == ResizeTopLeft || m_resizeMode == ResizeTopRight
                              || m_resizeMode == ResizeTop);
        if (geo.width() < kMinW) {
            if (fromLeft) geo.setLeft(geo.right() - kMinW);
            else geo.setRight(geo.left() + kMinW);
        }
        if (geo.height() < kMinH) {
            if (fromTop) geo.setTop(geo.bottom() - kMinH);
            else geo.setBottom(geo.top() + kMinH);
        }
        setGeometry(geo);
        return;
    }
    // 窗口拖动
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        return;
    }
    updateHoverCursor(event->pos());
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放：结束拖动/调整
 * @author chiangyang
 */
void ClotheslineWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_resizeMode != ResizeNone) {
            m_resizeMode = ResizeNone;
            updateHoverCursor(event->pos());
        }
        if (m_dragging) {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 鼠标双击：卡片/关闭按钮/调整区以外的空白区域双击隐藏晾衣绳
 * @author chiangyang
 */
void ClotheslineWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->pos();
        if (closeButtonRect().contains(pos)) {
            return; // 双击关闭按钮交给单击逻辑处理
        }
        if (hitTestResize(pos) != ResizeNone) {
            return; // 调整区双击不隐藏，避免误触
        }
        for (const Card &c : m_cards) {
            if (c.rect.contains(pos)) {
                return; // 卡片双击不隐藏（单击为复制）
            }
        }
        close();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

/**
 * @brief 鼠标离开：复位关闭按钮悬停态与光标
 * @author chiangyang
 */
void ClotheslineWindow::leaveEvent(QEvent *event) {
    if (m_closeHovered) {
        m_closeHovered = false;
        update();
    }
    if (!m_dragging && m_resizeMode == ResizeNone) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

/**
 * @brief 窗口尺寸变化：重建卡片布局
 * @author chiangyang
 */
void ClotheslineWindow::resizeEvent(QResizeEvent *event) {
    layoutCards();
    QWidget::resizeEvent(event);
}

/**
 * @brief Esc 关闭
 * @author chiangyang
 */
void ClotheslineWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

/**
 * @brief 全局事件过滤：点击窗口外（宠物除外）关闭
 * @author chiangyang
 */
bool ClotheslineWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        PetWindow *pet = DesktopPetManager::instance()->petWindow();
        const bool inPet = pet && pet->geometry().contains(globalPos);
        if (!geometry().contains(globalPos) && !inPet) {
            close();
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief 关闭时停止动画并移除全局事件过滤
 * @author chiangyang
 */
void ClotheslineWindow::closeEvent(QCloseEvent *event) {
    qApp->removeEventFilter(this);
    m_animTimer->stop();
    QWidget::closeEvent(event);
    deleteLater();
}
