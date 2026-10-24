#include "CardPreviewPopup.h"
#include "PetUiScale.h"
#include "../core/TranslationManager.h"
#include "../core/StyleManager.h"
#include "../widgets/PinWindow.h"
#include "../widgets/TranslateOverlayWindow.h"
#include "../ocr/OcrEngine.h"
#include "../ocr/OcrAsyncHelper.h"
#include "../ocr/OcrResultDialog.h"
#include "../translate/TranslateService.h"
#include "../translate/TranslateEngine.h"
#include "../history/ClipboardMonitor.h"
#include "../log/Logger.h"
#include <QPointer>

#include <QPainter>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>
#include <QMessageBox>
#include <QFontMetrics>

namespace {
// 预览内容最大宽高（物理像素上限，实际再夹取到所在屏幕可用区域）
constexpr int kPreviewMaxW = 1920;
constexpr int kPreviewMaxH = 1440;
// 预览卡片内边距（96 逻辑基准）
constexpr int kPreviewPad = 10;
// 滚轮缩放
constexpr qreal kZoomStep = 1.15; // 缩放步进
constexpr qreal kZoomMin = 0.2;   // 最小缩放倍数
constexpr qreal kZoomMax = 10.0;  // 最大缩放倍数
}

/**
 * @brief 构造函数：无边框置顶工具窗，不抢焦点
 * @author chiangyang
 */
CardPreviewPopup::CardPreviewPopup(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_pad = PetUi::scaled(kPreviewPad);
    hide();
}

/**
 * @brief 显示图片预览（等比放大）
 * @author chiangyang
 */
void CardPreviewPopup::showImage(const QPixmap &pix, const QPoint &centerGlobal) {
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
 * @author chiangyang
 */
void CardPreviewPopup::showText(const QString &text, const QPoint &centerGlobal) {
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

/**
 * @brief 滚轮缩放预览（类似贴图窗口）：以窗口中心为基准放大/缩小，内容保持清晰
 * @author chiangyang
 */
void CardPreviewPopup::wheelEvent(QWheelEvent *event) {
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
 * @brief 鼠标移出预览：预览消失（右键菜单打开期间除外，保持预览可见）
 * @author chiangyang
 */
void CardPreviewPopup::leaveEvent(QEvent *event) {
    if (!m_menuOpen) {
        hide();
    }
    QWidget::leaveEvent(event);
}

/**
 * @brief 右键菜单（参考贴图窗口）：复制/标注/文字识别/翻译
 *
 * 菜单弹出期间预览保持可见；仅当真正点击菜单项后才隐藏预览并执行对应功能，
 * 菜单被 Esc/点击空白取消时预览保留：
 * - 复制：图片写剪贴板 / 文本写剪贴板
 * - 标注：用原图创建贴图窗口并直接进入标注模式
 * - 文字识别：异步 OCR 后弹出识别结果对话框
 * - 翻译：图片走 OCR→批量翻译叠加窗口；文本直接调用翻译服务并弹出译文
 * @param event 右键菜单事件
 * @author chiangyang
 */
void CardPreviewPopup::contextMenuEvent(QContextMenuEvent *event) {
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

    // 菜单打开期间预览保持可见；仅选中菜单项后才隐藏预览，取消菜单则保留
    m_menuOpen = true;
    QAction *chosen = menu.exec(event->globalPos());
    m_menuOpen = false;
    if (!chosen) {
        return;
    }
    hide();

    if (chosen == copyAction) {
        QClipboard *cb = QGuiApplication::clipboard();
        if (isImage) {
            cb->setPixmap(image);
        } else {
            // 文本已在历史中，标记为已记录，避免复制动作再生成一条重复记录
            ClipboardMonitor::instance()->markTextAsRecorded(text);
            cb->setText(text);
        }
        LOG_INFO("CardPreviewPopup: preview content copied from context menu");
    } else if (chosen == annotateAction) {
        // 标注需要稳定窗口：用原图创建贴图并直接进入标注模式。
        // PinWindow 构造函数不设几何（须由调用方 setGeometry），按原图像素设定
        // 初始尺寸；超高图按屏幕可用高度的 90% 等比缩小初始显示（与
        // SnipScreen::pinPixmap 一致），PinWindow 内部仍保留原图，复制/保存不受影响
        QSize windowSize = image.size();
        QScreen *screen = QGuiApplication::screenAt(centerGlobal);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        const QRect avail = screen ? screen->availableGeometry()
                                   : QRect(0, 0, 1920, 1080);
        const int maxHeight = qMax(100, int(avail.height() * 0.9));
        if (windowSize.height() > maxHeight) {
            const qreal scale = qreal(maxHeight) / windowSize.height();
            windowSize.setHeight(maxHeight);
            windowSize.setWidth(qMax(1, qRound(windowSize.width() * scale)));
        }
        // 以屏幕可用区域为中心弹出，整体夹到屏幕内
        QPoint pos = avail.center() - QPoint(windowSize.width() / 2, windowSize.height() / 2);
        pos.setX(qBound(avail.left(), pos.x(),
                        qMax(avail.left(), avail.right() - windowSize.width() + 1)));
        pos.setY(qBound(avail.top(), pos.y(),
                        qMax(avail.top(), avail.bottom() - windowSize.height() + 1)));
        auto *pin = new PinWindow(image);
        pin->setGeometry(QRect(pos, windowSize));
        pin->show();
        pin->raise();
        pin->enterAnnotationMode();
        LOG_INFO(QString("CardPreviewPopup: preview annotate -> pin window %1x%2 with annotation mode")
                 .arg(windowSize.width()).arg(windowSize.height()));
    } else if (chosen == ocrAction) {
        OcrAsync::run(host, image.toImage(), QRect(),
                      [host, centerGlobal](const OcrEngine::OcrResult &result) {
            auto *dialog = new OcrResultDialog(result);
            dialog->adjustSize();
            if (QScreen *s = QGuiApplication::screenAt(centerGlobal)) {
                dialog->move(s->availableGeometry().center() - dialog->rect().center());
            }
            dialog->show();
        });
    } else if (chosen == translateAction) {
        // 隐私提示用异步确认（Windows 上模态弹框关闭会破坏置顶窗口的鼠标派发；
        // 见 TranslateService::checkEnabledAndPrivacyAsync）。晾衣绳窗口关闭即
        // 销毁（deleteLater），弹框开启期间可能已关闭，用 QPointer 兜底。
        QPointer<QWidget> hostGuard(host);
        TranslateService::checkEnabledAndPrivacyAsync(host, QRect(),
            [hostGuard, isImage, image, text, centerGlobal](bool allowed) mutable {
            if (!hostGuard) {
                LOG_INFO("CardPreviewPopup: Translate canceled (host closed during privacy check)");
                return;
            }
            if (!allowed) {
                return;
            }
            if (isImage) {
                // 图片：OCR 识别 → 批量翻译 → 译文叠加窗口（与贴图窗口一致）
                OcrAsync::run(hostGuard, image.toImage(), QRect(),
                              [hostGuard, image, centerGlobal](const OcrEngine::OcrResult &result) {
                    if (!hostGuard) return;
                    const QPoint pos = centerGlobal - QPoint(image.width() / 2, image.height() / 2);
                    TranslateOverlayWindow::translateAndShow(
                        hostGuard, image, result.texts, result.polygons, pos, QRect(pos, image.size()));
                });
            } else {
                // 文本：直接调用翻译服务，译文复用识别结果对话框展示
                TranslateService *ts = TranslateService::instance();
                connect(ts, &TranslateService::finished, hostGuard,
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
                connect(ts, &TranslateService::failed, hostGuard,
                        [hostGuard](TranslateEngine::TranslateError code, const QString &) {
                    QMessageBox::warning(hostGuard, QString(), TranslateService::errorMessage(code));
                }, Qt::SingleShotConnection);
                ts->translate(text);
            }
        });
    }
}

/**
 * @brief 绘制预览卡片：白底圆角 + 图片/文本内容
 * @author chiangyang
 */
void CardPreviewPopup::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    const QRect contentRect = rect().adjusted(m_pad, m_pad, -m_pad, -m_pad);
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

/**
 * @brief 预览内容有效最大尺寸：kPreviewMaxW×kPreviewMaxH 夹取到所在屏幕
 *        可用区域（四边各留 20px 边距），避免预览超出屏幕
 * @author chiangyang
 */
QSize CardPreviewPopup::effectiveMaxSize(const QPoint &centerGlobal) const {
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
void CardPreviewPopup::place(const QPoint &centerGlobal, const QSize &windowSize) {
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
void CardPreviewPopup::renderImage() {
    const QSizeF logical = QSizeF(m_baseContent) * m_zoom;
    const qreal dpr = devicePixelRatioF();
    m_pix = m_original.scaled((logical * dpr).toSize(),
                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_pix.setDevicePixelRatio(dpr);
    place(m_centerGlobal, logical.toSize() + QSize(m_pad * 2, m_pad * 2));
    update();
}

/**
 * @brief 按当前缩放倍数渲染文本：字号随缩放变化重新排版（文字始终清晰），
 *        窗口尺寸夹取到屏幕可用区域
 * @author chiangyang
 */
void CardPreviewPopup::renderText() {
    QFont f = font();
    f.setPointSizeF(m_baseFontPt * m_zoom);
    const QFontMetrics fm(f);
    const QRect br = fm.boundingRect(QRect(0, 0, m_wrapWidth, 100000),
                                     Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                                     m_text);
    const QSize content(qMin(br.width(), m_wrapWidth), br.height());
    const QSize winSize = (content + QSize(m_pad * 2, m_pad * 2))
                              .boundedTo(effectiveMaxSize(m_centerGlobal)
                                         + QSize(m_pad * 2, m_pad * 2));
    place(m_centerGlobal, winSize);
    update();
}
