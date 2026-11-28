#include "ColorLoupe.h"

#include "../core/ConfigManager.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace {

// 取色器布局：上方为正方形放大区（放大内容铺满），底部信息条紧贴在
// 正方形下边界之下（无缝相连、不遮挡放大内容），四行：颜色值 + 坐标 +
// 操作提示 + Esc 提示。整体为一张圆角卡片，白色外圈勾边。
constexpr int kSrcPixels = 15;                    ///< 每边显示的源像素数
constexpr int kZoom = 12;                         ///< 每个源像素的放大边长
constexpr int kLoupeSize = kSrcPixels * kZoom;    ///< 正方形放大区边长（180）
constexpr int kSideMargin = 1;                    ///< 卡片外的白色外圈宽度
constexpr int kBarLine1H = 26;                    ///< 信息条第一行（色块 + 颜色值）
constexpr int kBarPosH = 20;                      ///< 信息条第二行（光标坐标）
constexpr int kBarLine2H = 20;                    ///< 信息条第三行（操作提示）
constexpr int kBarLine3H = 18;                    ///< 信息条第四行（Esc 提示）
constexpr int kBarHeight = kBarLine1H + kBarPosH + kBarLine2H + kBarLine3H;
constexpr int kSwatchSize = 16;                   ///< 色块边长
constexpr int kBarPadL = 8;                       ///< 信息条左内边距
constexpr int kBarPadR = 8;                       ///< 信息条右内边距
constexpr int kBarGap = 9;                        ///< 色块与文字间距
constexpr int kValueFontPx = 12;                  ///< 颜色值字号
constexpr int kHintFontPx = 10;                   ///< 提示文字字号
constexpr int kCursorOffset = 24;                 ///< 放大镜与光标的默认间距

/**
 * @brief 构造圆角矩形路径（卡片底与裁剪区域共用）
 * @author chiangyang
 */
QPainterPath roundedRectPath(const QRect &rect, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);
    return path;
}

} // namespace

/**
 * @brief 构造函数
 * @param parent 宿主控件
 * @author chiangyang
 */
ColorLoupe::ColorLoupe(QWidget *parent)
    : QWidget(parent)
{
    // 对鼠标事件透明：mousemove/点击继续送达宿主（Selector / 取色覆盖窗口）
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // 宽高：正方形放大区 + 信息条，四周各 1px 白色外圈
    resize(kLoupeSize + 2 * kSideMargin,
           kLoupeSize + kBarHeight + 2 * kSideMargin);
    setVisible(false);

    // 恢复上次使用的取色格式（独立取色器与截图选区共用一份配置）
    const QString saved = ConfigManager::instance()->value("colorPicker/format", "hex").toString();
    m_format = colorFormatFromString(saved);
}

/**
 * @brief 设置取色源（冻结截图及其覆盖的全局几何）
 * @author chiangyang
 */
void ColorLoupe::setColorSource(const QPixmap &pixmap, const QRect &globalGeometry) {
    if (pixmap.isNull()) {
        m_hasSource = false;
        m_valid = false;
        setVisible(false);
        return;
    }

    m_dpr = pixmap.devicePixelRatio();
    if (m_dpr <= 0)
        m_dpr = 1.0;
    m_source = pixmap.toImage();
    m_sourceGeometry = globalGeometry;
    m_hasSource = true;
}

/**
 * @brief 设置信息条第二行的操作提示文字
 * @author chiangyang
 */
void ColorLoupe::setHintText(const QString &text) {
    if (m_hint == text)
        return;
    m_hint = text;
    update();
}

/**
 * @brief 设置信息条第三行的 Esc 提示文字
 * @author chiangyang
 */
void ColorLoupe::setEscHint(const QString &text) {
    if (m_escHint == text)
        return;
    m_escHint = text;
    update();
}

/**
 * @brief 正方形放大区矩形（控件内坐标，信息条在其下方）
 * @author chiangyang
 */
QRect ColorLoupe::loupeContentRect() const {
    return QRect(kSideMargin, kSideMargin, kLoupeSize, kLoupeSize);
}

/**
 * @brief 更新光标全局位置：采样 + 重新定位 + 重绘
 * @author chiangyang
 */
void ColorLoupe::setCursorGlobal(const QPoint &globalPos) {
    m_cursor = globalPos;

    if (!m_hasSource || m_source.isNull()) {
        if (isVisible())
            setVisible(false);
        return;
    }

    // 全局逻辑坐标 -> 源图像物理像素坐标（约定同 SnipScreen::grabVirtualDesktop）
    const QPointF rel = QPointF(globalPos - m_sourceGeometry.topLeft()) * m_dpr;
    const QPoint pixel(qFloor(rel.x()), qFloor(rel.y()));

    m_valid = m_source.rect().contains(pixel);
    if (m_valid)
        m_color = m_source.pixelColor(pixel);

    if (!m_valid) {
        if (isVisible())
            setVisible(false);
        return;
    }

    reposition();
    if (!isVisible())
        setVisible(true);
    update();
}

/**
 * @brief 切换到下一种颜色格式并持久化到配置
 * @author chiangyang
 */
void ColorLoupe::cycleFormat() {
    const ColorFormat next = static_cast<ColorFormat>((static_cast<int>(m_format) + 1) % 3);
    if (next == m_format)
        return;

    m_format = next;
    ConfigManager::instance()->setValue("colorPicker/format", colorFormatToString(m_format));
    ConfigManager::instance()->sync();
    emit formatChanged(m_format);
    update();
}

/**
 * @brief 依据光标位置计算自身位置，超出宿主边界时向内翻转
 * @author chiangyang
 */
void ColorLoupe::reposition() {
    QWidget *host = parentWidget();
    if (!host)
        return;

    const QPoint localCursor = host->mapFromGlobal(m_cursor);
    const QRect bounds = host->rect();

    // 默认放在光标右下方
    QPoint topLeft = localCursor + QPoint(kCursorOffset, kCursorOffset);
    // 右/下越界时翻转到光标左/上方
    if (topLeft.x() + width() > bounds.right())
        topLeft.setX(localCursor.x() - kCursorOffset - width());
    if (topLeft.y() + height() > bounds.bottom())
        topLeft.setY(localCursor.y() - kCursorOffset - height());
    // 翻转后仍越界（光标贴边）则钳制到宿主边界内
    topLeft.setX(qBound(bounds.left(), topLeft.x(), bounds.right() - width() + 1));
    topLeft.setY(qBound(bounds.top(), topLeft.y(), bounds.bottom() - height() + 1));

    move(topLeft);
}

/**
 * @brief 绘制取色器：白色外圈 + 圆角卡片（正方形放大区 + 底部四行信息条）
 *
 * 信息条紧贴在正方形下边界之下，不遮挡放大内容；放大内容裁剪到
 * 卡片圆角内，任何描边都不覆盖可见的放大像素。
 * @author chiangyang
 */
void ColorLoupe::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // 正方形放大区与整张卡片（放大区 + 信息条）
    const QRect content = loupeContentRect();
    const QRect card(kSideMargin, kSideMargin,
                     kLoupeSize, kLoupeSize + kBarHeight);
    const QRect barRect(kSideMargin, content.bottom() + 1, kLoupeSize, kBarHeight);

    // ---------- 白色外圈（卡片外 1px 的圆角描边，不覆盖卡片像素） ----------
    QPainterPath ringPath;
    ringPath.addRoundedRect(QRectF(card.adjusted(-1, -1, 1, 1)), 5, 5);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.strokePath(ringPath, QPen(QColor(255, 255, 255, 190), 1));

    // ---------- 卡片底（放大区 + 信息条连成一张圆角卡） ----------
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 20, 20, 215));
    painter.drawPath(roundedRectPath(card, 4));

    // ---------- 放大内容：铺满正方形放大区（裁剪到卡片圆角内） ----------
    // 源矩形：以取色像素为中心的 kSrcPixels × kSrcPixels 物理像素块。
    // 光标贴近屏幕边缘时源矩形会越界：Qt 会把裁剪后的源拉伸到整个目标区，
    // 故此处按比例映射到目标子矩形，越界区域保持卡片底色。
    const QPointF rel = QPointF(m_cursor - m_sourceGeometry.topLeft()) * m_dpr;
    const QPoint center(qFloor(rel.x()), qFloor(rel.y()));
    const QRect srcRect(center.x() - kSrcPixels / 2, center.y() - kSrcPixels / 2,
                        kSrcPixels, kSrcPixels);
    const QRect srcClipped = srcRect.intersected(m_source.rect());

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.save();
    painter.setClipPath(roundedRectPath(card, 4));
    if (!srcClipped.isEmpty()) {
        const qreal scaleX = qreal(content.width()) / srcRect.width();
        const qreal scaleY = qreal(content.height()) / srcRect.height();
        const QRectF dst(content.left() + (srcClipped.left() - srcRect.left()) * scaleX,
                         content.top() + (srcClipped.top() - srcRect.top()) * scaleY,
                         srcClipped.width() * scaleX,
                         srcClipped.height() * scaleY);
        painter.drawImage(dst, m_source, srcClipped);
    }

    // 像素网格（淡白色细分线，便于辨认像素块）
    painter.setPen(QPen(QColor(255, 255, 255, 36), 1));
    for (int i = 1; i < kSrcPixels; ++i) {
        const qreal x = content.left() + i * kZoom + 0.5;
        const qreal y = content.top() + i * kZoom + 0.5;
        painter.drawLine(QPointF(x, content.top()), QPointF(x, content.top() + kLoupeSize));
        painter.drawLine(QPointF(content.left(), y), QPointF(content.left() + kLoupeSize, y));
    }

    // 中心取色像素高亮框（白外框 + 深色内框，深浅色内容都可见）
    const QRect centerCell(content.left() + (kSrcPixels / 2) * kZoom,
                           content.top() + (kSrcPixels / 2) * kZoom, kZoom, kZoom);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawRect(centerCell.adjusted(1, 1, -1, -1));
    painter.setPen(QPen(QColor(0, 0, 0, 180), 1));
    painter.drawRect(centerCell);
    painter.restore();

    // ---------- 信息条第一行：色块 + 颜色值 ----------
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRect swatch(barRect.left() + kBarPadL, barRect.top() + 5, kSwatchSize, kSwatchSize);
    painter.setPen(QPen(QColor(255, 255, 255, 130), 1));
    painter.setBrush(m_color);
    painter.drawRect(swatch);

    QFont valueFont = painter.font();
    valueFont.setPixelSize(kValueFontPx);
    painter.setFont(valueFont);
    const QRect valueRect(swatch.right() + 1 + kBarGap, barRect.top(),
                          content.right() - kBarPadR - (swatch.right() + 1 + kBarGap),
                          kBarLine1H);
    // 防御性省略：宽度按最长颜色串预留，极端字体度量下不至于溢出
    const QString elided = painter.fontMetrics().elidedText(
        colorString(), Qt::ElideRight, valueRect.width());
    painter.setPen(Qt::white);
    painter.drawText(valueRect, Qt::AlignVCenter | Qt::AlignLeft, elided);

    // ---------- 信息条第二行：光标坐标 ----------
    QFont posFont = painter.font();
    posFont.setPixelSize(kHintFontPx);
    painter.setFont(posFont);
    const QRect posRect(barRect.left() + kBarPadL, barRect.top() + kBarLine1H,
                        kLoupeSize - kBarPadL - kBarPadR, kBarPosH);
    painter.setPen(QColor(255, 255, 255, 200));
    painter.drawText(posRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("x:%1  y:%2").arg(m_cursor.x()).arg(m_cursor.y()));

    // ---------- 信息条第三行：操作提示 ----------
    if (!m_hint.isEmpty()) {
        QFont hintFont = painter.font();
        hintFont.setPixelSize(kHintFontPx);
        painter.setFont(hintFont);
        const QRect hintRect(barRect.left() + kBarPadL,
                             barRect.top() + kBarLine1H + kBarPosH,
                             kLoupeSize - kBarPadL - kBarPadR, kBarLine2H);
        const QString elidedHint = painter.fontMetrics().elidedText(
            m_hint, Qt::ElideRight, hintRect.width());
        painter.setPen(QColor(255, 255, 255, 150));
        painter.drawText(hintRect, Qt::AlignVCenter | Qt::AlignLeft, elidedHint);
    }

    // ---------- 信息条第四行：Esc 提示 ----------
    if (!m_escHint.isEmpty()) {
        QFont hintFont = painter.font();
        hintFont.setPixelSize(kHintFontPx);
        painter.setFont(hintFont);
        const QRect escRect(barRect.left() + kBarPadL,
                            barRect.top() + kBarLine1H + kBarPosH + kBarLine2H,
                            kLoupeSize - kBarPadL - kBarPadR, kBarLine3H);
        const QString elidedEsc = painter.fontMetrics().elidedText(
            m_escHint, Qt::ElideRight, escRect.width());
        painter.setPen(QColor(255, 255, 255, 150));
        painter.drawText(escRect, Qt::AlignVCenter | Qt::AlignLeft, elidedEsc);
    }
}
