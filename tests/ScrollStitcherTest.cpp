/**
 * @file ScrollStitcherTest.cpp
 * @brief 滚动截图拼接器单元测试
 *
 * 覆盖垂直/水平拼接、固定表头与底栏、重复帧丢弃、无纹理画面拒绝、最大长度限制。
 * 纯 QImage 计算，无 GUI 依赖，使用 QTEST_GUILESS_MAIN。
 * @author chiangyang
 */
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <cstring>

#include "capture/ScrollStitcher.h"

namespace {

/**
 * @brief 生成具有唯一纹理的"长文档"图（每行/列像素值互不相同，避免周期误匹配）
 * @param width 宽度
 * @param height 高度
 * @return 文档图像
 * @author chiangyang
 */
QImage makeDocument(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int v = (y * 37 + x * 11 + (y * y) / 17 + (x * x) / 13) % 256;
            image.setPixel(x, y, qRgb(v, (v * 3) % 256, (v * 7) % 256));
        }
    }
    return image;
}

/**
 * @brief 生成白底稀疏纹理的"文档"图（模拟白底网页：少量文字行 + 大片留白）
 * @param width 宽度
 * @param height 高度
 * @return 文档图像
 * @author chiangyang
 */
QImage makeSparseDocument(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(qRgb(255, 255, 255));
    for (int y = 0; y < height; ++y) {
        // 每 23 行中只有 5 行有内容，其余为纯白留白
        if (y % 23 >= 5) continue;
        for (int x = 0; x < width; ++x) {
            const int v = (y * 37 + x * 11 + (y * y) / 17) % 180;
            image.setPixel(x, y, qRgb(v / 2, v / 2, v / 2));
        }
    }
    return image;
}

/**
 * @brief 从文档中截取一帧
 * @author chiangyang
 */
QImage slice(const QImage &doc, int x, int y, int width, int height) {
    return doc.copy(x, y, width, height);
}

bool imagesEqual(const QImage &a, const QImage &b) {
    if (a.size() != b.size()) return false;
    for (int y = 0; y < a.height(); ++y) {
        if (std::memcmp(a.constScanLine(y), b.constScanLine(y), a.width() * 4) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

class ScrollStitcherTest : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief 垂直滚动：逐帧滚动 50px，结果应与文档对应区域逐像素一致
     * @author chiangyang
     */
    void verticalStitch();

    /**
     * @brief 水平滚动：逐帧滚动 50px，结果应为横向长图
     * @author chiangyang
     */
    void horizontalStitch();

    /**
     * @brief 白底稀疏纹理页面：模板自适应上移，底部留白时仍能正确拼接
     * @author chiangyang
     */
    void whitePageStitch();

    /**
     * @brief 固定表头与底栏不干扰定位
     *
     * 每帧顶部、底部各有一段固定内容，只有中间区域随滚动变化；
     * 位移仍应正确求出，长图持续增长（不会因静止行卡住）。
     * @author chiangyang
     */
    void fixedHeaderFooterStitch();

    /**
     * @brief 未滚动时重复帧应被丢弃
     * @author chiangyang
     */
    void duplicateFrameIgnored();

    /**
     * @brief 纯色画面无纹理，无法可靠匹配，应返回 NoMatch
     * @author chiangyang
     */
    void texturelessFrameRejected();

    /**
     * @brief 超过最大长度时拒绝追加
     * @author chiangyang
     */
    void maxLengthRejected();
};

void ScrollStitcherTest::verticalStitch() {
    const int frameW = 120;
    const int frameH = 150;
    const int step = 50;
    const QImage doc = makeDocument(frameW, 400);

    ScrollStitcher stitcher(ScrollStitcher::Orientation::Vertical);
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    int offset = 0;
    for (int i = 1; i < 6; ++i) {
        offset += step;
        const auto result = stitcher.appendFrame(slice(doc, 0, offset, frameW, frameH));
        QVERIFY(result == ScrollStitcher::AppendResult::Appended);
    }

    QCOMPARE(stitcher.stitchedLength(), frameH + 5 * step);
    QCOMPARE(stitcher.result().size(), QSize(frameW, frameH + 5 * step));
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, 0, 0, frameW, frameH + 5 * step)));
}

void ScrollStitcherTest::horizontalStitch() {
    const int frameW = 150;
    const int frameH = 120;
    const int step = 50;
    const QImage doc = makeDocument(400, frameH);

    ScrollStitcher stitcher(ScrollStitcher::Orientation::Horizontal);
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    int offset = 0;
    for (int i = 1; i < 6; ++i) {
        offset += step;
        QVERIFY(stitcher.appendFrame(slice(doc, offset, 0, frameW, frameH))
                == ScrollStitcher::AppendResult::Appended);
    }

    const int expectedLength = frameW + 5 * step;
    QCOMPARE(stitcher.stitchedLength(), expectedLength);
    QCOMPARE(stitcher.result().size(), QSize(expectedLength, frameH));
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, 0, 0, expectedLength, frameH)));
}

void ScrollStitcherTest::whitePageStitch() {
    const int frameW = 120;
    const int frameH = 150;
    const int step = 50;
    const QImage doc = makeSparseDocument(frameW, 440);

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    int offset = 0;
    for (int i = 1; i < 6; ++i) {
        offset += step;
        QVERIFY(stitcher.appendFrame(slice(doc, 0, offset, frameW, frameH))
                == ScrollStitcher::AppendResult::Appended);
    }

    QCOMPARE(stitcher.stitchedLength(), frameH + 5 * step);
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, 0, 0, frameW, frameH + 5 * step)));
}

void ScrollStitcherTest::fixedHeaderFooterStitch() {
    const int frameW = 120;
    const int frameH = 160;
    const int step = 40;
    const int headerH = 20;
    const int footerH = 15;
    const int contentH = frameH - headerH - footerH;

    const QImage header = makeDocument(frameW, headerH);
    const QImage footer = makeSparseDocument(frameW, footerH);
    const QImage doc = makeDocument(frameW, 420);

    // 固定表头/底栏 + 中间滚动内容
    auto makeFrame = [&](int offset) {
        QImage frame(frameW, frameH, QImage::Format_RGB32);
        QPainter painter(&frame);
        painter.drawImage(0, 0, header);
        painter.drawImage(0, headerH, doc.copy(0, offset, frameW, contentH));
        painter.drawImage(0, headerH + contentH, footer);
        return frame;
    };

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(makeFrame(0))
            == ScrollStitcher::AppendResult::Appended);

    for (int i = 1; i < 5; ++i) {
        QCOMPARE(stitcher.appendFrame(makeFrame(i * step)),
                 ScrollStitcher::AppendResult::Appended);
        // 表头/底栏始终静止，若被当作定位依据会算成 0 位移而卡住
        QCOMPARE(stitcher.stitchedLength(), frameH + i * step);
    }
}

void ScrollStitcherTest::duplicateFrameIgnored() {
    const QImage doc = makeDocument(100, 300);
    const QImage frame = slice(doc, 0, 0, 100, 150);

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(frame) == ScrollStitcher::AppendResult::Appended);
    QVERIFY(stitcher.appendFrame(frame) == ScrollStitcher::AppendResult::Duplicate);
    QCOMPARE(stitcher.stitchedLength(), 150);
}

void ScrollStitcherTest::texturelessFrameRejected() {
    QImage solid(100, 150, QImage::Format_RGB32);
    solid.fill(qRgb(180, 180, 180));
    // 第二帧换成不同灰度：画面确有变化，但纯色区域仍没有可对齐的特征
    QImage otherSolid(100, 150, QImage::Format_RGB32);
    otherSolid.fill(qRgb(120, 120, 120));

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(solid) == ScrollStitcher::AppendResult::Appended);
    QVERIFY(stitcher.appendFrame(otherSolid) == ScrollStitcher::AppendResult::NoMatch);
    QCOMPARE(stitcher.stitchedLength(), 150);
}

void ScrollStitcherTest::maxLengthRejected() {
    const int frameH = 150;
    const QImage doc = makeDocument(100, 400);

    ScrollStitcher stitcher;
    stitcher.setMaxLength(180);
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 0, 100, frameH))
            == ScrollStitcher::AppendResult::Appended);
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 50, 100, frameH))
            == ScrollStitcher::AppendResult::LimitReached);
    QCOMPARE(stitcher.stitchedLength(), frameH);
}

QTEST_GUILESS_MAIN(ScrollStitcherTest)
#include "ScrollStitcherTest.moc"
