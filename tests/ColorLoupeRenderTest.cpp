#include "capture/ColorLoupe.h"

#include <QImage>
#include <QPixmap>
#include <QtTest/QtTest>
#include <memory>

/**
 * @brief 取色放大镜渲染回归测试
 *
 * 背景：放大镜边框曾直接画在 loupeRect 上，描边压进最外圈放大像素
 * （且上边线因落在控件外被裁掉，形成"左右下边缘发暗、顶部正常"的
 * 错色）。本测试用纯白源图渲染放大镜，断言可见的放大像素不被
 * 任何边框/卡片元素覆盖。
 *
 * 布局：上方矩形放大区（内容区 = loupeContentRect()，即
 * QRect(1,1,180,120)，宽 180 × 高 120），放大内容铺满矩形；下方
 * 信息条（四行：颜色值 + 坐标 + 操作提示 + Esc 提示，共 kBarHeight
 * 像素）与放大区为两个独立圆角矩形（圆角半径 8，中间 kGap 间隙）；
 * 白色外圈在各元素外 1px。
 * @author chiangyang
 */
class ColorLoupeRenderTest : public QObject {
    Q_OBJECT

private slots:
    /// 可见的放大像素不被边框/信息条污染；信息条区域保持深色
    void contentEdgesNotCoveredByBorder();

    /// 源矩形越界时按比例裁剪映射：有效源区映射到对应子区域，
    /// 其余保持卡片底色，不允许拉伸填满或假色填充
    void sourceEdgeClipping();

private:
    ColorLoupe *makeLoupe();
};

namespace {
/// 底部信息条高度（与 ColorLoupe.cpp 的 kBarHeight 一致）
constexpr int kBarHeight = 84;
/// 每个源像素的放大边长（与 ColorLoupe.cpp 的 kZoom 一致）
constexpr int kZoom = 12;
/// 放大区与信息条的间隙（与 ColorLoupe.cpp 的 kGap 一致）
constexpr int kGap = 6;

/// 离屏渲染放大镜并转为非预乘 ARGB，便于按颜色断言
/// （预乘格式下半透明白圈的红分量 ≈ alpha×255，断言易误判）
QImage renderLoupe(ColorLoupe *loupe) {
    QImage canvas(loupe->size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    loupe->render(&canvas);
    return canvas.convertToFormat(QImage::Format_ARGB32);
}
} // namespace

ColorLoupe *ColorLoupeRenderTest::makeLoupe() {
    // 纯白源图（200×200），几何 (0,0)-(200,200)，dpr=1
    QImage source(200, 200, QImage::Format_RGB32);
    source.fill(QColor(255, 255, 255));

    ColorLoupe *loupe = new ColorLoupe();
    loupe->setColorSource(QPixmap::fromImage(source), QRect(0, 0, 200, 200));
    // 带提示文字，覆盖三行信息条布局（操作提示 + Esc 提示）
    loupe->setHintText(QStringLiteral("Wheel: format · Click: copy"));
    loupe->setEscHint(QStringLiteral("Esc: exit"));
    return loupe;
}

/**
 * 纯白源图 + 光标居中：放大像素（含四边最外圈）应为白色，
 * 放大区下方的信息条区域应为深色。旧实现中边框描边压进内容边缘
 * （左/右/下），左/右/下边缘断言会失败。
 */
void ColorLoupeRenderTest::contentEdgesNotCoveredByBorder() {
    std::unique_ptr<ColorLoupe> loupe(makeLoupe());
    loupe->setCursorGlobal(QPoint(100, 100));

    const QImage canvas = renderLoupe(loupe.get());
    QVERIFY(canvas.size() == loupe->size());

    const QRect content = loupe->loupeContentRect();
    QVERIFY(content.width() == 180 && content.height() == 120);

    // 取样点：放大区四边最外圈、避开网格线（content.left()+i*12+0.5）
    // 与中心高亮框；信息条在放大区下方（隔 kGap 间隙），四边完整可见
    const QPoint samples[] = {
        QPoint(content.left(),  content.top() + 30),      // 左边缘
        QPoint(content.right(), content.top() + 30),      // 右边缘
        QPoint(content.left() + 30, content.top()),       // 上边缘
        QPoint(content.left() + 30, content.bottom()),    // 下边缘
        QPoint(content.left() + 4,  content.bottom() - 2),// 左下角内侧
        QPoint(content.right() - 4, content.bottom() - 2),// 右下角内侧
    };
    for (const QPoint &p : samples) {
        const QRgb px = canvas.pixel(p);
        QVERIFY2(qRed(px) >= 240 && qGreen(px) >= 240 && qBlue(px) >= 240,
                 qPrintable(QStringLiteral("content edge pixel (%1,%2) contaminated: rgb(%3,%4,%5)")
                     .arg(p.x()).arg(p.y()).arg(qRed(px)).arg(qGreen(px)).arg(qBlue(px))));
    }

    // 信息条区域（放大区下方，隔间隙后 4px 处）应保持深色
    const QRgb bar = canvas.pixel(QPoint(content.left() + 4,
                                         content.bottom() + 1 + kGap + 4));
    QVERIFY2(qAlpha(bar) > 0 && qRed(bar) < 80 && qGreen(bar) < 80 && qBlue(bar) < 80,
             "info bar area should stay dark");

    // 白色外圈在元素外 1px（x=0，四边均在控件内不被裁掉）
    const QRgb ring = canvas.pixel(QPoint(content.left() - 1, content.top() + 30));
    QVERIFY2(qRed(ring) >= 200 && qGreen(ring) >= 200 && qBlue(ring) >= 200,
             qPrintable(QStringLiteral("white ring pixel rgb(%1,%2,%3) alpha %4")
                 .arg(qRed(ring)).arg(qGreen(ring)).arg(qBlue(ring)).arg(qAlpha(ring))));
}

/**
 * 光标贴近源图左上角（srcRect 越界）：取色像素 (2,2) 对应源区
 * 列 -5..9、行 -3..6，有效部分（列 0..9、行 0..6）映射到放大区
 * 第 5~14 列、第 3~9 行（右下子区域），左上越界区保持卡片底色，
 * 不做白色兜底填充。
 */
void ColorLoupeRenderTest::sourceEdgeClipping() {
    std::unique_ptr<ColorLoupe> loupe(makeLoupe());
    loupe->setCursorGlobal(QPoint(2, 2));

    const QImage canvas = renderLoupe(loupe.get());
    const QRect content = loupe->loupeContentRect();

    // 有效映射区内（第 8 列/行格中部）= 白色
    const QPoint inside(content.left() + 8 * kZoom + 6, content.top() + 8 * kZoom + 6);
    const QRgb pxIn = canvas.pixel(inside);
    QVERIFY2(qRed(pxIn) >= 240 && qGreen(pxIn) >= 240 && qBlue(pxIn) >= 240,
             qPrintable(QStringLiteral("mapped area pixel (%1,%2) rgb(%3,%4,%5)")
                 .arg(inside.x()).arg(inside.y())
                 .arg(qRed(pxIn)).arg(qGreen(pxIn)).arg(qBlue(pxIn))));

    // 越界区（第 2 列/行格中部）= 深色卡片底
    const QPoint outside(content.left() + 2 * kZoom + 6, content.top() + 2 * kZoom + 6);
    const QRgb pxOut = canvas.pixel(outside);
    QVERIFY2(qRed(pxOut) < 80 && qGreen(pxOut) < 80 && qBlue(pxOut) < 80,
             qPrintable(QStringLiteral("unmapped area pixel (%1,%2) rgb(%3,%4,%5)")
                 .arg(outside.x()).arg(outside.y())
                 .arg(qRed(pxOut)).arg(qGreen(pxOut)).arg(qBlue(pxOut))));
}

QTEST_MAIN(ColorLoupeRenderTest)
#include "ColorLoupeRenderTest.moc"
