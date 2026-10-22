#ifndef SCROLLSTITCHER_H
#define SCROLLSTITCHER_H

#include <QImage>
#include <QSize>
#include <QVector>

/**
 * @brief 滚动截图拼接器（纯算法，无 GUI 依赖）
 *
 * 输入为同尺寸的连续帧（选区画面），输出为沿主方向不断增长的长图。
 * 垂直方向沿 Y 轴向下追加，水平方向沿 X 轴向右追加。
 *
 * 拼接原理：
 * 1. 灰度化后为每行按固定步长采样列、生成 64 位指纹；
 * 2. 只在前后两帧中都唯一出现的行参与位移投票，得票最多的位移为候选；
 * 3. 用上一帧其余唯一行回代校验一致率，不足则丢弃该帧；
 * 4. 匹配成功后只追加当前帧的新内容条带；帧底部连续不动的区域视为
 *    固定页脚，不参与对齐也不再拼入长图，避免正文丢失与页脚重复。
 *
 * 空白行与重复排版行因不唯一而不投票，固定表头/底栏因位移为 0
 * 被排除，纯色画面则因缺乏唯一行被判为无法定位。
 *
 * 水平方向内部把帧转置 90°，复用同一套垂直算法，最终输出时转回。
 * @author chiangyang
 */
class ScrollStitcher {
public:
    /**
     * @brief 滚动方向
     */
    enum class Orientation {
        Vertical,   ///< 垂直滚动（上下），长图向下增长
        Horizontal, ///< 水平滚动（左右），长图向右增长
    };

    /**
     * @brief 追加帧的结果
     */
    enum class AppendResult {
        Appended,       ///< 成功并入新内容
        Duplicate,      ///< 与上一帧相同（页面未滚动），已忽略
        NoMatch,        ///< 找不到可靠重叠（滚动过快或画面无纹理），已忽略
        LimitReached,   ///< 已达到最大长度，未追加
        SizeMismatch,   ///< 帧尺寸与首帧不一致，未追加
        InvalidFrame,   ///< 空帧
    };

    /**
     * @brief 构造函数
     * @param orientation 滚动方向
     * @author chiangyang
     */
    explicit ScrollStitcher(Orientation orientation = Orientation::Vertical);

    /**
     * @brief 重置拼接状态
     * @param orientation 新的滚动方向
     * @author chiangyang
     */
    void reset(Orientation orientation);

    /**
     * @brief 当前滚动方向
     * @author chiangyang
     */
    Orientation orientation() const { return m_orientation; }

    /**
     * @brief 追加一帧
     *
     * 首帧直接作为结果基准；后续帧做重叠匹配后追加新内容。
     * @param frame 选区画面（任意格式，内部统一转为 RGB32）
     * @return 追加结果
     * @author chiangyang
     */
    AppendResult appendFrame(const QImage &frame);

    /**
     * @brief 获取拼接结果（水平方向已转回原始朝向）
     * @return 长图；尚未追加任何帧时返回空图
     * @author chiangyang
     */
    QImage result() const;

    /**
     * @brief 以当前帧重设匹配基准（保留已拼接结果）
     *
     * 连续无法定位时由会话调用：放弃旧基准，后续帧从当前画面继续拼接。
     * 无法定位期间的内容不会进入结果，仅用于让会话恢复可用。
     * @param frame 当前帧（原始朝向）
     * @return 帧尺寸与首帧一致并成功更新基准时返回 true
     * @author chiangyang
     */
    bool restartFromFrame(const QImage &frame);

    /**
     * @brief 是否尚无任何帧
     * @author chiangyang
     */
    bool isEmpty() const { return m_strips.isEmpty(); }

    /**
     * @brief 选区帧尺寸（原始朝向）
     * @author chiangyang
     */
    QSize frameSize() const { return m_frameSize; }

    /**
     * @brief 当前拼接长度（垂直=高度，水平=宽度）
     * @author chiangyang
     */
    int stitchedLength() const;

    /**
     * @brief 设置最大拼接长度（主方向像素）
     * @param length 最大长度，默认 20000
     * @author chiangyang
     */
    void setMaxLength(int length) { m_maxLength = qMax(1, length); }

    /**
     * @brief 获取最大拼接长度
     * @author chiangyang
     */
    int maxLength() const { return m_maxLength; }

    ///< 默认最大拼接长度（像素）
    static constexpr int kDefaultMaxLength = 20000;

private:
    /**
     * @brief 通过行指纹投票求两帧之间的滚动位移
     *
     * 为每行像素生成 64 位指纹，只让在两帧中均唯一出现的行参与投票：
     * 空白行、重复排版行会自然失去投票权，固定表头/底栏因位移为 0
     * 被排除，避免其干扰定位。得票最多的位移再回代校验一致性。
     *
     * @param prev 上一帧灰度图（转置坐标系）
     * @param next 当前帧灰度图（转置坐标系）
     * @param staticBottomRows 输出：帧底部连续保持不动的行数（固定页脚候选）
     * @return 内容上移的像素数；0 表示未滚动；-1 表示无法可靠定位
     * @author chiangyang
     */
    int findVerticalShift(const QImage &prev, const QImage &next,
                          int *staticBottomRows) const;

    /**
     * @brief 将新内容追加为一段条带（转置坐标系）
     *
     * 只保存新增部分而非每帧整图拷贝：长图增长到上万像素后，
     * 逐帧复制整图会带来显著的 CPU 与内存抖动。
     * @param frame 当前帧（转置坐标系）
     * @param startRow 新内容起始行（含）
     * @param endRow 新内容结束行（不含，固定页脚在此之下）
     * @return 是否追加成功
     * @author chiangyang
     */
    bool appendRows(const QImage &frame, int startRow, int endRow);

    /**
     * @brief 从首帧条带底部移除已确认的固定页脚
     *
     * 首帧先按整帧保存，第二帧检测到固定页脚后才能确认其高度；
     * 此时把首帧底部的页脚行裁掉，避免页脚出现在长图开头。
     * @param footerRows 累计确认的固定页脚行数
     * @author chiangyang
     */
    void trimFirstStripFooter(int footerRows);

    Orientation m_orientation;      ///< 滚动方向
    QVector<QImage> m_strips;       ///< 分段条带（按顺序拼接即为结果，水平方向为转置坐标系）
    QImage m_lastGray;              ///< 上一帧灰度图（模板来源，转置坐标系）
    QSize m_workFrameSize;          ///< 转置坐标系下的帧尺寸
    QSize m_frameSize;              ///< 原始朝向的帧尺寸
    int m_stitchedLength = 0;       ///< 已拼接的主方向长度
    int m_footerRows = 0;           ///< 已确认的固定页脚行数（0 表示无页脚）
    int m_firstStripTrimmedRows = 0;///< 首帧条带已裁掉的页脚行数
    int m_maxLength = kDefaultMaxLength;  ///< 最大拼接长度
};

#endif // SCROLLSTITCHER_H
