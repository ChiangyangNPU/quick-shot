#include "ClotheslineWindow.h"
#include "PetWindow.h"
#include "DesktopPetManager.h"
#include "../history/HistoryManager.h"
#include "../core/TranslationManager.h"
#include "../core/StyleManager.h"
#include "../widgets/PinWindow.h"
#include "../widgets/TranslateOverlayWindow.h"
#include "../ocr/OcrEngine.h"
#include "../ocr/OcrAsyncHelper.h"
#include "../ocr/OcrResultDialog.h"
#include "../translate/TranslateService.h"
#include "../translate/TranslateEngine.h"
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
#include <QFontMetrics>
#include <QCursor>
#include <QWheelEvent>
#include <QWindow>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QContextMenuEvent>
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
constexpr int kPinLen = 24;     // 挂绳长度（绳子锚点到卡片顶）
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
// 悬停放大预览
constexpr int kPreviewMaxW = 1920; // 预览内容最大宽度
constexpr int kPreviewMaxH = 1440; // 预览内容最大高度
constexpr int kPreviewPad = 10;   // 预览卡片内边距
constexpr qreal kZoomStep = 1.15; // 滚轮缩放步进
constexpr qreal kZoomMin = 0.2;   // 最小缩放倍数
constexpr qreal kZoomMax = 10.0;  // 最大缩放倍数
}

/**
 * @brief 卡片悬停放大预览弹窗
 *
 * 鼠标悬停在晾衣绳卡片上时，以晾衣绳窗口中心为基准居中弹出：
 * - 图片卡片：加载原图等比放大（不超过 kPreviewMaxW × kPreviewMaxH，按 DPR 输出保证清晰）
 * - 文本卡片：自动换行显示完整文本（超出最大高度时截断）
 * - 鼠标移到预览图上时滚轮可放大/缩小（类似贴图窗口），移出预览即消失
 * - 右键预览弹出菜单（参考贴图窗口）：复制/标注/文字识别/翻译（文本卡片仅复制/翻译），
 *   点击菜单项后预览消失并执行对应功能
 * @author chiangyang
 */
class CardPreviewPopup : public QWidget {
public:
    /**
     * @brief 构造函数：无边框置顶工具窗，不抢焦点
     * @author chiangyang
     */
    explicit CardPreviewPopup(QWidget *parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        hide();
    }

    /**
     * @brief 显示图片预览（等比放大）
     * @param pix 原图/缩略图
     * @param centerGlobal 期望中心的全局坐标（晾衣绳窗口中心）
     * @author chiangyang
     */
    void showImage(const QPixmap &pix, const QPoint &centerGlobal) {
        if (pix.isNull()) {
            hide();
            return;
        }
        m_isImage = true;
        m_text.clear();
        m_original = pix; // 保留原图，缩放始终从原图出图保证清晰
        m_zoom = 1.0;
        m_centerGlobal = centerGlobal;
        m_baseContent = pix.size().scaled(effectiveMaxSize(centerGlobal), Qt::KeepAspectRatio);
        renderImage();
    }

    /**
     * @brief 显示文本预览（自动换行，限宽限高）
     * @param text 文本内容
     * @param centerGlobal 期望中心的全局坐标（晾衣绳窗口中心）
     * @author chiangyang
     */
    void showText(const QString &text, const QPoint &centerGlobal) {
        if (text.isEmpty()) {
            hide();
            return;
        }
        m_isImage = false;
        m_original = QPixmap();
        m_pix = QPixmap();
        m_text = text;
        m_zoom = 1.0;
        m_centerGlobal = centerGlobal;
        m_baseFontPt = font().pointSizeF();
        m_wrapWidth = effectiveMaxSize(centerGlobal).width();
        renderText();
    }

protected:
    /**
     * @brief 滚轮缩放预览（类似贴图窗口）：以窗口中心为基准放大/缩小，内容保持清晰
     * @author chiangyang
     */
    void wheelEvent(QWheelEvent *event) override {
        const qreal factor = event->angleDelta().y() > 0 ? kZoomStep : 1.0 / kZoomStep;
        m_zoom = qBound(kZoomMin, m_zoom * factor, kZoomMax);
        if (m_isImage) {
            renderImage();
        } else {
            renderText();
        }
        event->accept();
    }

    /**
     * @brief 鼠标移出预览：预览消失
     * @author chiangyang
     */
    void leaveEvent(QEvent *event) override {
        hide();
        QWidget::leaveEvent(event);
    }

    /**
     * @brief 右键菜单（参考贴图窗口）：复制/标注/文字识别/翻译
     *
     * 菜单弹出即隐藏预览；点击菜单项后执行对应功能：
     * - 复制：图片写剪贴板 / 文本写剪贴板
     * - 标注：用原图创建贴图窗口并直接进入标注模式
     * - 文字识别：异步 OCR 后弹出识别结果对话框
     * - 翻译：图片走 OCR→批量翻译叠加窗口；文本直接调用翻译服务并弹出译文
     * @param event 右键菜单事件
     * @author chiangyang
     */
    void contextMenuEvent(QContextMenuEvent *event) override {
        QWidget *host = parentWidget();             // 晾衣绳窗口，作为对话框父级
        const QPixmap image = m_original;           // 拷贝一份，预览隐藏/切换内容后仍可用
        const QString text = m_text;
        const bool isImage = m_isImage && !image.isNull();
        const QPoint centerGlobal = m_centerGlobal;
        if (!isImage && text.isEmpty()) {
            return;
        }

        QMenu menu(host);
        TranslationManager *tm = TranslationManager::instance();
        menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        menu.setAttribute(Qt::WA_TranslucentBackground);
        menu.setStyleSheet(StyleManager::getMenuStyle());

        QAction *copyAction = menu.addAction(tm->get("pinwindow.copy", "Copy"));
        QAction *annotateAction = nullptr;
        QAction *ocrAction = nullptr;
        if (isImage) {
            annotateAction = menu.addAction(tm->get("pinwindow.annotate", "Annotate"));
            ocrAction = menu.addAction(tm->get("ocr.button"));
        }
        QAction *translateAction = menu.addAction(tm->get("translate.button"));

        hide();  // 菜单弹出即隐藏预览

        QAction *chosen = menu.exec(event->globalPos());
        if (!chosen) {
            return;
        }

        if (chosen == copyAction) {
            QClipboard *cb = QGuiApplication::clipboard();
            if (isImage) {
                cb->setPixmap(image);
            } else {
                cb->setText(text);
            }
            LOG_INFO("ClotheslineWindow: preview content copied from context menu");
        } else if (chosen == annotateAction) {
            // 标注需要稳定窗口：用原图创建贴图并直接进入标注模式
            auto *pin = new PinWindow(image);
            pin->show();
            if (QScreen *s = QGuiApplication::screenAt(centerGlobal)) {
                const QRect g = s->availableGeometry();
                pin->move(g.center() - pin->rect().center());
            }
            pin->enterAnnotationMode();
            LOG_INFO("ClotheslineWindow: preview annotate -> pin window with annotation mode");
        } else if (chosen == ocrAction) {
            OcrAsync::run(host, image.toImage(), QRect(), false,
                          [host, centerGlobal](const OcrEngine::OcrResult &result) {
                auto *dialog = new OcrResultDialog(result);
                dialog->adjustSize();
                if (QScreen *s = QGuiApplication::screenAt(centerGlobal)) {
                    dialog->move(s->availableGeometry().center() - dialog->rect().center());
                }
                dialog->show();
            });
        } else if (chosen == translateAction) {
            if (!TranslateService::checkEnabledAndPrivacy(host)) {
                return;
            }
            if (isImage) {
                // 图片：OCR 识别 → 批量翻译 → 译文叠加窗口（与贴图窗口一致）
                OcrAsync::run(host, image.toImage(), QRect(), true,
                              [host, image, centerGlobal](const OcrEngine::OcrResult &result) {
                    const QPoint pos = centerGlobal - QPoint(image.width() / 2, image.height() / 2);
                    TranslateOverlayWindow::translateAndShow(
                        host, image, result.texts, result.polygons, pos, QRect(pos, image.size()));
                });
            } else {
                // 文本：直接调用翻译服务，译文复用识别结果对话框展示
                TranslateService *ts = TranslateService::instance();
                connect(ts, &TranslateService::finished, host,
                        [centerGlobal](const QString &, const QString &translated) {
                    OcrEngine::OcrResult r;
                    r.texts = {translated};
                    auto *dialog = new OcrResultDialog(r);
                    dialog->adjustSize();
                    if (QScreen *s = QGuiApplication::screenAt(centerGlobal)) {
                        dialog->move(s->availableGeometry().center() - dialog->rect().center());
                    }
                    dialog->show();
                }, Qt::SingleShotConnection);
                connect(ts, &TranslateService::failed, host,
                        [host](TranslateEngine::TranslateError code, const QString &) {
                    QMessageBox::warning(host, QString(), TranslateService::errorMessage(code));
                }, Qt::SingleShotConnection);
                ts->translate(text);
            }
        }
    }

    /**
     * @brief 绘制预览卡片：白底圆角 + 图片/文本内容
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setPen(QPen(QColor(0, 0, 0, 40), 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
        const QRect contentRect = rect().adjusted(kPreviewPad, kPreviewPad, -kPreviewPad, -kPreviewPad);
        if (m_isImage) {
            if (!m_pix.isNull()) {
                p.drawPixmap(contentRect.topLeft(), m_pix);
            }
        } else {
            QFont f = font();
            f.setPointSizeF(m_baseFontPt * m_zoom);
            p.setFont(f);
            p.setPen(QColor(60, 60, 60));
            p.drawText(contentRect,
                       Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, m_text);
        }
    }

private:
    /**
     * @brief 预览内容有效最大尺寸：kPreviewMaxW×kPreviewMaxH 夹取到所在屏幕
     *        可用区域（四边各留 20px 边距），避免预览超出屏幕
     * @author chiangyang
     */
    QSize effectiveMaxSize(const QPoint &centerGlobal) const {
        QSize maxSize(kPreviewMaxW, kPreviewMaxH);
        if (QScreen *screen = QGuiApplication::screenAt(centerGlobal)) {
            maxSize = maxSize.boundedTo(screen->availableGeometry().size() - QSize(40, 40));
        }
        return maxSize;
    }

    /**
     * @brief 以期望中心定位并显示（夹取到所在屏幕可用区域内）
     * @author chiangyang
     */
    void place(const QPoint &centerGlobal, const QSize &windowSize) {
        setFixedSize(windowSize);
        QPoint pos = centerGlobal - QPoint(windowSize.width() / 2, windowSize.height() / 2);
        if (QScreen *screen = QGuiApplication::screenAt(centerGlobal)) {
            const QRect sg = screen->availableGeometry();
            // 窗口大于屏幕时 qBound 上下界会反转，先夹取下界保证安全
            pos.setX(qBound(sg.left(), pos.x(), qMax(sg.left(), sg.right() - windowSize.width() + 1)));
            pos.setY(qBound(sg.top(), pos.y(), qMax(sg.top(), sg.bottom() - windowSize.height() + 1)));
        }
        move(pos);
        show();
        raise();
    }

    /**
     * @brief 按当前缩放倍数渲染图片：从原图按目标尺寸出图（DPR 物理像素），保持清晰
     * @author chiangyang
     */
    void renderImage() {
        const QSizeF logical = QSizeF(m_baseContent) * m_zoom;
        const qreal dpr = devicePixelRatioF();
        m_pix = m_original.scaled((logical * dpr).toSize(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_pix.setDevicePixelRatio(dpr);
        place(m_centerGlobal, logical.toSize() + QSize(kPreviewPad * 2, kPreviewPad * 2));
        update();
    }

    /**
     * @brief 按当前缩放倍数渲染文本：字号随缩放变化重新排版（文字始终清晰），
     *        窗口尺寸夹取到屏幕可用区域
     * @author chiangyang
     */
    void renderText() {
        QFont f = font();
        f.setPointSizeF(m_baseFontPt * m_zoom);
        const QFontMetrics fm(f);
        const QRect br = fm.boundingRect(QRect(0, 0, m_wrapWidth, 100000),
                                         Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                                         m_text);
        const QSize content(qMin(br.width(), m_wrapWidth), br.height());
        const QSize winSize = (content + QSize(kPreviewPad * 2, kPreviewPad * 2))
                                  .boundedTo(effectiveMaxSize(m_centerGlobal)
                                             + QSize(kPreviewPad * 2, kPreviewPad * 2));
        place(m_centerGlobal, winSize);
        update();
    }

    QPixmap m_original;         ///< 原图（图片模式的缩放源，保证缩放后清晰）
    QPixmap m_pix;              ///< 当前缩放后待绘制的图
    QString m_text;             ///< 文本预览内容
    bool m_isImage = true;      ///< 当前是图片预览还是文本预览
    QSize m_baseContent;        ///< zoom=1 时图片内容的逻辑尺寸
    qreal m_zoom = 1.0;         ///< 当前缩放倍数
    qreal m_baseFontPt = 12.0;  ///< 文本基准字号（zoom=1）
    int m_wrapWidth = 460;      ///< 文本换行宽度
    QPoint m_centerGlobal;      ///< 居中基准（晾衣绳窗口中心）
};

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
    m_fullImageCache.clear();
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
        // 预览加载原图（缩略图放大会模糊），按记录 ID 缓存避免重复读盘
        QPixmap full = m_fullImageCache.value(c.id);
        if (full.isNull()) {
            full = QPixmap(c.text); // 图片卡片的 text 存原图路径
            if (!full.isNull()) {
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
 * @brief 全局事件过滤：点击窗口外（宠物除外）关闭
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
            // 截图/录屏覆盖层上的交互不关闭晾衣绳：复制/贴图/快照后晾衣绳保持打开并自动刷新
            // 注意：顶层窗口的鼠标事件先派发给其 QWidgetWindow（QWindow 句柄），
            // 需经 QWidget::find 映射回 widget 再判断顶层窗口类型
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (!w) {
                if (auto *win = qobject_cast<QWindow *>(watched)) {
                    w = QWidget::find(win->winId());
                }
            }
            QWidget *top = w ? w->window() : nullptr;
            if (top && (top->inherits("SnipScreen") || top->inherits("RecordingControlWindow")
                        || top->inherits("QMenu"))) {  // 预览右键菜单等弹出菜单交互不关闭
                return QWidget::eventFilter(watched, event);
            }
            // 诊断日志：记录触发关闭的事件来源（定位晾衣绳被误关闭的根因）
            LOG_INFO(QString("ClotheslineWindow: close by outside press, watched=%1, top=%2")
                     .arg(watched ? watched->metaObject()->className() : "null")
                     .arg(top ? top->metaObject()->className() : "null"));
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
