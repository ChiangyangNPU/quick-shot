#include "OcrRecPostprocess.h"
#include "../log/Logger.h"
#include <cmath>
#include <algorithm>

/**
 * @brief CTC 解码
 *
 * 对识别模型输出进行 argmax + CTC 解码（去重 + 去 blank）。
 *
 * CTC 解码规则：
 * 1. 对每个时间步取 argmax 得到字符索引
 * 2. 去除连续重复字符（如 "aaa" -> "a"）
 * 3. 去除 blank（index 0 通常是 blank）
 * 4. 使用字典将索引映射为字符
 *
 * @param logits 模型输出 logits (shape: [1, seqLen, numChars])
 * @param seqLen 序列长度
 * @param numChars 字符数量（包含 blank）
 * @param charDict 字符字典
 * @return 解码结果（文本和置信度）
 * @author chiangyang
 */
OcrRecPostprocess::RecResult OcrRecPostprocess::decode(const float *logits, int seqLen, int numChars,
                                                        const QStringList &charDict) {
    RecResult result;
    result.score = 0.0f;

    if (seqLen <= 0 || numChars <= 0 || charDict.isEmpty()) {
        LOG_INFO("OcrRecPostprocess: invalid input, returning empty result");
        return result;
    }

    // softmax 函数
    auto softmax = [](const float *logits, int size) -> std::vector<float> {
        std::vector<float> probs(size);
        float maxVal = *std::max_element(logits, logits + size);
        float sum = 0.0f;
        for (int i = 0; i < size; ++i) {
            probs[i] = std::exp(logits[i] - maxVal);
            sum += probs[i];
        }
        for (int i = 0; i < size; ++i) {
            probs[i] /= sum;
        }
        return probs;
    };

    // CTC 解码
    QString text;
    float totalScore = 0.0f;
    int validCount = 0;
    int prevIdx = -1; // 上一个字符索引

    for (int t = 0; t < seqLen; ++t) {
        const float *logitPtr = logits + t * numChars;

        // argmax
        int maxIdx = 0;
        float maxVal = logitPtr[0];
        for (int c = 1; c < numChars; ++c) {
            if (logitPtr[c] > maxVal) {
                maxVal = logitPtr[c];
                maxIdx = c;
            }
        }

        // softmax 计算概率
        auto probs = softmax(logitPtr, numChars);
        float prob = probs[maxIdx];

        // CTC blank 通常是 index 0
        // PP-OCR 字典中，index 0 是 blank
        if (maxIdx == 0) {
            // blank，跳过
            prevIdx = -1;
            continue;
        }

        // 去重：如果当前字符与前一个相同，跳过
        if (maxIdx == prevIdx) {
            prevIdx = maxIdx;
            continue;
        }

        // 索引映射到字符
        // PP-OCR 字典格式：字典中没有 blank，index 0 对应字典第 0 个字符
        // 但模型输出的 index 0 是 blank，所以字典索引 = maxIdx - 1
        int dictIdx = maxIdx - 1;
        if (dictIdx >= 0 && dictIdx < charDict.size()) {
            text += charDict[dictIdx];
            totalScore += prob;
            validCount++;
        }

        prevIdx = maxIdx;
    }

    result.text = text;
    result.score = (validCount > 0) ? (totalScore / validCount) : 0.0f;

    LOG_INFO(QString("OcrRecPostprocess: decode result, text=%1, score=%2, seqLen=%3")
        .arg(text).arg(result.score, 0, 'f', 3).arg(seqLen));

    return result;
}
