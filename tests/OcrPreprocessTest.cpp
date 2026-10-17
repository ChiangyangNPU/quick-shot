#include <QtTest>
#include "ocr/OcrPreprocess.h"

#include <cmath>

/**
 * @brief OcrPreprocess 预处理的单元测试
 *
 * 纯计算测试，防止 resize/归一化/CHW 转换被改坏：
 * - det：长边 1920 限制、32 对齐、最小 32 钳制
 * - rec：高度固定 48、宽度 32~640 钳制
 * - 归一化 (pixel/255 - mean)/std、RGB→BGR 通道交换、CHW 布局
 *   （rec 32x48 输入 ratio=1 且 Qt 同尺寸 scaled 不重采样，可精确断言像素值）
 * @author chiangyang
 */
class OcrPreprocessTest : public QObject {
    Q_OBJECT

    static QImage makeSolidRgb(int w, int h, int r, int g, int b) {
        QImage img(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            uchar *line = img.bits() + y * img.bytesPerLine();
            for (int x = 0; x < w; ++x) {
                line[x * 3 + 0] = static_cast<uchar>(r);
                line[x * 3 + 1] = static_cast<uchar>(g);
                line[x * 3 + 2] = static_cast<uchar>(b);
            }
        }
        return img;
    }

private slots:
    void det_resizesAndAlignsTo32() {
        // 小图：100x50 → 长边不超限 ratio=1 → 对齐 32 → 96x32
        {
            const auto r = OcrPreprocess::preprocessForDet(makeSolidRgb(100, 50, 0, 0, 0));
            QCOMPARE(r.resizedW, 96);
            QCOMPARE(r.resizedH, 32);
            QCOMPARE(static_cast<int>(r.data.size()), 3 * 32 * 96);
        }
        // 大图：4000x2000 → 长边压到 1920 → 1920x960
        {
            const auto r = OcrPreprocess::preprocessForDet(makeSolidRgb(4000, 2000, 0, 0, 0));
            QCOMPARE(r.resizedW, 1920);
            QCOMPARE(r.resizedH, 960);
        }
        // 极小图：双向钳到 32
        {
            const auto r = OcrPreprocess::preprocessForDet(makeSolidRgb(10, 5, 0, 0, 0));
            QCOMPARE(r.resizedW, 32);
            QCOMPARE(r.resizedH, 32);
        }
    }

    void rec_resizesTo48HeightAndClampsWidth() {
        // 高 24 → 放大到 48，宽 192x2=384
        {
            const auto r = OcrPreprocess::preprocessForRec(makeSolidRgb(192, 24, 0, 0, 0));
            QCOMPARE(r.resizedH, 48);
            QCOMPARE(r.resizedW, 384);
        }
        // 超宽钳到 640：2000x100 → ratio=0.48 → 960 → 640
        {
            const auto r = OcrPreprocess::preprocessForRec(makeSolidRgb(2000, 100, 0, 0, 0));
            QCOMPARE(r.resizedW, 640);
            QCOMPARE(r.resizedH, 48);
        }
        // 窄钳到 32：4x100 → ratio=0.48 → 1 → 32
        {
            const auto r = OcrPreprocess::preprocessForRec(makeSolidRgb(4, 100, 0, 0, 0));
            QCOMPARE(r.resizedW, 32);
        }
    }

    void rec_normalizesAndConvertsToCHW_BGR() {
        // 32x48 输入：recW=32（ratio=1，同尺寸 scaled 不重采样），可精确断言
        QImage img(32, 48, QImage::Format_RGB888);
        auto setPx = [&](int x, int y, int r, int g, int b) {
            uchar *line = img.bits() + y * img.bytesPerLine();
            line[x * 3 + 0] = static_cast<uchar>(r);
            line[x * 3 + 1] = static_cast<uchar>(g);
            line[x * 3 + 2] = static_cast<uchar>(b);
        };
        setPx(0, 0, 255, 128, 0);    // R=255 G=128 B=0
        setPx(31, 0, 10, 20, 30);    // R=10 G=20 B=30

        const auto r = OcrPreprocess::preprocessForRec(img);
        QCOMPARE(r.resizedW, 32);
        QCOMPARE(r.resizedH, 48);

        const auto norm = [](int px) { return (px / 255.0f - 0.5f) / 0.5f; };
        // CHW 平面大小 = 48*32；channel0=B、channel1=G、channel2=R
        const int plane = 48 * 32;
        QCOMPARE(static_cast<int>(r.data.size()), 3 * plane);
        // 像素 (0,0)：B=0 → -1；G=128 → ≈0.00392；R=255 → +1
        QVERIFY2(qAbs(r.data[0] - norm(0)) < 1e-4f, "ch0(0,0) B=0");
        QVERIFY2(qAbs(r.data[plane] - norm(128)) < 1e-4f, "ch1(0,0) G=128");
        QVERIFY2(qAbs(r.data[2 * plane] - norm(255)) < 1e-4f, "ch2(0,0) R=255");
        // 像素 (31,0)：平面内线性索引 = y*32 + 31 = 31
        QVERIFY2(qAbs(r.data[31] - norm(30)) < 1e-4f, "ch0(31,0) B=30");
        QVERIFY2(qAbs(r.data[plane + 31] - norm(20)) < 1e-4f, "ch1(31,0) G=20");
        QVERIFY2(qAbs(r.data[2 * plane + 31] - norm(10)) < 1e-4f, "ch2(31,0) R=10");
    }

    void det_usesImageNetParams() {
        // 纯色 32x32：R=255 G=128 B=0，同尺寸不重采样，逐通道精确断言
        const auto r = OcrPreprocess::preprocessForDet(makeSolidRgb(32, 32, 255, 128, 0));
        QCOMPARE(r.resizedW, 32);
        QCOMPARE(r.resizedH, 32);
        const auto norm = [](float px, float mean, float std) {
            return (px / 255.0f - mean) / std;
        };
        const int plane = 32 * 32;  // CHW 平面大小
        // channel0 = B=0 → (0-0.485)/0.229（纯色图全平面同值）
        QVERIFY2(qAbs(r.data[0] - norm(0.0f, 0.485f, 0.229f)) < 1e-4f, "det ch0 B=0");
        // channel1 = G=128 → (128/255-0.456)/0.224
        QVERIFY2(qAbs(r.data[plane] - norm(128.0f, 0.456f, 0.224f)) < 1e-4f, "det ch1 G=128");
        // channel2 = R=255 → (1-0.406)/0.225
        QVERIFY2(qAbs(r.data[2 * plane] - norm(255.0f, 0.406f, 0.225f)) < 1e-4f, "det ch2 R=255");
    }
};

QTEST_GUILESS_MAIN(OcrPreprocessTest)
#include "OcrPreprocessTest.moc"
