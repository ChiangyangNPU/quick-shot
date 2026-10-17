#include <QtTest>
#include "capture/AnnotationInteractionHandler.h"
#include "capture/Annotation.h"
#include "capture/AnnotationManager.h"

/**
 * @brief 马赛克分支两级绘制缓存（drawWithMosaic）的输出等价性测试
 *
 * 不暴露缓存内部，直接比对渲染输出的像素：
 * - 同状态重复渲染 → 逐字节一致（缓存复用稳定）
 * - 新增马赛克笔迹 → 旧笔迹区域像素不变（全局像素化块对齐全局原点），
 *   新笔迹区域像素变化
 * - 标注内容变化（contentVersion 提升）→ 输出变化（基础层重建）
 * - 背景更换（cacheKey 变化）→ 输出变化
 * - 拉伸模式（PinWindow 场景）背景铺满目标
 * @author chiangyang
 */
class MosaicPaintCacheTest : public QObject {
    Q_OBJECT

    static QPixmap makeBackground(int w, int h) {
        QPixmap pm(w, h);
        pm.fill(QColor(40, 80, 120));
        QPainter p(&pm);
        p.setPen(QPen(Qt::white, 3));
        for (int i = -h; i < w; i += 17) {   // 斜线纹理，保证像素化有细节可打
            p.drawLine(i, 0, i + h, h);
        }
        p.end();
        return pm;
    }

    static QImage render(AnnotationInteractionHandler &h, const QPixmap &bg,
                         const QSize &size, const QPoint &offset = QPoint(0, 0),
                         bool stretch = false) {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        h.drawWithMosaic(p, bg, size, AnnotationManager::kDefaultMosaicBlockSize,
                         offset, stretch);
        p.end();
        return img;
    }

private slots:
    void sameStateRepaint_isByteIdentical() {
        AnnotationInteractionHandler h;
        const QPixmap bg = makeBackground(200, 150);
        h.manager().mosaicAt(QPoint(40, 40), 10);
        h.manager().endMosaicStroke();

        const QImage f1 = render(h, bg, QSize(200, 150));
        const QImage f2 = render(h, bg, QSize(200, 150));
        QCOMPARE(f1, f2);
    }

    void newStroke_oldRegionUnchanged_newRegionChanged() {
        AnnotationInteractionHandler h;
        const QPixmap bg = makeBackground(200, 150);
        h.manager().mosaicAt(QPoint(40, 40), 10);
        h.manager().endMosaicStroke();
        const QImage before = render(h, bg, QSize(200, 150));

        // 新增第二笔（远离第一笔）
        h.manager().mosaicAt(QPoint(140, 100), 10);
        h.manager().endMosaicStroke();
        const QImage after = render(h, bg, QSize(200, 150));

        QVERIFY(before != after);
        // 旧笔迹区域：全局像素化块对齐全局原点，重算后像素不变
        QCOMPARE(after.copy(0, 0, 80, 80), before.copy(0, 0, 80, 80));
        // 新笔迹区域：出现马赛克（与基础层不同）
        QVERIFY(after.copy(110, 70, 60, 60) != before.copy(110, 70, 60, 60));
    }

    void contentChange_rebuildsBase() {
        AnnotationInteractionHandler h;
        const QPixmap bg = makeBackground(200, 150);
        h.manager().mosaicAt(QPoint(40, 40), 10);
        h.manager().endMosaicStroke();
        const QImage before = render(h, bg, QSize(200, 150));

        // 新增一个矩形标注（contentVersion 提升）
        auto rect = std::make_unique<RectAnnotation>(QPoint(10, 10), QColor(Qt::red), 3);
        rect->setEnd(QPoint(60, 60));
        h.manager().add(std::move(rect));
        h.manager().commit();
        const QImage after = render(h, bg, QSize(200, 150));

        QVERIFY(before != after);
    }

    void backgroundChange_rebuildsBase() {
        AnnotationInteractionHandler h;
        h.manager().mosaicAt(QPoint(40, 40), 10);
        h.manager().endMosaicStroke();

        const QPixmap bg1 = makeBackground(200, 150);
        const QImage f1 = render(h, bg1, QSize(200, 150));
        // 不同纹理的背景（cacheKey 不同）
        QPixmap bg2(200, 150);
        bg2.fill(QColor(200, 60, 30));
        const QImage f2 = render(h, bg2, QSize(200, 150));

        QVERIFY(f1 != f2);
    }

    void stretchMode_fillsTarget() {
        AnnotationInteractionHandler h;
        const QPixmap bg = makeBackground(100, 100);
        h.manager().mosaicAt(QPoint(20, 20), 8);
        h.manager().endMosaicStroke();

        // 拉伸模式：100x100 背景铺满 200x150 目标（PinWindow 缩放场景）
        const QImage stretched = render(h, bg, QSize(200, 150), QPoint(0, 0), true);
        const QImage oneToOne = render(h, bg, QSize(200, 150), QPoint(0, 0), false);
        // 直接断言：拉伸模式右下角非透明有纹理（1:1 模式该处越出背景，为透明）
        QVERIFY(stretched.copy(150, 100, 40, 40) != oneToOne.copy(150, 100, 40, 40));
    }

    void clearPaintCaches_releasesAndStillRenders() {
        AnnotationInteractionHandler h;
        const QPixmap bg = makeBackground(200, 150);
        h.manager().mosaicAt(QPoint(40, 40), 10);
        h.manager().endMosaicStroke();
        const QImage f1 = render(h, bg, QSize(200, 150));

        h.clearPaintCaches();
        const QImage f2 = render(h, bg, QSize(200, 150));
        QCOMPARE(f1, f2);   // 缓存清空后重建结果一致
    }
};

QTEST_MAIN(MosaicPaintCacheTest)
#include "MosaicPaintCacheTest.moc"
