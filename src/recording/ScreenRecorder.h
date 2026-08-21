#ifndef SCREENRECORDER_H
#define SCREENRECORDER_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QImage>
#include <QMutex>

#include <atomic>
#include <thread>

class ScreenRecorder;

class ScreenRecorder : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 录屏器内部实现结构体
     * @author chiangyang
     */
    struct Impl {
        std::atomic<bool> running{false};    ///< 是否正在录制
        std::atomic<bool> paused{false};     ///< 是否已暂停
        std::atomic<bool> canceled{false};   ///< 是否取消录制（取消时删除视频文件）
        std::thread worker;                  ///< 工作线程

        QRect captureRectPx;                 ///< 捕获区域（物理像素）
        QSize outputSizePx;                  ///< 输出尺寸（物理像素）
        QString outputFilePath;              ///< 输出文件路径
        int fps = 30;                        ///< 帧率

        QByteArray frameBuffer;              ///< 帧缓冲区

        QImage annotationOverlay;            ///< 标注叠加图像（输出分辨率，ARGB32格式）
        QMutex annotationMutex;              ///< 保护 annotationOverlay 的互斥锁

        bool systemAudioEnabled = false;     ///< 是否启用系统音频
        bool microphoneEnabled = false;      ///< 是否启用麦克风

        ScreenRecorder *recorder = nullptr;  ///< 录屏器实例指针
    };

    /**
     * @brief 窗口信息结构体
     * @author chiangyang
     */
    struct WindowInfo {
        int windowId;           ///< 窗口ID
        QString title;          ///< 窗口标题
        QString ownerName;      ///< 窗口所属进程名
        QRect rect;             ///< 窗口矩形
    };

    /**
     * @brief 录制模式枚举
     * @author chiangyang
     */
    enum class RecordingMode {
        Area,   ///< 区域录制
        Window  ///< 窗口录制
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit ScreenRecorder(QObject *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~ScreenRecorder() override;

    /**
     * @brief 开始录制
     * @param captureRectPx 捕获区域（物理像素）
     * @param outputSizePx 输出尺寸（物理像素）
     * @param outputFilePath 输出文件路径
     * @param fps 帧率
     * @return 是否成功开始录制
     * @author chiangyang
     */
    bool start(const QRect &captureRectPx, const QSize &outputSizePx, const QString &outputFilePath, int fps = 30);

    /**
     * @brief 暂停录制
     * @author chiangyang
     */
    void pause();

    /**
     * @brief 恢复录制
     * @author chiangyang
     */
    void resume();

    /**
     * @brief 停止录制
     * @author chiangyang
     */
    void stop();

    /**
     * @brief 取消录制
     * 
     * 取消录制并删除已生成的视频文件。与 stop() 不同，cancel() 会在录制结束后删除视频文件。
     * @author chiangyang
     */
    void cancel();

    /**
     * @brief 检查是否正在录制
     * @return 是否正在录制
     * @author chiangyang
     */
    bool isRecording() const;

    /**
     * @brief 检查是否已暂停
     * @return 是否已暂停
     * @author chiangyang
     */
    bool isPaused() const;

    /**
     * @brief 获取可用的音频设备列表
     * @return 音频设备名称列表
     * @author chiangyang
     */
    QList<QString> availableAudioDevices() const;

    /**
     * @brief 设置音频设备
     * @param index 设备索引
     * @author chiangyang
     */
    void setAudioDevice(int index);

    /**
     * @brief 检查录屏功能是否可用
     * @return 是否可用
     * @author chiangyang
     */
    bool isAvailable() const;

    /**
     * @brief 获取可用的窗口列表
     * @return 窗口信息列表
     * @author chiangyang
     */
    static QList<WindowInfo> getAvailableWindows();

    /**
     * @brief 开始窗口录制
     * @param windowId 窗口ID
     * @param outputFilePath 输出文件路径
     * @param width 宽度
     * @param height 高度
     * @param fps 帧率
     * @return 是否成功开始录制
     * @author chiangyang
     */
    bool startWindowRecording(int windowId, const QString &outputFilePath, int width, int height, int fps = 30);

    /**
     * @brief 开始区域录制
     * @param captureRect 捕获区域
     * @param outputFilePath 输出文件路径
     * @param fps 帧率
     * @return 是否成功开始录制
     * @author chiangyang
     */
    bool startAreaRecording(const QRect &captureRect, const QString &outputFilePath, int fps = 30);

    /**
     * @brief 设置音频是否启用
     * @param enabled 是否启用
     * @author chiangyang
     */
    void setAudioEnabled(bool enabled);

    /**
     * @brief 设置麦克风是否启用
     * @param enabled 是否启用
     * @author chiangyang
     */
    void setMicrophoneEnabled(bool enabled);

    /**
     * @brief 获取当前录制模式
     * @return 当前录制模式
     * @author chiangyang
     */
    RecordingMode currentMode() const { return m_currentMode; }

    /**
     * @brief 设置标注叠加图像
     *
     * 录制线程每帧将此图像合成到帧缓冲中。UI 线程在标注变化时调用。
     * 传入空 QImage 清除叠加。
     *
     * @param overlay 标注叠加图像（输出分辨率，ARGB32格式）
     * @author chiangyang
     */
    void setAnnotationOverlay(const QImage &overlay);

    /**
     * @brief 获取输出分辨率
     * @return 输出尺寸
     * @author chiangyang
     */
    QSize outputSize() const;

signals:
    /**
     * @brief 录制出错信号
     * @param message 错误信息
     * @author chiangyang
     */
    void errorOccurred(const QString &message);
    /**
     * @brief 录制停止信号
     * @param outputFilePath 输出文件路径
     * @author chiangyang
     */
    void stopped(const QString &outputFilePath);

private:
    Impl *m_impl = nullptr;
    RecordingMode m_currentMode = RecordingMode::Area;
};

#endif
