#ifndef SCROLLCAPTURESESSION_H
#define SCROLLCAPTURESESSION_H

#include "ScrollStitcher.h"
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRect>

class SnipScreen;
class QAbstractButton;
class QLabel;
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
 * 选区边框改由独立的透明穿透窗口（参考框）绘制，边框位于选区外侧，
 * 裁剪选区时不会被拍入（Windows 另加捕获排除兜底）。
 * 提示条同样定位在选区外侧。
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
     * @brief 提示条按钮的基础文案（翻译文字，不含快捷键提示）
     * @param role 0 方向 / 1 灵敏度 / 2 完成 / 3 取消
     * @author chiangyang
     */
    QString hintButtonLabel(int role) const;

    /**
     * @brief 设置对齐灵敏度并持久化（0 宽松 / 1 标准 / 2 严格）
     *
     * 灵敏度映射到拼接器的重叠行数要求与 NCC 判定阈值：宽松容忍更快的
     * 滚动与更弱的纹理（"画面变化过大"更少出现），严格抗误拼优先。
     * 会话进行中切换立即生效，不影响已拼接内容。
     * @param level 0 宽松 / 1 标准 / 2 严格（越界自动收敛）
     * @author chiangyang
     */
    void setSensitivity(int level);

    /**
     * @brief 宿主退出时强制清理（不触发完成/取消信号）
     * @author chiangyang
     */
    void shutdown();

signals:
    /**
     * @brief 拼接完成
     * @param pixmap 长图（物理像素，携带选区屏 DPR）
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
     * @brief 创建选区参考框窗口（透明、鼠标穿透，仅绘制边框）
     * @param selection 选区矩形（全局坐标）
     * @author chiangyang
     */
    void createFrameOverlay(const QRect &selection);

    /**
     * @brief 销毁选区参考框窗口
     * @author chiangyang
     */
    void destroyFrameOverlay();

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
    QPointer<QWidget> m_frameOverlay;    ///< 选区参考框窗口（悬浮在选区位）
    QLabel *m_statusLabel = nullptr;     ///< 提示条状态文字
    QAbstractButton *m_orientationBtn = nullptr; ///< 滚动方向切换按钮（垂直/水平）
    QAbstractButton *m_sensitivityBtn = nullptr; ///< 灵敏度切换按钮（宽松/标准/严格）
    QAbstractButton *m_finishBtn = nullptr;  ///< 完成按钮
    QAbstractButton *m_cancelBtn = nullptr;  ///< 取消按钮
    QRect m_captureRect;                 ///< 选区（全局坐标，已内缩避开边框）
    qreal m_dpr = 1.0;                   ///< 选区所在屏 DPR（拼接结果按此折算逻辑尺寸）
    int m_noMatchStreak = 0;             ///< 连续无法定位的帧数
    int m_sensitivity = 1;               ///< 对齐灵敏度（0 宽松 / 1 标准 / 2 严格）
    int m_noMatchResetThreshold = 8;     ///< 连续无法定位多少帧后重设基准（随灵敏度）
    bool m_active = false;               ///< 会话是否进行中
    bool m_showResyncNotice = false;     ///< 是否正在显示重新对齐提示
};

#endif // SCROLLCAPTURESESSION_H
