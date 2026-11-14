/**
 * @file ScrollStitcherTest.cpp
 * @brief 滚动截图拼接器单元测试
 *
 * 覆盖垂直/水平拼接、正反向滚动、固定表头与底栏、重复帧丢弃、无纹理
 * 画面拒绝、最大长度限制、页面跳转/快速滚动后的基准重设，以及亚像素
 * 平滑滚动（抗锯齿混合帧）的剖面兜底匹配。
 * 渲染文本用例需要 QApplication，使用 QTEST_MAIN。
 * @author chiangyang
 */
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <cmath>
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
    // 行 3 写入逐列唯一标记：基础公式在 mod 256 下存在列别名（部分列完全
    // 相同），会污染水平方向的锚点投票；真实截屏每列几乎必然不同，这里
    // 显式保证列唯一性，使水平用例贴近真实内容
    if (height > 3) {
        for (int x = 0; x < width; ++x) {
            image.setPixel(x, 3, qRgb(x % 256, (x * 7) % 256, (x * 13) % 256));
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
QImage makeAltDocument(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int v = (y * 53 + x * 17 + (y * y) / 11 + (x * x) / 7 + 91) % 256;
            image.setPixel(x, y, qRgb((v * 5) % 256, v, (v * 2) % 256));
        }
    }
    return image;
}

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

/**
 * @brief 生成亚像素混合帧：模拟 macOS 平滑滚动在抓帧时刻的渲染
 *
 * 真实滚动的位移常为分数像素，系统渲染时在列方向做相邻内容的线性
 * 插值（抗锯齿混合），行指纹逐字节精确比对必然失配。
 * @param doc 源文档
 * @param offset 亚像素滚动偏移（列方向）
 * @author chiangyang
 */
QImage makeBlendedFrame(const QImage &doc, double offset, int frameW, int frameH) {
    QImage frame(frameW, frameH, QImage::Format_RGB32);
    const int x0 = int(std::floor(offset));
    const double f = offset - x0;
    for (int y = 0; y < frameH; ++y) {
        for (int x = 0; x < frameW; ++x) {
            const QRgb ca = doc.pixel(x0 + x, y);
            const QRgb cb = doc.pixel(x0 + 1 + x, y);
            const int r = int(qRed(ca) * (1 - f) + qRed(cb) * f);
            const int g = int(qGreen(ca) * (1 - f) + qGreen(cb) * f);
            const int b = int(qBlue(ca) * (1 - f) + qBlue(cb) * f);
            frame.setPixel(x, y, qRgb(r, g, b));
        }
    }
    return frame;
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

    /**
     * @brief 纯色固定页脚：不重复拼入，正文逐像素完整
     * @author chiangyang
     */
    void fixedFooterPixels();

    /**
     * @brief 页脚右侧有移动滚动条时，页脚仍应被完整剔除
     * @author chiangyang
     */
    void fixedFooterWithMovingEdge();

    /**
     * @brief 页面跳转后共同内容很少：返回 NoMatch，重设基准后可继续
     * @author chiangyang
     */
    void pageChangeRejected();

    /**
     * @brief 滚动超过重叠范围：返回 NoMatch，重设基准后可继续
     * @author chiangyang
     */
    void fastScrollBecomesNoMatch();

    /**
     * @brief 水平向左滚动（反向）：新内容进入左侧，应拼接为前置条带
     * @author chiangyang
     */
    void horizontalScrollLeftPrepend();

    /**
     * @brief 水平往返滚动：回滚已捕获范围不产生重复条带，越过边界才增长
     * @author chiangyang
     */
    void horizontalBidirectional();

    /**
     * @brief 垂直向上滚动（反向）：新内容进入顶部，应拼接为前置条带
     * @author chiangyang
     */
    void verticalScrollUpPrepend();

    /**
     * @brief 固定左侧栏 + 向左滚动：前置条带应跳过侧栏列，侧栏只出现一次
     * @author chiangyang
     */
    void horizontalScrollLeftWithSidebar();

    /**
     * @brief 灵敏度档位：严格档拒绝小重叠快速滚动，宽松档接受
     *
     * 每帧滚动量使重叠仅 12 行（低于标准/严格档的重叠要求，高于宽松档），
     * 验证灵敏度阈值确实映射到重叠行数要求。
     * @author chiangyang
     */
    void sensitivityLevels();

    /**
     * @brief 亚像素平滑滚动（抗锯齿混合帧）：剖面兜底匹配应正常拼接
     *
     * 触控板/平滑滚动在抓帧时刻多为分数像素位置，渲染时相邻内容线性
     * 混合，行指纹精确比对必然失配；此时应退化为剖面 SAD 匹配并持续
     * 拼接，累计长度接近真实滚动距离。
     * @author chiangyang
     */
    void horizontalSmoothScrollBlend();
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
        // 表头/底栏始终静止，若被当作定位依据会算成 0 位移而卡住；
        // 底栏已确认并被排除，长度从首帧正文高度开始增长
        QCOMPARE(stitcher.stitchedLength(), frameH - footerH + i * step);
    }

    // 像素级校验：固定表头只出现一次，静态底栏不重复拼入，正文无丢失
    const int contentRows = contentH + 4 * step;
    QImage expected(frameW, headerH + contentRows, QImage::Format_RGB32);
    QPainter expectedPainter(&expected);
    expectedPainter.drawImage(0, 0, header);
    expectedPainter.drawImage(0, headerH, doc.copy(0, 0, frameW, contentRows));
    QVERIFY(imagesEqual(stitcher.result(), expected));
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

void ScrollStitcherTest::fixedFooterPixels() {
    const int frameW = 120;
    const int frameH = 160;
    const int headerH = 20;
    const int footerH = 15;
    const int contentH = frameH - headerH - footerH;
    const int step = 40;
    const int scrolls = 4;

    const QImage header = makeDocument(frameW, headerH);
    const QImage doc = makeDocument(frameW, 500);
    QImage footer(frameW, footerH, QImage::Format_RGB32);
    footer.fill(qRgb(30, 60, 90));  // 与页面背景差异明显的纯色页脚

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
    for (int i = 1; i <= scrolls; ++i) {
        QCOMPARE(stitcher.appendFrame(makeFrame(i * step)),
                 ScrollStitcher::AppendResult::Appended);
    }

    // 期望：表头 + 连续正文，不含任何页脚行
    const int contentRows = contentH + scrolls * step;
    QImage expected(frameW, headerH + contentRows, QImage::Format_RGB32);
    QPainter expectedPainter(&expected);
    expectedPainter.drawImage(0, 0, header);
    expectedPainter.drawImage(0, headerH, doc.copy(0, 0, frameW, contentRows));

    QCOMPARE(stitcher.stitchedLength(), headerH + contentRows);
    QVERIFY(imagesEqual(stitcher.result(), expected));
}

void ScrollStitcherTest::fixedFooterWithMovingEdge() {
    const int frameW = 120;
    const int frameH = 160;
    const int headerH = 20;
    const int footerH = 15;
    const int contentH = frameH - headerH - footerH;
    const int step = 40;
    const int scrolls = 4;

    const QImage header = makeDocument(frameW, headerH);
    const QImage doc = makeDocument(frameW, 500);

    auto makeFrame = [&](int offset, int barY) {
        QImage frame(frameW, frameH, QImage::Format_RGB32);
        QPainter painter(&frame);
        painter.drawImage(0, 0, header);
        painter.drawImage(0, headerH, doc.copy(0, offset, frameW, contentH));
        painter.fillRect(0, headerH + contentH, frameW, footerH, QColor(30, 60, 90));
        // 模拟页脚右侧滚动的滚动条滑块：只改变边缘 2 列，行指纹会跳过
        painter.fillRect(frameW - 2, headerH + contentH + barY, 2, 5, Qt::white);
        return frame;
    };

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(makeFrame(0, 0))
            == ScrollStitcher::AppendResult::Appended);
    for (int i = 1; i <= scrolls; ++i) {
        QCOMPARE(stitcher.appendFrame(makeFrame(i * step, (i * 3) % 11)),
                 ScrollStitcher::AppendResult::Appended);
    }

    const int contentRows = contentH + scrolls * step;
    QImage expected(frameW, headerH + contentRows, QImage::Format_RGB32);
    QPainter expectedPainter(&expected);
    expectedPainter.drawImage(0, 0, header);
    expectedPainter.drawImage(0, headerH, doc.copy(0, 0, frameW, contentRows));

    QCOMPARE(stitcher.stitchedLength(), headerH + contentRows);
    QVERIFY(imagesEqual(stitcher.result(), expected));
}

void ScrollStitcherTest::pageChangeRejected() {
    const int frameW = 120;
    const int frameH = 300;
    const int headerH = 20;
    const int footerH = 15;
    const int contentH = frameH - headerH - footerH;

    const QImage header = makeDocument(frameW, headerH);
    const QImage footer = makeDocument(frameW, footerH);
    const QImage docA = makeDocument(frameW, 600);
    const QImage docB = makeAltDocument(frameW, 600);

    auto makeFrame = [&](const QImage &doc, int offset) {
        QImage frame(frameW, frameH, QImage::Format_RGB32);
        QPainter painter(&frame);
        painter.drawImage(0, 0, header);
        painter.drawImage(0, headerH, doc.copy(0, offset, frameW, contentH));
        painter.drawImage(0, headerH + contentH, footer);
        return frame;
    };

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(makeFrame(docA, 0))
            == ScrollStitcher::AppendResult::Appended);
    const int before = stitcher.stitchedLength();

    // 页面跳转后只剩表头/底栏共同行，锚点不足，应按无法定位处理
    QCOMPARE(stitcher.appendFrame(makeFrame(docB, 0)),
             ScrollStitcher::AppendResult::NoMatch);
    QCOMPARE(stitcher.stitchedLength(), before);

    // 重设基准后，新页面的后续滚动应继续追加；首次确认页脚时
    // 会把第一帧底部的页脚一并裁掉，因此比 before 少 footerH 行
    QVERIFY(stitcher.restartFromFrame(makeFrame(docB, 0)));
    QCOMPARE(stitcher.appendFrame(makeFrame(docB, 40)),
             ScrollStitcher::AppendResult::Appended);
    QCOMPARE(stitcher.stitchedLength(), before - footerH + 40);
}

void ScrollStitcherTest::fastScrollBecomesNoMatch() {
    const int frameW = 120;
    const int frameH = 300;
    const int fastStep = frameH - 10;  // 重叠 10 行，低于 kMinOverlapRows
    const QImage doc = makeDocument(frameW, 800);

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);
    const int before = stitcher.stitchedLength();

    QCOMPARE(stitcher.appendFrame(slice(doc, 0, fastStep, frameW, frameH)),
             ScrollStitcher::AppendResult::NoMatch);
    QCOMPARE(stitcher.stitchedLength(), before);

    QVERIFY(stitcher.restartFromFrame(slice(doc, 0, fastStep, frameW, frameH)));
    QCOMPARE(stitcher.appendFrame(slice(doc, 0, fastStep + 40, frameW, frameH)),
             ScrollStitcher::AppendResult::Appended);
    QCOMPARE(stitcher.stitchedLength(), before + 40);
}

void ScrollStitcherTest::horizontalScrollLeftPrepend() {
    const int frameW = 150;
    const int frameH = 120;
    const int step = 50;
    const int start = 400;
    const QImage doc = makeDocument(600, frameH);

    ScrollStitcher stitcher(ScrollStitcher::Orientation::Horizontal);
    QVERIFY(stitcher.appendFrame(slice(doc, start, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    // 向左滚动：视口起点从 400 退到 200，新内容依次前置
    for (int i = 1; i <= 4; ++i) {
        QCOMPARE(stitcher.appendFrame(slice(doc, start - i * step, 0, frameW, frameH)),
                 ScrollStitcher::AppendResult::Appended);
    }

    QCOMPARE(stitcher.stitchedLength(), frameW + 4 * step);
    QCOMPARE(stitcher.result().size(), QSize(frameW + 4 * step, frameH));
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, start - 4 * step, 0, frameW + 4 * step, frameH)));
}

void ScrollStitcherTest::horizontalBidirectional() {
    const int frameW = 150;
    const int frameH = 120;
    const int step = 50;
    const QImage doc = makeDocument(700, frameH);

    // 先向右滚两帧（扩到 [200,450)），再原路回滚（不产生重复），最后
    // 越过起点向左扩展两帧（前置）
    const int offsets[] = { 200, 250, 300, 250, 200, 150, 100 };
    ScrollStitcher stitcher(ScrollStitcher::Orientation::Horizontal);
    QVERIFY(stitcher.appendFrame(slice(doc, offsets[0], 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);
    // 右滚两帧：追加
    for (int i = 1; i <= 2; ++i) {
        QCOMPARE(stitcher.appendFrame(slice(doc, offsets[i], 0, frameW, frameH)),
                 ScrollStitcher::AppendResult::Appended);
    }
    // 原路回滚两帧：视口仍在已捕获范围内，应判为未新增
    for (int i = 3; i <= 4; ++i) {
        QCOMPARE(stitcher.appendFrame(slice(doc, offsets[i], 0, frameW, frameH)),
                 ScrollStitcher::AppendResult::Duplicate);
    }
    // 越过起点向左两帧：前置新内容
    for (int i = 5; i <= 6; ++i) {
        QCOMPARE(stitcher.appendFrame(slice(doc, offsets[i], 0, frameW, frameH)),
                 ScrollStitcher::AppendResult::Appended);
    }

    // 覆盖范围：最左前置帧 offsets[6]=100 到最右追加帧的右缘 300+150
    const int left = offsets[6];
    const int right = offsets[2] + frameW;
    QCOMPARE(stitcher.stitchedLength(), right - left);
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, left, 0, right - left, frameH)));
}

void ScrollStitcherTest::verticalScrollUpPrepend() {
    const int frameW = 120;
    const int frameH = 150;
    const int step = 50;
    const int start = 400;
    const QImage doc = makeDocument(frameW, 600);

    ScrollStitcher stitcher;
    QVERIFY(stitcher.appendFrame(slice(doc, 0, start, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    // 向上滚动：新内容从顶部进入，依次前置
    for (int i = 1; i <= 4; ++i) {
        QCOMPARE(stitcher.appendFrame(slice(doc, 0, start - i * step, frameW, frameH)),
                 ScrollStitcher::AppendResult::Appended);
    }

    QCOMPARE(stitcher.stitchedLength(), frameH + 4 * step);
    QVERIFY(imagesEqual(stitcher.result(), slice(doc, 0, start - 4 * step, frameW, frameH + 4 * step)));
}

void ScrollStitcherTest::horizontalScrollLeftWithSidebar() {
    const int frameW = 200;
    const int frameH = 120;
    const int sidebarW = 40;
    const int step = 50;
    const int start = 400;
    const QImage doc = makeDocument(700, frameH);
    const QImage sidebar = makeDocument(sidebarW, frameH);

    // 帧左侧 40px 为固定侧栏，其余为随滚动移动的内容
    auto makeFrame = [&](int offset) {
        QImage frame(frameW, frameH, QImage::Format_RGB32);
        QPainter painter(&frame);
        painter.drawImage(0, 0, sidebar);
        painter.drawImage(sidebarW, 0, doc.copy(offset + sidebarW, 0, frameW - sidebarW, frameH));
        return frame;
    };

    ScrollStitcher stitcher(ScrollStitcher::Orientation::Horizontal);
    QVERIFY(stitcher.appendFrame(makeFrame(start))
            == ScrollStitcher::AppendResult::Appended);
    for (int i = 1; i <= 2; ++i) {
        QCOMPARE(stitcher.appendFrame(makeFrame(start - i * step)),
                 ScrollStitcher::AppendResult::Appended);
    }

    // 前置条带只含内容列（跳过侧栏），侧栏只在首帧位置出现一次
    const int stitched = frameW + 2 * step;
    QCOMPARE(stitcher.stitchedLength(), stitched);

    QImage expected(stitched, frameH, QImage::Format_RGB32);
    QPainter painter(&expected);
    // 两次前置的内容：doc[340,390) 与 doc[390,440)
    painter.drawImage(0, 0, doc.copy(start - 2 * step + sidebarW, 0, step, frameH));
    painter.drawImage(step, 0, doc.copy(start - step + sidebarW, 0, step, frameH));
    painter.drawImage(2 * step, 0, sidebar);
    painter.drawImage(2 * step + sidebarW, 0, doc.copy(start + sidebarW, 0, frameW - sidebarW, frameH));
    QVERIFY(imagesEqual(stitcher.result(), expected));
}

void ScrollStitcherTest::sensitivityLevels() {
    const int frameW = 120;
    const int frameH = 150;
    const int step = frameH - 12;  // 重叠 12 行
    const QImage doc = makeDocument(frameW, 800);

    // 严格档：重叠要求 24 行 > 12，判 NoMatch
    ScrollStitcher strict(ScrollStitcher::Orientation::Vertical);
    strict.setSensitivity(2);
    QVERIFY(strict.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);
    QCOMPARE(strict.appendFrame(slice(doc, 0, step, frameW, frameH)),
             ScrollStitcher::AppendResult::NoMatch);

    // 标准档：重叠要求 16 行 > 12，判 NoMatch
    ScrollStitcher normal(ScrollStitcher::Orientation::Vertical);
    normal.setSensitivity(1);
    QVERIFY(normal.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);
    QCOMPARE(normal.appendFrame(slice(doc, 0, step, frameW, frameH)),
             ScrollStitcher::AppendResult::NoMatch);

    // 宽松档：重叠要求 8 行 < 12，正常拼接
    ScrollStitcher loose(ScrollStitcher::Orientation::Vertical);
    loose.setSensitivity(0);
    QVERIFY(loose.appendFrame(slice(doc, 0, 0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);
    QCOMPARE(loose.appendFrame(slice(doc, 0, step, frameW, frameH)),
             ScrollStitcher::AppendResult::Appended);
    QCOMPARE(loose.stitchedLength(), frameH + step);
}

void ScrollStitcherTest::horizontalSmoothScrollBlend() {
    const int frameW = 700;
    const int frameH = 460;
    const double step = 12.4;  // 触控板式平滑滚动：每帧分数像素位移

    // 真实渲染文本页：贴近实际截图内容（注意勿用像素公式生成器——
    // 公式化内容常带周期性列结构，会人为制造混叠假峰）
    QImage doc(frameW + 500, frameH, QImage::Format_RGB32);
    doc.fill(qRgb(250, 250, 248));
    {
        QPainter p(&doc);
        p.setPen(QColor(40, 40, 45));
        QFont f = p.font();
        f.setPixelSize(15);
        p.setFont(f);
        const QString words[] = {
            "quick brown fox jumps over lazy dog", "release engineering pipeline",
            "配置项已同步到云端", "performance metrics collected", "滚动截图水平方向验证",
            "network retry strategy revised", "缓存淘汰策略调整说明", "deployment rollback plan",
            "无量纲指标看板上线", "merge conflict resolution notes"
        };
        int y = 20;
        int i = 0;
        while (y < frameH - 20) {
            const QString line = QString("%1. %2 — %3")
                .arg(i).arg(words[i % 10]).arg(words[(i * 7 + 3) % 10]);
            p.drawText(QRect(12, y, frameW + 500 - 24, 22), Qt::AlignLeft | Qt::AlignVCenter, line);
            y += 26;
            ++i;
        }
        p.setPen(QColor(210, 210, 210));
        for (int sy = 12; sy < frameH; sy += 52) p.drawLine(0, sy, frameW + 500, sy);
    }

    ScrollStitcher stitcher(ScrollStitcher::Orientation::Horizontal);
    QVERIFY(stitcher.appendFrame(makeBlendedFrame(doc, 60.0, frameW, frameH))
            == ScrollStitcher::AppendResult::Appended);

    const int frames = 9;
    for (int i = 1; i <= frames; ++i) {
        QCOMPARE(stitcher.appendFrame(makeBlendedFrame(doc, 60.0 + i * step, frameW, frameH)),
                 ScrollStitcher::AppendResult::Appended);
    }

    // 真实滚动距离 9 * 12.4 ≈ 111.6；SAD 峰值取最近整数，允许每帧 ±1 的
    // 取整误差，累计长度应落在 [真实距离 - 帧数, 真实距离 + 帧数] 区间
    const double scrolled = frames * step;
    QVERIFY(stitcher.stitchedLength() >= frameW + int(scrolled) - frames);
    QVERIFY(stitcher.stitchedLength() <= frameW + int(scrolled) + frames + 1);
}

QTEST_MAIN(ScrollStitcherTest)
#include "ScrollStitcherTest.moc"
