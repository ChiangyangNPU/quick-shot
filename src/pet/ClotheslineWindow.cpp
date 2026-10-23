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

    // 宽度铺满主屏幕，位置：顶部往下 1/4 高度处（靠屏幕上边界）
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect screenGeo = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    setFixedSize(screenGeo.width(), kWindowH);
    if (!isVisible()) {
        move(screenGeo.left(), screenGeo.top() + screenGeo.height() / 4);
    }

    // 重建卡片布局（锚点由绳子形状决定）
    const int cardCount = m_cards.size();
    if (cardCount > 0) {
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
    update();
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
        return;
    }

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

/**
 * @brief 鼠标按下：命中卡片则复制；命中空白区域则准备拖动窗口
 * @author chiangyang
 */
void ClotheslineWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
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
 * @brief 鼠标移动：拖动窗口；悬停时按区域切换光标
 * @author chiangyang
 */
void ClotheslineWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        return;
    }
    // 光标反馈：卡片上指向手型，空白处张开手型提示可拖动
    bool overCard = false;
    for (const Card &c : m_cards) {
        if (c.rect.contains(event->pos())) {
            overCard = true;
            break;
        }
    }
    setCursor(overCard ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放：结束拖动
 * @author chiangyang
 */
void ClotheslineWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
    QWidget::mouseReleaseEvent(event);
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
