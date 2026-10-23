#ifndef FLYANIMATIONWIDGET_H
#define FLYANIMATIONWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QRect>

/**
 * @brief 截图"飞入宠物"动画窗口
 *
 * 无边框、透明背景、鼠标穿透的置顶窗口：携带截图缩略图，
 * 从截图区域（sourceGlobalRect）沿加速曲线缩小飞到宠物嘴边，
 * 到达后发射 arrived() 并自毁。
 * @author chiangyang
 */
class FlyAnimationWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数（仅准备，不显示；调用 start() 开始动画）
     * @param pixmap 截图图像
     * @param fromGlobalRect 动画起点全局矩形（截图区域）
     * @param toGlobalPoint 动画终点全局坐标（宠物嘴部中心）
     * @author chiangyang
     */
    FlyAnimationWidget(const QPixmap &pixmap,
                       const QRect &fromGlobalRect,
                       const QPoint &toGlobalPoint);

    /**
     * @brief 开始飞行动画
     * @author chiangyang
     */
    void start();

signals:
    /**
     * @brief 到达宠物嘴边信号
     * @author chiangyang
     */
    void arrived();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_pixmap;   ///< 截图图像
    QRect m_startRect;  ///< 起始全局矩形（等比缩到适中大小后居中）
    QRect m_endRect;    ///< 结束全局矩形（终点处缩到小方块）
};

#endif // FLYANIMATIONWIDGET_H
