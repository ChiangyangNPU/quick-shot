#ifndef GIFENCODER_H
#define GIFENCODER_H

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

/**
 * @brief 轻量级 GIF89a 动画编码器
 *
 * 纯 Qt 实现的 GIF 动画编码器，无第三方依赖，用于录屏时把逐帧画面写成
 * 可循环播放的 GIF 文件。
 *
 * 编码策略：
 * - 每帧使用局部调色板（QImage::Format_Indexed8 转换生成，最多 256 色）；
 * - 与上一帧完全相同则只累加显示延迟，静态画面不会重复写数据，显著减小体积；
 * - 使用 Netscape 扩展实现无限循环；
 * - 帧间 Disposal 为「保留」，每帧按整帧覆盖绘制。
 *
 * 使用方式：open() -> 多次 addFrame() -> close()（异常中断用 abort() 删除文件）。
 * 编码器非线程安全，应在同一个线程内使用（录屏时为录屏工作线程）。
 * @author chiangyang
 */
class GifEncoder {
public:
    /**
     * @brief 构造函数
     * @author chiangyang
     */
    GifEncoder();

    /**
     * @brief 析构函数
     *
     * 若仍处于打开状态会自动调用 abort()，避免留下残缺文件。
     * @author chiangyang
     */
    ~GifEncoder();

    /**
     * @brief 创建并打开 GIF 文件
     *
     * 写入 GIF89a 文件头、逻辑屏幕描述符和无限循环扩展。
     * @param filePath 输出文件路径（.gif）
     * @param size 画面尺寸（像素）
     * @return 成功返回 true，否则 false
     * @author chiangyang
     */
    bool open(const QString &filePath, const QSize &size);

    /**
     * @brief 追加一帧画面
     *
     * 画面会被转换为 8 位索引色并写入；若与待写入帧完全相同，则只累加延迟。
     * @param frame 帧图像（任意 QImage 格式，内部会按 open() 的尺寸缩放）
     * @param delayCentiseconds 该帧显示时长（单位：1/100 秒）
     * @return 成功返回 true，否则 false
     * @author chiangyang
     */
    bool addFrame(const QImage &frame, int delayCentiseconds);

    /**
     * @brief 完成编码并关闭文件
     *
     * 写出最后一帧和文件结束符后关闭文件。
     * @return 成功返回 true，否则 false
     * @author chiangyang
     */
    bool close();

    /**
     * @brief 中止编码并删除已生成的文件
     * @author chiangyang
     */
    void abort();

    /**
     * @brief 是否已成功打开文件
     * @return true 表示可以追加帧
     * @author chiangyang
     */
    bool isOpen() const { return m_opened; }

    /**
     * @brief 获取已写入的帧数（相同帧合并后计为 1 帧）
     * @return 帧数量
     * @author chiangyang
     */
    int frameCount() const { return m_frameCount; }

private:
    /**
     * @brief 将输入图像转换为带调色板的 8 位索引图
     * @param frame 输入图像
     * @return 索引色图像，失败时返回空图像
     * @author chiangyang
     */
    QImage toIndexed(const QImage &frame) const;

    /**
     * @brief 写入 GIF 文件头与循环扩展
     * @return 成功返回 true
     * @author chiangyang
     */
    bool writeHeader();

    /**
     * @brief 把待写入帧刷新到文件
     * @return 成功返回 true
     * @author chiangyang
     */
    bool flushPending();

    /**
     * @brief 写入单帧（图形控制扩展 + 图像描述符 + 局部色表 + LZW 数据）
     * @param indexed 索引色帧
     * @param delayCentiseconds 帧延迟（1/100 秒）
     * @return 成功返回 true
     * @author chiangyang
     */
    bool writeImage(const QImage &indexed, int delayCentiseconds);

    /**
     * @brief LZW 压缩并封装为 GIF 子块
     * @param indices 索引像素数据（按行连续存放）
     * @param minCodeSize LZW 最小码长（2~8）
     * @return GIF 子块字节流（含结束符 0x00），失败返回空
     * @author chiangyang
     */
    QByteArray encodeLzw(const QByteArray &indices, int minCodeSize) const;

    /**
     * @brief 写字节数据到文件
     * @param data 待写数据
     * @return 成功返回 true
     * @author chiangyang
     */
    bool writeBytes(const QByteArray &data);

    QFile m_file;                 ///< 输出文件
    QSize m_size;                 ///< 画面尺寸
    QImage m_pending;             ///< 待写入帧（索引色）
    int m_pendingDelay = 0;       ///< 待写入帧累计延迟（1/100 秒）
    bool m_hasPending = false;    ///< 是否存在待写入帧
    bool m_opened = false;        ///< 文件是否已打开
    int m_frameCount = 0;         ///< 已写出的帧数
};

#endif // GIFENCODER_H
