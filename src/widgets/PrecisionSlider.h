#ifndef PRECISIONSLIDER_H
#define PRECISIONSLIDER_H

#include <QSlider>
#include <QMouseEvent>

/**
 * @brief 精准滑块类
 * 
 * 重写了鼠标事件处理函数，确保鼠标单击时能够精准地跳转到点击的位置
 * @author chiangyang
 */
class PrecisionSlider : public QSlider {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param orientation 滑块方向
     * @param parent 父对象
     * @author chiangyang
     */
    explicit PrecisionSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    /**
     * @brief 重写鼠标按下事件
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // PRECISIONSLIDER_H
