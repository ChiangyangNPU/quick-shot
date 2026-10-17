#include <QtTest>
#include "ocr/OcrRecPostprocess.h"

/**
 * @brief OcrRecPostprocess CTC 解码的单元测试
 *
 * 喂固定 logits 断言解码文本与置信度，防止后处理改坏：
 * - argmax 选字符、index 0 为 blank、连续重复折叠、blank 打断重复
 * - 字典映射偏移（模型 index i → 字典 i-1）
 * - 置信度为有效步 argmax 概率的平均值
 * @author chiangyang
 */
class OcrRecPostprocessTest : public QObject {
    Q_OBJECT

    // 组装 [1, seqLen, numChars] 的 logits，每步指定 argmax 索引
    static std::vector<float> makeLogits(const QList<int> &argmaxPerStep, int numChars) {
        std::vector<float> logits(argmaxPerStep.size() * numChars, 0.0f);
        for (int t = 0; t < argmaxPerStep.size(); ++t) {
            logits[t * numChars + argmaxPerStep[t]] = 5.0f; // 大间隔保证 argmax 唯一
        }
        return logits;
    }

private slots:
    void basic_decodeSkipsBlankAndDuplicates() {
        // 字典 4 字符 → numChars = 5（index 0 为 blank）
        const QStringList dict = {"你", "好", "世", "界"};
        // 序列: blank, 你, 你(折叠), 好, blank, 世
        const auto logits = makeLogits({0, 1, 1, 2, 0, 3}, 5);
        const auto r = OcrRecPostprocess::decode(logits.data(), 6, 5, dict);
        QCOMPARE(r.text, QString("你好世"));
        QVERIFY(r.score > 0.9f && r.score <= 1.0f);
    }

    void blank_breaksDuplicateCollapse() {
        const QStringList dict = {"你", "好", "世", "界"};
        // 序列: 你, blank, 你 → blank 打断折叠，两个"你"都保留
        const auto logits = makeLogits({1, 0, 1}, 5);
        const auto r = OcrRecPostprocess::decode(logits.data(), 3, 5, dict);
        QCOMPARE(r.text, QString("你你"));
    }

    void dictOffset_isModelIndexMinusOne() {
        const QStringList dict = {"A", "B", "C"};
        // 模型 index 1→dict[0]="A"，index 2→dict[1]="B"，index 3→dict[2]="C"
        const auto logits = makeLogits({1, 2, 3}, 4);
        const auto r = OcrRecPostprocess::decode(logits.data(), 3, 4, dict);
        QCOMPARE(r.text, QString("ABC"));
    }

    void score_isMeanOfValidStepProbs() {
        // 两个有效步 logits 均为 [0, 1, 0, 0]：argmax 概率 = e/(e+3)
        const QStringList dict = {"x", "y"};
        std::vector<float> logits = {
            0.0f, 1.0f, 0.0f, 0.0f,   // step0 → index 1
            0.0f, 1.0f, 0.0f, 0.0f,   // step1 → index 1（不相邻重复：仅一帧）
        };
        const auto r = OcrRecPostprocess::decode(logits.data(), 2, 4, dict);
        QCOMPARE(r.text, QString("x"));
        const float expected = static_cast<float>(std::exp(1.0) / (std::exp(1.0) + 3.0));
        QVERIFY2(qAbs(r.score - expected) < 1e-4f,
                 qPrintable(QString("score %1 vs expected %2").arg(r.score).arg(expected)));
    }

    void invalidInputs_returnEmpty() {
        const QStringList dict = {"a"};
        QVERIFY(OcrRecPostprocess::decode(nullptr, 0, 4, dict).text.isEmpty());
        QVERIFY(OcrRecPostprocess::decode(nullptr, 2, 0, dict).text.isEmpty());
        QVERIFY(OcrRecPostprocess::decode(nullptr, 2, 4, QStringList()).text.isEmpty());
        // 越界模型索引（超过字典容量）不崩溃且被跳过
        std::vector<float> logits(2 * 10, 0.0f);
        logits[9] = 5.0f;  // step0 → index 9，dict 只有 1 项（最大可用 index 2）
        const auto r = OcrRecPostprocess::decode(logits.data(), 2, 10, dict);
        QCOMPARE(r.text, QString());
        QCOMPARE(r.score, 0.0f);
    }

    void allBlank_returnsEmpty() {
        const QStringList dict = {"a", "b"};
        const auto logits = makeLogits({0, 0, 0}, 3);
        const auto r = OcrRecPostprocess::decode(logits.data(), 3, 3, dict);
        QCOMPARE(r.text, QString());
        QCOMPARE(r.score, 0.0f);
    }
};

QTEST_GUILESS_MAIN(OcrRecPostprocessTest)
#include "OcrRecPostprocessTest.moc"
