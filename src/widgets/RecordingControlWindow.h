#ifndef RECORDINGCONTROLWINDOW_H
#define RECORDINGCONTROLWINDOW_H

#include <QPushButton>
#include <QVBoxLayout>
#include <QCheckBox>

/**
 * @brief 录屏控制窗口类
 * 
 * 包含录屏控制按钮（开始、暂停、继续、结束）和音频选项
 * 输出分辨率自动匹配选区大小
 * @author chiangyang
 */
class RecordingControlWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit RecordingControlWindow(QWidget *parent = nullptr);

    /**
     * @brief 重新翻译UI文本
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 更新按钮状态
     * @param isRecording 是否正在录制
     * @param isPaused 是否暂停
     * @author chiangyang
     */
    void updateButtonStates(bool isRecording, bool isPaused);

    /**
     * @brief 更新按钮显示样式
     * @author chiangyang
     */
    void updateButtonStyles();

    /**
     * @brief 更新背景样式
     * @author chiangyang
     */
    void updateBackgroundStyle();

    /**
     * @brief 设置录制状态
     * @param recording 是否正在录制
     * @author chiangyang
     */
    void setRecording(bool recording);

    /**
     * @brief 设置暂停状态
     * @param paused 是否暂停
     * @author chiangyang
     */
    void setPaused(bool paused);

protected:
    /**
     * @brief 绘制事件：主动绘制圆角矩形背景
     *
     * 当设置 WA_TranslucentBackground 后，QSS 的 background-color 不会自动绘制，
     * 因此用 QPainter 主动绘制圆角矩形背景，保证圆角外区域透明且圆角内有底色。
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

signals:
    /**
     * @brief 请求开始录制信号
     * @author chiangyang
     */
    void startRequested();
    /**
     * @brief 请求暂停录制信号
     * @author chiangyang
     */
    void pauseRequested();
    /**
     * @brief 请求恢复录制信号
     * @author chiangyang
     */
    void resumeRequested();
    /**
     * @brief 请求停止录制信号
     * @author chiangyang
     */
    void stopRequested();
    /**
     * @brief 系统音频开关信号
     * @param enabled 是否启用
     * @author chiangyang
     */
    void systemAudioToggled(bool enabled);
    /**
     * @brief 麦克风开关信号
     * @param enabled 是否启用
     * @author chiangyang
     */
    void microphoneToggled(bool enabled);

private:
    /**
     * @brief 设置UI布局
     * @author chiangyang
     */
    void setupUi();

    // 控制按钮
    QPushButton *btnStart = nullptr;
    QPushButton *btnPause = nullptr;
    QPushButton *btnResume = nullptr;
    QPushButton *btnStop = nullptr;

    // 音频录制选项
    QCheckBox *chkSystemAudio = nullptr;
    QCheckBox *chkMicrophone = nullptr;

    // 状态
    bool m_isRecording = false;
    bool m_isPaused = false;

    // 布局
    QHBoxLayout *m_mainLayout = nullptr;
};

#endif // RECORDINGCONTROLWINDOW_H