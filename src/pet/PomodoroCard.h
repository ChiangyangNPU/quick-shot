#ifndef POMODOROCARD_H
#define POMODOROCARD_H

#include <QWidget>

class QLabel;
class QPushButton;

/**
 * @brief 番茄钟提示卡（气泡样式）
 *
 * 无边框气泡小卡片，悬浮在宠物上方（空间不足时翻到下方），小三角指向宠物。
 * 两种形态：
 * - 工作结束：标题 +「休息 N 分钟」（主按钮）+「跳过本次」（次按钮）
 * - 休息结束：标题 +「继续工作」（主按钮）
 * 视觉复用待办气泡的画法（圆角面板 + 多层投影 + 小三角），配色取 PetTheme。
 * @author chiangyang
 */
class PomodoroCard : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象（顶层窗口传 nullptr）
     * @author chiangyang
     */
    explicit PomodoroCard(QWidget *parent = nullptr);

    /**
     * @brief 显示"工作结束，该休息了"形态
     * @param breakMinutes 本次休息时长（分钟，用于主按钮文案）
     * @author chiangyang
     */
    void showBreakDue(int breakMinutes);

    /**
     * @brief 显示"休息结束，继续工作"形态
     * @author chiangyang
     */
    void showWorkDue();

    /**
     * @brief 设置小三角朝向与水平锚点（贴合宠物定位用）
     * @param arrowBelow true 表示卡片在宠物上方、小三角朝下
     * @param anchorX 小三角在窗口内的水平位置（宠物中心）
     * @author chiangyang
     */
    void setBubblePlacement(bool arrowBelow, int anchorX);

signals:
    /**
     * @brief 用户选择"休息 N 分钟"
     * @author chiangyang
     */
    void breakTaken();

    /**
     * @brief 用户选择"跳过本次"（或 Esc 关闭工作结束卡片）
     * @author chiangyang
     */
    void skipped();

    /**
     * @brief 用户选择"继续工作"（或 Esc 关闭休息结束卡片）
     * @author chiangyang
     */
    void resumed();

protected:
    /**
     * @brief 绘制气泡背景：圆角面板、柔和投影与指向宠物的小三角
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief Esc 关闭卡片（按当前形态等效为跳过/继续）
     * @param event 按键事件
     * @author chiangyang
     */
    void keyPressEvent(QKeyEvent *event) override;

private:
    /**
     * @brief 当前卡片形态
     * @author chiangyang
     */
    enum class Mode {
        BreakDue, ///< 工作结束：休息/跳过
        WorkDue   ///< 休息结束：继续工作
    };

    /**
     * @brief 按当前语言与形态刷新标题与按钮文案
     * @author chiangyang
     */
    void updateTexts();

    /**
     * @brief 按当前主题刷新标题与按钮配色（构造与每次弹出时调用）
     * @author chiangyang
     */
    void applyStyle();

    /**
     * @brief 按小三角所在侧重新计算内容边距
     * @author chiangyang
     */
    void updateContentMargins();

    Mode m_mode = Mode::BreakDue;  ///< 当前形态
    int m_breakMinutes = 5;        ///< 本次休息时长（主按钮文案用）

    QLabel *m_title = nullptr;       ///< 标题
    QPushButton *m_primaryBtn = nullptr; ///< 主按钮（休息 N 分钟 / 继续工作）
    QPushButton *m_ghostBtn = nullptr; ///< 次按钮（跳过本次，休息结束形态隐藏）
    class QVBoxLayout *m_rootLayout = nullptr; ///< 根布局（朝向变化时调整边距）

    int m_shadowPad = 8;    ///< 投影留白（逻辑像素，构造时按 DPI 缩放）
    int m_arrowHeight = 9;  ///< 小三角高度（逻辑像素，构造时按 DPI 缩放）
    bool m_arrowBelow = true; ///< 小三角是否朝下（卡片位于宠物上方）
    int m_anchorX = -1;       ///< 小三角在窗口内的水平位置
};

#endif // POMODOROCARD_H
