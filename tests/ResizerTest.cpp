#include <QtTest>
#include "capture/Resizer.h"

/**
 * @brief Resizer 选区几何的单元测试（header-only 纯逻辑）
 *
 * 固定选区管理的核心不变量：
 * - 反向构造的规范化（left/top/right/bottom 取 min/max，宽高含两端像素）
 * - range 钳制对 coords/adjust/translate/margins 四条修改路径全覆盖
 * - margins 向外为正的语义
 * - 8 个锚点的几何与顺序、relativePos（视觉语义）全命中
 * - absolutePos（原始坐标语义）在规范化输入下的全命中，以及翻转输入
 *   下边框检测的已知限制（锚点用视觉坐标、边框用原始坐标）
 * - getCursorByLocation 光标映射全分支
 * @author chiangyang
 */
class ResizerTest : public QObject {
    Q_OBJECT

private slots:
    void normalization_inclusivePixelSemantics() {
        Resizer r(200, 250, 100, 50);   // 反向构造（x1>x2, y1>y2）
        QCOMPARE(r.left(), 100);
        QCOMPARE(r.right(), 200);
        QCOMPARE(r.top(), 50);
        QCOMPARE(r.bottom(), 250);
        QCOMPARE(r.width(), 101);       // 宽高含两端像素（abs+1）
        QCOMPARE(r.height(), 201);
        QCOMPARE(r.rect(), QRect(100, 50, 101, 201));
        QCOMPARE(r.topLeft(), QPoint(100, 50));
        QCOMPARE(r.bottomRight(), QPoint(200, 250));
        QCOMPARE(r.topRight(), QPoint(200, 50));
        QCOMPARE(r.bottomLeft(), QPoint(100, 250));
        QCOMPARE(r.center(), QPoint(150, 150));
    }

    void range_clampsAllMutationPaths() {
        Resizer r(0, 0, 100, 100);
        r.range(QRect(0, 0, 500, 500));

        // coords 钳制（QRect(0,0,500,500) 的 right/bottom = 499，含端点语义）
        r.coords(-50, -50, 2000, 2000);
        QCOMPARE(r.left(), 0);
        QCOMPARE(r.right(), 499);
        QCOMPARE(r.top(), 0);
        QCOMPARE(r.bottom(), 499);

        // adjust 钳制
        r.coords(100, 100, 400, 400);
        r.adjust(-500, 0, 500, 0);
        QCOMPARE(r.left(), 0);
        QCOMPARE(r.right(), 499);

        // translate 整体平移钳制（不越出 range）
        r.coords(100, 100, 200, 200);
        r.translate(5000, 5000);
        QCOMPARE(r.right(), 499);
        QCOMPARE(r.bottom(), 499);
        QCOMPARE(r.width(), 101);       // 平移不改变尺寸

        // margins 扩展钳制
        r.margins(5000, 5000, 5000, 5000);
        QCOMPARE(r.left(), 0);
        QCOMPARE(r.top(), 0);
        QCOMPARE(r.right(), 499);
        QCOMPARE(r.bottom(), 499);
    }

    void margins_outwardPositive() {
        Resizer r(100, 100, 200, 200);
        r.margins(10, 10, 10, 10);      // 四边各向外 10
        QCOMPARE(r.left(), 90);
        QCOMPARE(r.right(), 210);
        QCOMPARE(r.top(), 90);
        QCOMPARE(r.bottom(), 210);
        r.margins(-5, -5, -5, -5);      // 四边各向内缩 5
        QCOMPARE(r.left(), 95);
        QCOMPARE(r.right(), 205);
        QCOMPARE(r.width(), 111);
    }

    void anchors_geometryAndOrder() {
        Resizer r(100, 100, 200, 200);  // anchor_w=7，半宽 3
        QCOMPARE(r.topLeftAnchor(), QRect(97, 97, 7, 7));
        QCOMPARE(r.topRightAnchor(), QRect(197, 97, 7, 7));
        QCOMPARE(r.bottomLeftAnchor(), QRect(97, 197, 7, 7));
        QCOMPARE(r.bottomRightAnchor(), QRect(197, 197, 7, 7));
        // 中点锚点中心位于四边中点
        QCOMPARE(r.topAnchor().center(), QPoint(150, 100));
        QCOMPARE(r.bottomAnchor().center(), QPoint(150, 200));
        QCOMPARE(r.leftAnchor().center(), QPoint(100, 150));
        QCOMPARE(r.rightAnchor().center(), QPoint(200, 150));
        // 角锚点中心恰在选区顶点上（历史修复：不再向外斜偏）
        QCOMPARE(r.topLeftAnchor().center(), QPoint(100, 100));
        // 顺序约定：右、上、下、左、四角（TL/TR/BL/BR）
        QCOMPARE(r.anchors().size(), 8);
        QCOMPARE(r.anchors().first(), r.rightAnchor());
        QCOMPARE(r.anchors().at(4), r.topLeftAnchor());
        QCOMPARE(r.anchors().last(), r.bottomRightAnchor());
        QCOMPARE(r.cornerAnchors().size(), 4);
        QCOMPARE(r.cornerAnchors().first(), r.topLeftAnchor());
    }

    void relativePos_allHits() {
        Resizer r(100, 100, 200, 200);
        // 8 锚点（角 + 边中点）
        QCOMPARE(r.relativePos(QPoint(100, 100)), ResizerLocation::TL_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(200, 100)), ResizerLocation::TR_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(100, 200)), ResizerLocation::BL_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(200, 200)), ResizerLocation::BR_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(100, 150)), ResizerLocation::L_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(200, 150)), ResizerLocation::R_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(150, 100)), ResizerLocation::T_ANCHOR);
        QCOMPARE(r.relativePos(QPoint(150, 200)), ResizerLocation::B_ANCHOR);
        // 边框带（border_w=5，向外延伸）；测试点避开锚点 7px 矩形覆盖区
        QCOMPARE(r.relativePos(QPoint(98, 120)), ResizerLocation::L_BORDER);
        QCOMPARE(r.relativePos(QPoint(202, 120)), ResizerLocation::R_BORDER);
        QCOMPARE(r.relativePos(QPoint(120, 98)), ResizerLocation::T_BORDER);
        QCOMPARE(r.relativePos(QPoint(120, 202)), ResizerLocation::B_BORDER);
        // 内部与外部
        QCOMPARE(r.relativePos(QPoint(150, 150)), ResizerLocation::EMPTY_INSIDE);
        QCOMPARE(r.relativePos(QPoint(50, 50)), ResizerLocation::OUTSIDE);
        // 边框带外沿之外才算 OUTSIDE（left-6 < x < left）
        QCOMPARE(r.relativePos(QPoint(94, 150)), ResizerLocation::OUTSIDE);
    }

    void absolutePos_normalizedBox_allHits() {
        Resizer r(100, 100, 200, 200);  // x1<x2, y1<y2，绝对语义与视觉一致
        QCOMPARE(r.absolutePos(QPoint(100, 100)), ResizerLocation::X1Y1_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(200, 100)), ResizerLocation::X2Y1_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(100, 200)), ResizerLocation::X1Y2_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(200, 200)), ResizerLocation::X2Y2_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(100, 150)), ResizerLocation::X1_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(200, 150)), ResizerLocation::X2_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(150, 100)), ResizerLocation::Y1_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(150, 200)), ResizerLocation::Y2_ANCHOR);
        QCOMPARE(r.absolutePos(QPoint(98, 120)), ResizerLocation::X1_BORDER);
        QCOMPARE(r.absolutePos(QPoint(120, 202)), ResizerLocation::Y2_BORDER);
        QCOMPARE(r.absolutePos(QPoint(150, 150)), ResizerLocation::EMPTY_INSIDE);
        QCOMPARE(r.absolutePos(QPoint(50, 50)), ResizerLocation::OUTSIDE);
    }

    void absolutePos_flippedBox_borderRawSemantics_knownLimitation() {
        // 反向原始坐标（FreeSelecting 向左上拖拽结束后的状态：只推 x2_/y2_，
        // 释放不规范化）。absolutePos 文档前提是 x1<x2（X1=左）；锚点检测
        // 用视觉 left()/right()、边框检测用原始 x1_/x2_，翻转输入时视觉
        // 左边框落到 OUTSIDE——固定现状作为已知限制（回归基线）。
        Resizer r(200, 200, 100, 100);
        QCOMPARE(r.left(), 100);
        QCOMPARE(r.absolutePos(QPoint(100, 100)), ResizerLocation::X1Y1_ANCHOR); // 视觉 TL 锚点仍命中
        QCOMPARE(r.absolutePos(QPoint(98, 120)), ResizerLocation::OUTSIDE);      // 视觉左边框（纯边框点）：现状 OUTSIDE
        // 视觉语义的 relativePos 不受翻转影响
        QCOMPARE(r.relativePos(QPoint(98, 120)), ResizerLocation::L_BORDER);
        QCOMPARE(r.relativePos(QPoint(100, 100)), ResizerLocation::TL_ANCHOR);
    }

    void cursorMapping_fullBranches() {
        QCOMPARE(getCursorByLocation(ResizerLocation::L_ANCHOR).shape(), Qt::SizeHorCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::R_BORDER).shape(), Qt::SizeHorCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::X1_ANCHOR).shape(), Qt::SizeHorCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::T_ANCHOR).shape(), Qt::SizeVerCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::B_BORDER).shape(), Qt::SizeVerCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::Y2_ANCHOR).shape(), Qt::SizeVerCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::TL_ANCHOR).shape(), Qt::SizeFDiagCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::BR_ANCHOR).shape(), Qt::SizeFDiagCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::X2Y2_ANCHOR).shape(), Qt::SizeFDiagCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::TR_ANCHOR).shape(), Qt::SizeBDiagCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::X1Y2_ANCHOR).shape(), Qt::SizeBDiagCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::EMPTY_INSIDE).shape(), Qt::SizeAllCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::OUTSIDE).shape(), Qt::ForbiddenCursor);
        // 未列出的枚举值回退到 defaultCursor 参数
        QCOMPARE(getCursorByLocation(ResizerLocation::DEFAULT).shape(), Qt::CrossCursor);
        QCOMPARE(getCursorByLocation(ResizerLocation::DEFAULT, Qt::ArrowCursor).shape(),
                 Qt::ArrowCursor);
    }
};

QTEST_MAIN(ResizerTest)
#include "ResizerTest.moc"
