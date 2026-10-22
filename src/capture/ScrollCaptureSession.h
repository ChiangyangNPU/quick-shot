#ifndef SCROLLCAPTURESESSION_H
#define SCROLLCAPTURESESSION_H

#include "ScrollStitcher.h"
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRect>

class SnipScreen;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;

/**
 * @brief 滚动截图会话
 *
 * 从 SnipScreen 拆分出的滚动截图编排职责（参照 RecordingSession 的会话模式）：
 * - 进入时隐藏截图遮罩窗口，让用户看到并操作真实页面；
 * - 按固定间隔抓取选区画面，交由 ScrollStitcher 拼接；
 * - 在选区外侧显示提示条（方向 / 已拼接长度 / 完成 / 取消）；
 * - 完成时把长图交给宿主 SnipScreen 用 PinWindow 承载，取消则直接退出截图。
 *
 * 抓帧窗口遮挡说明：
 * 宿主 SnipScreen 进入会话后立即 hide()，因此抓到的选区内像素不含遮罩与边框；
 * 提示条定位在选区外侧，裁剪选区时不会被拍入（Windows 另加捕获排除兜底）。
 *
 * 键盘交互：
 * 宿主隐藏后由提示条窗口接收按键：Enter 完成、Esc 取消、H/V 切换方向。
 * 鼠标滚轮落在选区上时会透传给下层应用，实现用户手动滚动页面。
 * @author chiangyang
 */
class ScrollCaptureSession : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param host 宿主 SnipScreen（非拥有，生命周期须长于本对象）
     * @param parent QObject 父对象（传宿主以随宿主销毁）
     * @author chiangyang
     */
    explicit ScrollCaptureSession(SnipScreen *host, QObject *parent = nullptr);

    /**
     * @brief 析构函数：确保提示条与定时器被清理
     * @author chiangyang
     */
    ~ScrollCaptureSession() override;

    /**
     * @brief 是否处于滚动截图会话中
     * @author chiangyang
     */
    bool isActive() const { return m_active; }

    /**
     * @brief 当前滚动方向
     * @author chiangyang
     */
    ScrollStitcher::Orientation orientation() const { return m_stitcher.orientation(); }

    /**
     * @brief 以当前选区启动滚动截图
     * @param orientation 滚动方向
     * @return 是否成功启动（选区无效时返回 false）
     * @author chiangyang
     */
    bool start(ScrollStitcher::Orientation orientation);

    /**
     * @brief 完成拼接并把长图交给宿主贴图
     *
     * 无有效结果时按取消处理。
     * @author chiangyang
     */
    void finish();

    /**
     * @brief 取消滚动截图
     * @author chiangyang
     */
    void cancel();

    /**
     * @brief 会话进行中切换滚动方向（清空已拼接内容，以当前画面重新开始）
     * @param orientation 新的滚动方向
     * @author chiangyang
     */
    void setOrientation(ScrollStitcher::Orientation orientation);

    /**
     * @brief 宿主退出时强制清理（不触发完成/取消信号）
     * @author chiangyang
     */
    void shutdown();

signals:
    /**
     * @brief 拼接完成
     * @param pixmap 长图（物理像素、dpr=1）
     * @param pos 选区左上角全局坐标（贴图初始位置参考）
     * @author chiangyang
     */
    void completed(const QPixmap &pixmap, const QPoint &pos);

    /**
     * @brief 会话结束（完成或取消），宿主据此退出截图
     * @author chiangyang
     */
    void finished();

protected:
    /**
     * @brief 事件过滤器：处理提示条上的键盘快捷键
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @brief 启动抓帧定时器并抓取首帧
     * @author chiangyang
     */
    void beginCapture();

    /**
     * @brief 定时抓帧并尝试拼接
     * @author chiangyang
     */
    void captureTick();

    /**
     * @brief 抓取选区当前画面
     * @return 选区图像（物理像素、dpr=1）；失败返回空图
     * @author chiangyang
     */
    QImage grabFrame() const;

    /**
     * @brief 创建提示条窗口
     * @author chiangyang
     */
    void createHintBar();

    /**
     * @brief 销毁提示条窗口
     * @author chiangyang
     */
    void destroyHintBar();

    /**
     * @brief 把提示条定位到选区外侧
     * @author chiangyang
     */
    void positionHintBar();

    /**
     * @brief 刷新提示文案（方向 / 已拼接长度 / 重新对齐提示）
     * @author chiangyang
     */
    void updateHintText();

    /**
     * @brief 显示短暂的“已重新对齐”提示
     * @author chiangyang
     */
    void showResyncNotice();

    /**
     * @brief 停止抓帧定时器
     * @author chiangyang
     */
    void stopTimer();

    SnipScreen *m_host;                  ///< 宿主（非拥有）
    ScrollStitcher m_stitcher;           ///< 拼接器
    QTimer *m_timer = nullptr;           ///< 抓帧定时器
    QTimer *m_noticeTimer = nullptr;     ///< 重新对齐提示的显示时长
    QPointer<QWidget> m_hintBar;         ///< 提示条窗口
    QLabel *m_statusLabel = nullptr;     ///< 提示条状态文字
    QPushButton *m_finishBtn = nullptr;  ///< 完成按钮
    QPushButton *m_cancelBtn = nullptr;  ///< 取消按钮
    QRect m_captureRect;                 ///< 选区（全局坐标，已内缩避开边框）
    int m_noMatchStreak = 0;             ///< 连续无法定位的帧数
    bool m_active = false;               ///< 会话是否进行中
    bool m_showResyncNotice = false;     ///< 是否正在显示重新对齐提示
};

#endif // SCROLLCAPTURESESSION_H
