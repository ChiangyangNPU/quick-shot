#ifndef PETWINDOW_H
#define PETWINDOW_H

#include <QWidget>
#include <QPoint>

/**
 * @brief 桌面宠物窗口
 *
 * 无边框透明置顶小窗口，用 QPainter 代码绘制一只圆滚滚的卡通宠物
 * （闭嘴/张嘴/咀嚼三态），支持：
 * - 鼠标拖动改变位置（拖动结束发射 dragFinished）
 * - 单击发射 clicked（与拖动按移动距离区分）
 * - openMouth() 张嘴等待食物，chew() 咀嚼弹跳
 * - 待机时随机眨眼
 * @author chiangyang
 */
class PetWindow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal mouthOpen READ mouthOpen WRITE setMouthOpen)
    Q_PROPERTY(qreal chewScale READ chewScale WRITE setChewScale)
    Q_PROPERTY(qreal starProgress READ starProgress WRITE setStarProgress)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象（顶层窗口传 nullptr）
     * @author chiangyang
     */
    explicit PetWindow(QWidget *parent = nullptr);

    /**
     * @brief 宠物窗口边长（正方形）
     * @return 边长像素值
     * @author chiangyang
     */
    static int petSize() { return 96; }

    /**
     * @brief 张嘴等待食物（飞行动画开始时调用）
     * @author chiangyang
     */
    void openMouth();

    /**
     * @brief 咀嚼吞下（飞行动画到达时调用）：闭嘴 + 多轮咀嚼挤压回弹，
     *        完成后左眼闪一颗星星作为奖励
     * @author chiangyang
     */
    void chew();

    /**
     * @brief 宠物嘴部的全局屏幕坐标（飞行动画终点）
     * @return 嘴部中心全局坐标
     * @author chiangyang
     */
    QPoint mouthCenterGlobal() const;

    qreal mouthOpen() const { return m_mouthOpen; }
    void setMouthOpen(qreal v);

    qreal chewScale() const { return m_chewScale; }
    void setChewScale(qreal v);

    qreal starProgress() const { return m_starProgress; }
    void setStarProgress(qreal v);

signals:
    /**
     * @brief 单击信号（与拖动区分：移动距离未超阈值）
     * @author chiangyang
     */
    void clicked();

    /**
     * @brief 拖动结束信号
     * @param newPos 拖动结束后的窗口左上角全局坐标
     * @author chiangyang
     */
    void dragFinished(const QPoint &newPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /**
     * @brief 调度下一次眨眼（随机 2.4~5 秒；星星闪烁期间自动跳过）
     * @author chiangyang
     */
    void scheduleBlink();

    /**
     * @brief 左眼星星闪烁动画（咀嚼完成后触发，闪完恢复眨眼状态）
     * @author chiangyang
     */
    void sparkleStar();

    qreal m_mouthOpen = 0.0;        ///< 嘴张开程度 [0,1]，驱动 paintEvent 嘴形
    qreal m_chewScale = 1.0;        ///< 咀嚼挤压系数（垂直方向），1 为常态
    qreal m_starProgress = 0.0;     ///< 左眼星星闪烁进度 [0,1]，0 为无星星
    bool m_eyesClosed = false;      ///< 眨眼瞬间为 true

    bool m_pressing = false;        ///< 左键按下中
    bool m_dragging = false;        ///< 已判定为拖动（移动距离超阈值）
    QPoint m_dragOffset;            ///< 按下点相对窗口左上角的偏移
    QPoint m_pressGlobalPos;        ///< 按下时的全局坐标（拖动阈值判定用）
};

#endif // PETWINDOW_H
