#include "ClotheslineWindow.h"
#include "PetWindow.h"
#include "CardPreviewPopup.h"
#include "DesktopPetManager.h"
#include "PetUiScale.h"
#include "../history/HistoryManager.h"
#include "../history/ClipboardMonitor.h"
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
#include <QCursor>
#include <QWindow>
#include <QtMath>

namespace {
// 最多展示的卡片数量
constexpr int kMaxCards = 12;
// 卡片尺寸与排布（96 逻辑基准，绘制/命中时按 DPI 系数缩放）
constexpr int kCardW = 104;
constexpr int kCardH = 96;
constexpr int kCardGap = 16;
// 绳子与窗口
constexpr int kRopeY = 30;
constexpr int kRopeSag = 16;    // 悬垂深度
constexpr int kPinLen = 24;     // 挂绳长度（绳子锚点到卡片顶）
constexpr int kRopeMarginX = 12; // 绳子两端距窗口左右边距
// 窗口默认高度：绳子最低点 + 挂绳 + 卡片 + 底部边距，保证卡片不超出窗口下边界
constexpr int kBottomMargin = 16;
constexpr int kWindowH = kRopeY + kRopeSag + kPinLen + kCardH + kBottomMargin;
// 边缘调整与关闭按钮
constexpr int kResizeMargin = 6;    // 上下边界调整区厚度
constexpr int kCornerSize = 18;     // 角落调整区边长
constexpr int kCloseBtnSize = 24;   // 关闭按钮边长
constexpr int kMinW = 360;          // 窗口最小宽度
// 窗口最小高度：保证卡片完整可见（底部仅留 8px 边距）
constexpr int kMinH = kRopeY + kRopeSag + kPinLen + kCardH + 8;
// 动画
constexpr int kTickMs = 33;
constexpr qreal kSwayDeg = 3.0; // 摆动幅度（度）
// 预览原图缓存上限（超出后整体清空，防止长时间驻留占用过多内存）
constexpr int kFullImageCacheLimit = 24;
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
    m_dpiScale = PetUi::scale();
    setMinimumSize(sc(kMinW), sc(kMinH));
    // 需要悬停光标反馈（调整区/卡片/空白区域不同光标），开启鼠标追踪
    setMouseTracking(true);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(kTickMs);
    connect(m_animTimer, &QTimer::timeout, this, &ClotheslineWindow::onTick);

    // 悬停放大预览弹窗（随本窗口销毁）
    m_preview = new CardPreviewPopup(this);

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
                screenGeo.width(), sc(kWindowH));

    reload();

    show();
    raise();
    activateWindow();

    m_showElapsed.start();
    m_animTimer->start();

    // 全局事件过滤：应用自身窗口上的点击不关闭（见 eventFilter 注释）
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
    // 预览原图缓存保留（有界，updateHoverPreview 中控制上限），避免频繁刷新后反复读盘
    hidePreview(); // 卡片重建后悬停态失效
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
    const int cardW = sc(kCardW);
    const int gap = sc(kCardGap);
    // 窗口放不下全部卡片时只布置能放下的部分，避免两端卡片被裁掉且无法点击
    const int maxFit = qBound(1, (width() + gap) / (cardW + gap), cardCount);
    const qreal totalW = maxFit * cardW + (maxFit - 1) * gap;
    qreal x = (width() - totalW) / 2.0;
    for (int i = 0; i < cardCount; ++i) {
        Card &c = m_cards[i];
        if (i >= maxFit) {
            // 放不下的卡片不布置（空矩形不参与绘制/命中/悬停）
            c.rect = QRect();
            c.anchor = QPointF();
            continue;
        }
        const qreal cx = x + cardW / 2.0;
        c.anchor = QPointF(cx, ropeYAt(cx));
        c.rect = QRect(int(x), int(c.anchor.y() + sc(kPinLen)), cardW, sc(kCardH));
        x += cardW + gap;
    }
}

/**
 * @brief 计算晾衣绳在 x 处的高度（二次贝塞尔悬垂曲线）
 * @author chiangyang
 */
qreal ClotheslineWindow::ropeYAt(qreal x) const {
    const qreal x0 = sc(kRopeMarginX), x1 = width() - sc(kRopeMarginX);
    const qreal ropeY = sc(kRopeY);
    const qreal sag = sc(kRopeSag);
    if (x1 <= x0) {
        return ropeY;
    }
    const qreal t = qBound(0.0, (x - x0) / (x1 - x0), 1.0);
    // P0=(x0,ropeY) C=(mid,ropeY+sag*2) P1=(x1,ropeY)
    const qreal y = (1 - t) * (1 - t) * ropeY
                  + 2 * (1 - t) * t * (ropeY + sag * 2)
                  + t * t * ropeY;
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
        // 文本已在历史中，标记为已记录，避免复制动作本身再生成一条重复记录
        ClipboardMonitor::instance()->markTextAsRecorded(c.text);
        clipboard->setText(c.text);
    }
    LOG_INFO(QString("ClotheslineWindow: copied history item id=%1 to clipboard").arg(c.id));

    // 按记录 ID 标记弹跳卡片：若复制触发历史刷新导致卡片重排，反馈仍落在原卡片上
    m_bounceId = c.id;
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
        const int ropeY = sc(kRopeY);
        const int sag = sc(kRopeSag);
        const int marginX = sc(kRopeMarginX);
        QPainterPath rope;
        rope.moveTo(marginX, ropeY);
        rope.quadTo(width() / 2.0, ropeY + sag * 2.0, width() - marginX, ropeY);
        p.setPen(QPen(QColor(214, 178, 128), 2.5 * m_dpiScale, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(rope);

        // 卡片
        for (int i = 0; i < m_cards.size(); ++i) {
            const Card &c = m_cards[i];
            if (c.rect.isEmpty()) {
                continue; // 放不下的卡片未布置，不绘制
            }
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
            p.translate(0, (1 - eased) * -sc(24));

            // 点击弹跳：绕卡片中心缩放（按记录 ID 标记，卡片重建后仍对准原卡片）
            if (c.id == m_bounceId) {
                const qreal b = m_bounceTimer.elapsed() / 260.0;
                if (b >= 1.0) {
                    m_bounceId = -1;
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
            p.drawRoundedRect(QRectF(c.anchor.x() - sc(3), c.anchor.y() - sc(2),
                                     sc(6), sc(kPinLen + 4)), 2, 2);

            // 卡纸
            QPainterPath cardPath;
            cardPath.addRoundedRect(c.rect, 8, 8);
            p.setPen(QPen(QColor(255, 255, 255, 60), 1));
            p.setBrush(QColor(250, 250, 250, 235));
            p.drawPath(cardPath);

            const QRectF content = QRectF(c.rect).adjusted(sc(6), sc(6), -sc(6), -sc(6));
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
    p.setPen(QPen(Qt::white, 2 * m_dpiScale, Qt::SolidLine, Qt::RoundCap));
    const QPointF bc = btn.center();
    const qreal r = sc(5);
    p.drawLine(QPointF(bc.x() - r, bc.y() - r), QPointF(bc.x() + r, bc.y() + r));
    p.drawLine(QPointF(bc.x() - r, bc.y() + r), QPointF(bc.x() + r, bc.y() - r));
}

/**
 * @brief 命中检测：判断 pos 落在哪个边缘/角落调整区
 * @author chiangyang
 */
ClotheslineWindow::ResizeMode ClotheslineWindow::hitTestResize(const QPoint &pos) const {
    const int w = width(), h = height();
    const int corner = sc(kCornerSize);
    const int margin = sc(kResizeMargin);
    const bool left = pos.x() <= corner;
    const bool right = pos.x() >= w - corner;
    const bool topCorner = pos.y() <= corner;
    const bool bottomCorner = pos.y() >= h - corner;
    // 角落优先
    if (topCorner && left) return ResizeTopLeft;
    if (topCorner && right) return ResizeTopRight;
    if (bottomCorner && left) return ResizeBottomLeft;
    if (bottomCorner && right) return ResizeBottomRight;
    // 上/下边界
    if (pos.y() <= margin) return ResizeTop;
    if (pos.y() >= h - margin) return ResizeBottom;
    return ResizeNone;
}

/**
 * @brief 右上角关闭按钮（×）的矩形区域
 * @author chiangyang
 */
QRect ClotheslineWindow::closeButtonRect() const {
    const int size = sc(kCloseBtnSize);
    return QRect(width() - size - sc(8), sc(6), size, size);
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
 * @brief 更新卡片悬停预览：悬停卡片时以窗口中心为基准弹出放大预览
 * @param pos 窗口局部坐标
 * @author chiangyang
 */
void ClotheslineWindow::updateHoverPreview(const QPoint &pos) {
    if (m_dragging || m_resizeMode != ResizeNone) {
        hidePreview();
        return;
    }
    int idx = -1;
    for (int i = 0; i < m_cards.size(); ++i) {
        if (m_cards[i].rect.contains(pos)) {
            idx = i;
            break;
        }
    }
    if (idx == m_hoverIndex && m_preview->isVisible()) {
        return; // 悬停卡片未变化，避免重复弹窗
    }
    m_hoverIndex = idx;
    if (idx < 0) {
        hidePreview();
        return;
    }
    // 以晾衣绳窗口中心为基准居中弹出预览
    const Card &c = m_cards[idx];
    const QPoint center = mapToGlobal(QPoint(width() / 2, height() / 2));
    if (c.isImage) {
        // 预览加载原图（缩略图放大会模糊），按记录 ID 缓存避免重复读盘；
        // 缓存有界（有界清空策略），历史频繁刷新时也尽量命中
        QPixmap full = m_fullImageCache.value(c.id);
        if (full.isNull()) {
            full = QPixmap(c.text); // 图片卡片的 text 存原图路径
            if (!full.isNull()) {
                if (m_fullImageCache.size() >= kFullImageCacheLimit) {
                    m_fullImageCache.clear();
                }
                m_fullImageCache.insert(c.id, full);
            }
        }
        m_preview->showImage(full.isNull() ? c.thumb : full, center);
    } else {
        m_preview->showText(c.text, center);
    }
}

/**
 * @brief 隐藏卡片放大预览弹窗
 * @author chiangyang
 */
void ClotheslineWindow::hidePreview() {
    m_hoverIndex = -1;
    if (m_preview) {
        m_preview->hide();
    }
}

/**
 * @brief 鼠标按下：关闭按钮 > 边缘调整 > 卡片复制 > 空白拖动
 * @author chiangyang
 */
void ClotheslineWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        hidePreview(); // 按下即关闭预览
        // 关闭按钮（不立即关闭，等 release 消化本次点击后再关，防止事件穿透到桌面）
        if (closeButtonRect().contains(event->pos())) {
            m_pendingClose = true;
            event->accept();
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
 * @brief 鼠标移动：处理边缘调整与窗口拖动；空闲时更新悬停光标与放大预览
 * @author chiangyang
 */
void ClotheslineWindow::mouseMoveEvent(QMouseEvent *event) {
    // 边缘/角落调整大小
    if (m_resizeMode != ResizeNone && (event->buttons() & Qt::LeftButton)) {
        hidePreview();
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
        if (geo.width() < sc(kMinW)) {
            if (fromLeft) geo.setLeft(geo.right() - sc(kMinW));
            else geo.setRight(geo.left() + sc(kMinW));
        }
        if (geo.height() < sc(kMinH)) {
            if (fromTop) geo.setTop(geo.bottom() - sc(kMinH));
            else geo.setBottom(geo.top() + sc(kMinH));
        }
        setGeometry(geo);
        return;
    }
    // 窗口拖动
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        hidePreview();
        move(event->globalPosition().toPoint() - m_dragOffset);
        return;
    }
    updateHoverCursor(event->pos());
    updateHoverPreview(event->pos());
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放：结束拖动/调整；若双击请求了关闭则在此真正关闭
 * @note 双击的释放事件必须由本窗口消化完再关闭，否则残余事件会穿透到桌面
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
        if (m_pendingClose) {
            m_pendingClose = false;
            event->accept();
            close();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 鼠标双击：卡片/关闭按钮/调整区以外的空白区域双击隐藏晾衣绳
 * @note 不立即 close()，仅标记待关闭；立即隐藏窗口会导致本次双击的
 *       残余鼠标事件被系统派发给下方桌面（事件穿透）
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
        m_pendingClose = true; // 等本次双击的 release 到达后在 mouseReleaseEvent 中关闭
        event->accept();
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
    // 光标仍在晾衣绳或预览弹窗内时不隐藏预览（预览盖住卡片时会持续收到 Leave）
    const QPoint globalPos = QCursor::pos();
    const bool inPreview = m_preview && m_preview->isVisible()
                           && m_preview->geometry().contains(globalPos);
    if (!geometry().contains(globalPos) && !inPreview) {
        hidePreview();
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
 * @brief 全局事件过滤：应用自身窗口上的点击不关闭晾衣绳
 *
 * 晾衣绳是常驻历史面板，关闭只走显式途径：右上角 ×、Esc、双击空白、
 * 再次点击宠物。注意鼠标点击桌面/其他应用的事件不会进入本进程事件循环，
 * 本过滤器只能看到本应用自己的窗口——“点窗外关闭”实际能触发的只有
 * 点击本应用其他顶层窗口的情形（设置/历史/贴图/标注/OCR/翻译等），
 * 而这些全是应用内交互，一律不关闭；仅当点击落在无法映射回 QWidget 的
 * 窗口（原生对话框等）时才视为真正的窗外点击而关闭。
 * @note 顶层窗口的鼠标事件先派发给其 QWidgetWindow（QWindow 句柄），
 *       需经 QWidget::find 映射回 widget 再判断
 * @author chiangyang
 */
bool ClotheslineWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        PetWindow *pet = DesktopPetManager::instance()->petWindow();
        const bool inPet = pet && pet->geometry().contains(globalPos);
        // 预览弹窗属于本功能的一部分，点击预览不关闭晾衣绳
        const bool inPreview = m_preview && m_preview->isVisible()
                               && m_preview->geometry().contains(globalPos);
        if (!geometry().contains(globalPos) && !inPet && !inPreview) {
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (!w) {
                if (auto *win = qobject_cast<QWindow *>(watched)) {
                    w = QWidget::find(win->winId());
                }
            }
            // 能映射回 QWidget 的都是应用自身窗口（含其子窗口）：不关闭
            if (w) {
                return QWidget::eventFilter(watched, event);
            }
            // 原生对话框等非 Qt 窗口：视为真正的窗外点击
            LOG_INFO(QString("ClotheslineWindow: close by press on non-widget window, watched=%1")
                     .arg(watched ? watched->metaObject()->className() : "null"));
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
    // 诊断日志：区分用户关闭（ spontaneous ）与程序内 close()（定位晾衣绳被误关闭的根因）
    LOG_INFO(QString("ClotheslineWindow: closeEvent, spontaneous=%1").arg(event->spontaneous()));
    qApp->removeEventFilter(this);
    m_animTimer->stop();
    hidePreview();
    QWidget::closeEvent(event);
    deleteLater();
}
