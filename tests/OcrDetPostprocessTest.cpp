#include <QtTest>
#include <QPolygonF>
#include "ocr/OcrDetPostprocess.h"

/**
 * @brief OcrDetPostprocess 公共入口 process() 的单元测试
 *
 * 用合成 score map 验证"检测后处理"的端到端行为：
 * 高置信度亮块应产生文本区域多边形，全暗图不应产生任何区域。
 * 内部步骤（二值化 / 轮廓 / unclip）为私有实现，经公共入口覆盖。
 * @author chiangyang
 */
class OcrDetPostprocessTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 空输入：全暗 score map 不产生检测框 ----
    void process_darkMapYieldsNoBoxes() {
        const int H = 64, W = 64;
        QVector<float> scoreMap(H * W, 0.0f);

        const auto boxes = OcrDetPostprocess::process(
            scoreMap.constData(), H, W, W, H, W, H);
        QVERIFY2(boxes.isEmpty(), "全暗 score map 不应产生检测框");
    }

    // ---- 亮块：中心高置信度区域应产生一个检测框 ----
    void process_brightBlockYieldsOneBox() {
        const int H = 64, W = 64;
        QVector<float> scoreMap(H * W, 0.0f);
        // 在中心放一个 16x16 的高置信度方块
        for (int y = 24; y < 40; ++y) {
            for (int x = 24; x < 40; ++x) {
                scoreMap[y * W + x] = 0.9f;
            }
        }

        const auto boxes = OcrDetPostprocess::process(
            scoreMap.constData(), H, W, W, H, W, H);
        QCOMPARE(boxes.size(), 1);

        // 检测框应覆盖亮块区域（坐标在原尺寸坐标系内）
        const QRectF bbox = boxes.first().boundingRect();
        QVERIFY2(bbox.width() >= 10, qPrintable(QString("宽度过小: %1").arg(bbox.width())));
        QVERIFY2(bbox.height() >= 10, qPrintable(QString("高度过小: %1").arg(bbox.height())));
        QVERIFY(bbox.intersects(QRectF(20, 20, 24, 24)));
    }

    // ---- 两个分离的亮块：应产生两个检测框 ----
    void process_twoSeparateBlocksYieldTwoBoxes() {
        const int H = 64, W = 64;
        QVector<float> scoreMap(H * W, 0.0f);
        for (int y = 4; y < 16; ++y)
            for (int x = 4; x < 16; ++x) scoreMap[y * W + x] = 0.9f;
        for (int y = 44; y < 56; ++y)
            for (int x = 44; x < 56; ++x) scoreMap[y * W + x] = 0.9f;

        const auto boxes = OcrDetPostprocess::process(
            scoreMap.constData(), H, W, W, H, W, H);
        QCOMPARE(boxes.size(), 2);
    }
};

QTEST_GUILESS_MAIN(OcrDetPostprocessTest)
#include "OcrDetPostprocessTest.moc"
