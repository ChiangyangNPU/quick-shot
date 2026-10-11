#include "OverlayTextEdit.h"
#include <cmath>
#include <QEvent>
#include <QPainter>
#include <QLineF>
#include <QStyle>
#include <QGraphicsOpacityEffect>
#include <QtMath>
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

namespace {

/**
 * @brief 旋转实时预览控件：把源编辑框按角度离屏渲染到自身
 * @author chiangyang
 */
class RotationPreviewWidget final : public QWidget {
public:
    RotationPreviewWidget(OverlayTextEdit *source, QWidget *parent)
        : QWidget(parent), source(source), angleDegreesValue(0.0) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        hide();
    }

    /**
     * @brief 设置预览旋转角度并触发重绘
     * @param angle 旋转角度（度）
     * @author chiangyang
     */
    void setAngleDegrees(qreal angle) {
        angleDegreesValue = angle;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        if (!source) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const qreal w = source->width();
        const qreal h = source->height();

        QTransform t;
        t.translate(w / 2.0, h / 2.0);
        t.rotate(angleDegreesValue);
        t.translate(-w / 2.0, -h / 2.0);

        const QRectF br = t.mapRect(QRectF(0, 0, w, h));
        p.translate(-br.topLeft());
        p.setTransform(t, true);

        qreal oldOpacity = 1.0;
        QGraphicsOpacityEffect *op = qobject_cast<QGraphicsOpacityEffect *>(source->graphicsEffect());
        if (op) {
            oldOpacity = op->opacity();
            op->setOpacity(1.0);
        }

        source->render(&p);

        if (op) {
            op->setOpacity(oldOpacity);
        }
    }

private:
    OverlayTextEdit *source;
    qreal angleDegreesValue;
};
}

/**
 * @brief 构造函数
 * @param parent 父控件
 * @author chiangyang
 */
OverlayTextEdit::OverlayTextEdit(QWidget *parent)
    : FloatingWidget(parent),
      m_origFontSize(16),
      m_fontSize(16),
      m_editorColor(Qt::red),
      rotateButton(new QToolButton(parent)),
      m_isRotating(false),
      m_rotationPreviewActive(false),
      m_currentRotationDegrees(0.0),
      m_editorOpacityEffect(new QGraphicsOpacityEffect(this)),
      m_rotationPreview(new RotationPreviewWidget(this, parent)) {
    LOG_INFO("OverlayTextEdit instance created");
    updateStyleSheet();
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    setLineWrapMode(QTextEdit::NoWrap);

    rotateButton->setCursor(Qt::CrossCursor);
    rotateButton->setFocusPolicy(Qt::NoFocus);
    rotateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    rotateButton->setIconSize(QSize(14, 14));
    rotateButton->setFixedSize(QSize(18, 18));
    rotateButton->setAutoRaise(true);
    rotateButton->setToolTip(TranslationManager::instance()->get("overlay.rotate", "Rotate"));
    rotateButton->installEventFilter(this);
    rotateButton->hide();

    m_editorOpacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_editorOpacityEffect);

    m_rotationPreview->hide();

    connect(document(), &QTextDocument::contentsChanged, this, [this]() {
        if (m_rotationPreviewActive && !m_isRotating) {
            updateRotationPreview();
        }
        if (!isInteractionActive() && !m_isRotating) {
            adjustSizeToContent();
        }
    });
}

/**
 * @brief 析构函数
 * @author chiangyang
 */
OverlayTextEdit::~OverlayTextEdit() {
    if (rotateButton) {
        rotateButton->hide();
        rotateButton->deleteLater();
        rotateButton = nullptr;
    }
    if (m_rotationPreview) {
        m_rotationPreview->hide();
        m_rotationPreview->deleteLater();
        m_rotationPreview = nullptr;
    }
}

/**
 * @brief geometryChanged 载荷的旋转角度
 * @author chiangyang
 */
qreal OverlayTextEdit::currentRotation() const {
    return m_currentRotationDegrees;
}

/**
 * @brief 角缩放开始：快照字体原值作为等比缩放基准
 * @author chiangyang
 */
void OverlayTextEdit::onFloatingResizeStarted() {
    m_origFontSize = m_fontSize;
}

/**
 * @brief 角缩放：按对角线比例缩放字体
 * @author chiangyang
 */
void OverlayTextEdit::onFloatingScaled(qreal diagScaleFactor) {
    setFontSize(qMax(kMinFontSize, qRound(m_origFontSize * diagScaleFactor)));
}

/**
 * @brief 获取旋转角度
 * @author chiangyang
 */
qreal OverlayTextEdit::rotationDegrees() const {
    return m_currentRotationDegrees;
}

/**
 * @brief 设置编辑器字体大小
 * @author chiangyang
 */
void OverlayTextEdit::setFontSize(int px) {
    m_fontSize = px;
    updateStyleSheet();
}

/**
 * @brief 获取当前字体大小
 * @author chiangyang
 */
int OverlayTextEdit::fontSize() const {
    return m_fontSize;
}

/**
 * @brief 设置编辑器颜色
 * @author chiangyang
 */
void OverlayTextEdit::setEditorColor(const QColor &color) {
    m_editorColor = color;
    updateStyleSheet();
}

/**
 * @brief 更新样式表
 * @author chiangyang
 */
void OverlayTextEdit::updateStyleSheet() {
    setStyleSheet(StyleManager::getOverlayTextEditStyle(m_editorColor, m_fontSize));
}

/**
 * @brief 调整文本框大小以适应内容
 * @author chiangyang
 */
void OverlayTextEdit::adjustSizeToContent() {
    if (document()->isEmpty()) {
        QSize minSize(kMinWidth, kMinHeight);
        if (size() != minSize) {
            resize(minSize);
        }
        return;
    }

    int idealWidth = qMax(kMinWidth, qRound(document()->idealWidth())
        + static_cast<int>(frameWidth()) * 2
        + static_cast<int>(document()->documentMargin()) * 2);

    int contentHeight = qRound(document()->size().height())
        + static_cast<int>(frameWidth()) * 2;

    int newWidth = qMax(idealWidth, kMinWidth);
    int newHeight = qMax(contentHeight, kMinHeight);

    QSize newSize(newWidth, newHeight);
    if (size() != newSize) {
        resize(newSize);
    }
}

/**
 * @brief 大小变化事件处理（基类同步手柄覆盖层，此处叠加旋转子系统）
 * @author chiangyang
 */
void OverlayTextEdit::resizeEvent(QResizeEvent *event) {
    FloatingWidget::resizeEvent(event);
    updateRotateButtonPosition();
    if (m_rotationPreviewActive && !m_isRotating) {
        updateRotationPreview();
    }
}

/**
 * @brief 移动事件处理
 * @author chiangyang
 */
void OverlayTextEdit::moveEvent(QMoveEvent *event) {
    FloatingWidget::moveEvent(event);
    if (m_rotationPreviewActive) {
        updateRotationPreview();
    } else {
        updateRotateButtonPosition();
    }
}

/**
 * @brief 显示事件处理
 * @author chiangyang
 */
void OverlayTextEdit::showEvent(QShowEvent *event) {
    FloatingWidget::showEvent(event);
    if (rotateButton) {
        rotateButton->show();
        rotateButton->raise();
        if (m_rotationPreviewActive) {
            updateRotationPreview();
        } else {
            updateRotateButtonPosition();
        }
    }
}

/**
 * @brief 隐藏事件处理
 * @author chiangyang
 */
void OverlayTextEdit::hideEvent(QHideEvent *event) {
    if (rotateButton && !m_isRotating) {
        rotateButton->hide();
    }
    if (m_rotationPreview && !m_isRotating) {
        m_rotationPreview->hide();
    }
    FloatingWidget::hideEvent(event);
}

/**
 * @brief 更新旋转按钮位置
 * @author chiangyang
 */
void OverlayTextEdit::updateRotateButtonPosition() {
    if (!rotateButton || !parentWidget()) return;
    const QPoint topCenterInParent = mapToParent(QPoint(width() / 2, 0));
    const int gap = 6;
    const int x = topCenterInParent.x() - rotateButton->width() / 2;
    const int y = topCenterInParent.y() - rotateButton->height() - gap;
    rotateButton->move(x, y);
    rotateButton->raise();
}

/**
 * @brief 更新旋转预览
 * @author chiangyang
 */
void OverlayTextEdit::updateRotationPreview() {
    if (!m_rotationPreview || !parentWidget()) return;

    const qreal w = width();
    const qreal h = height();

    QTransform t;
    t.translate(w / 2.0, h / 2.0);
    t.rotate(m_currentRotationDegrees);
    t.translate(-w / 2.0, -h / 2.0);

    const QRectF br = t.mapRect(QRectF(0, 0, w, h));

    const QPointF srcCenter(w / 2.0, h / 2.0);
    const QPointF dstCenter = t.map(srcCenter) - br.topLeft();
    const QPointF centerInParent = mapToParent(rect().center());
    const QPointF topLeftInParent = centerInParent - dstCenter;

    m_rotationPreview->resize(br.size().toSize());
    m_rotationPreview->move(topLeftInParent.toPoint());
    m_rotationPreview->show();
    m_rotationPreview->raise();
    static_cast<RotationPreviewWidget *>(m_rotationPreview)->setAngleDegrees(m_currentRotationDegrees);
    if (rotateButton) {
        const qreal rad = qDegreesToRadians(m_currentRotationDegrees);
        const qreal c = std::cos(rad);
        const qreal s = std::sin(rad);

        const QPointF a(-w / 2.0, -h / 2.0);
        const QPointF b(w / 2.0, -h / 2.0);
        const QPointF aRot(a.x() * c - a.y() * s, a.x() * s + a.y() * c);
        const QPointF bRot(b.x() * c - b.y() * s, b.x() * s + b.y() * c);
        const QPointF mid = (aRot + bRot) * 0.5;
        const QPointF edge = bRot - aRot;

        QPointF n(-edge.y(), edge.x());
        const qreal nLen = std::hypot(n.x(), n.y());
        if (nLen > 0.0) n = QPointF(n.x() / nLen, n.y() / nLen);
        if ((n.x() * mid.x() + n.y() * mid.y()) < 0.0) n = QPointF(-n.x(), -n.y());

        const qreal gap = 6.0;
        const qreal btnHalf = std::max(rotateButton->width(), rotateButton->height()) / 2.0;
        const QPointF btnCenter = centerInParent + mid + n * (gap + btnHalf);
        rotateButton->move(QPoint(qRound(btnCenter.x() - rotateButton->width() / 2.0), qRound(btnCenter.y() - rotateButton->height() / 2.0)));
        rotateButton->raise();
    }
}

/**
 * @brief 事件过滤器（旋转按钮的按下/拖动/释放交互）
 * @author chiangyang
 */
bool OverlayTextEdit::eventFilter(QObject *watched, QEvent *event) {
    if (watched == rotateButton) {
        const auto normalizeDegrees = [](qreal deg) -> qreal {
            deg = std::fmod(deg, 360.0);
            if (deg < 0) deg += 360.0;
            return deg;
        };

        if (event->type() == QEvent::HoverEnter) {
            rotateButton->setCursor(Qt::ArrowCursor);
            return true;
        }

        if (event->type() == QEvent::HoverLeave) {
            rotateButton->setCursor(Qt::CrossCursor);
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton) {
                m_isRotating = true;
                m_rotateCenterGlobal = mapToGlobal(rect().center());
                m_rotatePressGlobal = e->globalPosition();
                setReadOnly(true);
                if (m_rotationPreviewActive) {
                    if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(0.0);
                    updateRotationPreview();
                }
                rotateButton->grabMouse();
                setCursor(Qt::CrossCursor);
                e->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_isRotating) {
                auto *e = static_cast<QMouseEvent *>(event);
                const QPointF p = e->globalPosition();
                const qreal dist = QLineF(m_rotatePressGlobal, p).length();
                const QPointF d = p - m_rotateCenterGlobal;
                const qreal rad = std::atan2(d.y(), d.x());
                const qreal deg = qRadiansToDegrees(rad) + 90.0;
                m_currentRotationDegrees = normalizeDegrees(deg);
                rotateButton->setToolTip(QString::number(m_currentRotationDegrees, 'f', 0) + QLatin1String("°"));
                if (!m_rotationPreviewActive && dist > 2.0) {
                    m_rotationPreviewActive = true;
                    if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(0.0);
                    updateRotationPreview();
                } else if (m_rotationPreviewActive) {
                    updateRotationPreview();
                }
                emit geometryChanged(geometry(), m_currentRotationDegrees);
                e->accept();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton) {
                const bool wasRotating = m_isRotating;
                m_isRotating = false;
                rotateButton->releaseMouse();
                unsetCursor();

                if (wasRotating) {
                    const qreal dist = QLineF(m_rotatePressGlobal, e->globalPosition()).length();
                    if (dist < 3.0) {
                        m_currentRotationDegrees = normalizeDegrees(m_currentRotationDegrees + 90.0);
                        rotateButton->setToolTip(QString::number(m_currentRotationDegrees, 'f', 0) + QLatin1String("°"));
                    }
                    if (m_rotationPreviewActive) {
                        updateRotationPreview();
                    } else {
                        if (m_editorOpacityEffect) m_editorOpacityEffect->setOpacity(1.0);
                        updateRotateButtonPosition();
                    }
                    setReadOnly(false);
                    e->accept();
                    return true;
                }
            }
        }
    }
    return FloatingWidget::eventFilter(watched, event);
}
