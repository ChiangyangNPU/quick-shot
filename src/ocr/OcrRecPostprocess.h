#ifndef OCR_REC_POSTPROCESS_H
#define OCR_REC_POSTPROCESS_H

#include <QString>
#include <QStringList>

/**
 * @brief OCR 识别后处理类
 *
 * 实现 CTC 解码，将模型输出的 logits 转换为文本。
 * @author chiangyang
 */
class OcrRecPostprocess {
public:
    /**
     * @brief 识别结果结构体
     * @author chiangyang
     */
    struct RecResult {
        QString text;   ///< 识别的文本
        float score;    ///< 置信度
    };

    /**
     * @brief CTC 解码
     *
     * 对识别模型输出进行 argmax + CTC 解码（去重 + 去 blank）。
     * @param logits 模型输出 logits (shape: [1, seqLen, numChars])
     * @param seqLen 序列长度
     * @param numChars 字符数量（包含 blank）
     * @param charDict 字符字典
     * @return 解码结果（文本和置信度）
     * @author chiangyang
     */
    static RecResult decode(const float *logits, int seqLen, int numChars,
                            const QStringList &charDict);
};

#endif // OCR_REC_POSTPROCESS_H
