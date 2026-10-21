#ifndef RECORDINGSESSION_H
#define RECORDINGSESSION_H

#include <QObject>
#include <QPoint>
#include <QImage>

class SnipScreen;
class RecordingToolBar;
class RecordingControlWindow;
class ScreenRecorder;

/**
 * @brief 录屏编排协调器
 *
 * 从 SnipScreen（2369 行上帝类）拆出的录屏编排职责，SnipScreen 保留
 * 模式切换入口与截图路径，录屏侧的全部细节收敛到本类：
 * - 录屏工具栏 / 录制控制栏 / ScreenRecorder 的创建与信号接线
 * - 录制生命周期：启动、暂停/恢复、停止（保留文件）、取消（删除文件）
 * - Windows WDA_EXCLUDEFROMCAPTURE：把 SnipScreen 排除出捕获，
 *   避免屏幕上已绘制的标注与 overlay 合成重复
 * - 标注 overlay 渲染与推送（renderAnnotationOverlay / pushAnnotationOverlay）
 * - 录屏输入遮罩 updateInputMask（非标注工具状态下非交互区域穿透）
 * - resetUi：停止/取消路径公共的 7 步 UI 复位
 *
 * 与 AnnotationInteractionHandler 的 Host 回调模式不同：本类只有一个宿主
 * （SnipScreen），无复用诉求，直接持有宿主指针（前置声明避免循环包含）
 * 并通过宿主的公共访问器读写共享状态（选区、背景图、标注管理器等）。
 *
 * @author chiangyang
 */
class RecordingSession : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param host 宿主 SnipScreen（非拥有，生命周期由 main.cpp 管理，须长于本对象）
     * @param parent QObject 父对象（传宿主以随宿主销毁）
     */
    explicit RecordingSession(SnipScreen *host, QObject *parent = nullptr);

    /**
     * @brief 创建录屏工具栏/控制栏/录制器（SnipScreen 构造阶段调用，替代原 createComponents 录屏段）
     */
    void createComponents();

    /**
     * @brief 接线控制栏/录制器/工具栏信号（替代原 connectRecordingSignals + connectRecordingToolbarSignals）
     */
    void connectSignals();

    // ---------- 模式与状态 ----------
    bool isMode() const;              ///< 是否处于录屏模式（含已选区未开录）
    void setMode(bool recording);     ///< 切换模式标志（startCapture/switchTo* 调用）
    bool isRecording() const;         ///< 是否正在录制中

    // ---------- 组件访问 ----------
    RecordingToolBar *toolbar() const;      ///< 录屏工具栏（工具栏家族切换/设置同步需要）
    RecordingControlWindow *control() const;  ///< 录制控制栏（工具栏定位需要）
    ScreenRecorder *recorder() const;       ///< 录制器（预留）

    // ---------- 供 SnipScreen 委托的批处理 ----------
    void uncheckAllAnnotationBtns();        ///< 取消工具栏全部标注按钮选中
    void retranslateUi();                   ///< 语言切换时刷新工具栏文字
    void refreshDefaultValues();            ///< 设置页默认值变更时刷新工具栏
    void updateToolBarState(bool hasSelection, bool canUndo, bool canRedo);
    bool isMouseInToolbars(const QPoint &globalPos) const;  ///< 鼠标是否在录屏工具栏/子工具栏/控制栏上

    // ---------- 工具栏显隐 ----------
    void showToolBar();               ///< 显示录屏工具栏（选区旁定位）
    void hideToolBar();               ///< 隐藏录屏工具栏、子工具栏与控制栏

    /**
     * @brief 打开录制控制栏并预选 GIF 格式
     *
     * 截屏工具栏与录屏工具栏的 GIF 按钮共用入口：录屏按钮置为选中以保持
     * 工具栏状态一致，在控制栏中再点击「开始」即开始录制 GIF。
     * @author chiangyang
     */
    void showControlForGif();

    // ---------- 录制生命周期（全局热键与 UI 入口共用） ----------
    void togglePause();               ///< 暂停/恢复（非录制态 no-op）
    void stop();                      ///< 停止并保留文件（非录制态 no-op）
    void cancel();                    ///< 取消：删除视频文件（复位 UI + 清遮罩 + cancel）

    /**
     * @brief 复位录制相关 UI 状态（停止/取消路径的公共 7 步）
     *
     * stopTimer/resetRecordBtn/hideTimerLabel/setRecording/setPaused/hide。
     * 不含 clearMask() 与 stop()/cancel() 收尾——控制栏停止路径有意保持输入
     * 遮罩直到 stopped 信号触发宿主 exit()；取消/热键路径自行清遮罩。
     */
    void resetUi();

    // ---------- 标注 overlay 与输入遮罩 ----------
    QImage renderAnnotationOverlay() const;  ///< 渲染输出分辨率叠加图（背景+标注+马赛克）
    void pushAnnotationOverlay();            ///< 录制中时把叠加图推送给录制器
    void updateInputMask();                  ///< 更新输入遮罩（非交互区域鼠标穿透）

private:
    SnipScreen *m_host;                     ///< 宿主（非拥有）
    RecordingToolBar *m_toolbar = nullptr;  ///< 录屏工具栏
    RecordingControlWindow *m_control = nullptr;  ///< 录制控制栏
    ScreenRecorder *m_recorder = nullptr;   ///< 录制器
    bool m_isMode = false;                  ///< 是否处于录屏模式（含已选区未开录）
    bool m_isRecordingCanceled = false;     ///< 录制是否被取消（取消时不退出截图）
};

#endif // RECORDINGSESSION_H
