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
 * 1. 取上一帧底部（水平为右侧）一定高度的模板；
 * 2. 在当前帧顶部（水平为左侧）60% 范围内滑动搜索模板的最佳匹配位置；
 * 3. 匹配位置之后的像素视为新内容，追加到结果图末尾；
 * 4. 匹配差异过大或模板无纹理时丢弃该帧，避免错位拼接。
 *
 * 模板取自上一帧尾部而非头部，可自然规避固定表头（滚动时表头不动，
 * 不会出现在上一帧尾部，因此不会被误匹配到当前帧顶部）。
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
     * @return 内容上移的像素数；0 表示未滚动；-1 表示无法可靠定位
     * @author chiangyang
     */
    int findVerticalShift(const QImage &prev, const QImage &next) const;

    /**
     * @brief 将新内容追加为一段条带（转置坐标系）
     *
     * 只保存新增部分而非每帧整图拷贝：长图增长到上万像素后，
     * 逐帧复制整图会带来显著的 CPU 与内存抖动。
     * @param frame 当前帧（转置坐标系）
     * @param startRow 新内容起始行
     * @return 是否追加成功
     * @author chiangyang
     */
    bool appendRows(const QImage &frame, int startRow);

    Orientation m_orientation;      ///< 滚动方向
    QVector<QImage> m_strips;       ///< 分段条带（按顺序拼接即为结果，水平方向为转置坐标系）
    QImage m_lastGray;              ///< 上一帧灰度图（模板来源，转置坐标系）
    QSize m_workFrameSize;          ///< 转置坐标系下的帧尺寸
    QSize m_frameSize;              ///< 原始朝向的帧尺寸
    int m_stitchedLength = 0;       ///< 已拼接的主方向长度
    int m_maxLength = kDefaultMaxLength;  ///< 最大拼接长度
};

#endif // SCROLLSTITCHER_H
