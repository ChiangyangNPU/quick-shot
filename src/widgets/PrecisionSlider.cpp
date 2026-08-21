#include "PrecisionSlider.h"
#include "../log/Logger.h"

/**
 * @brief 构造函数
 * @param orientation 滑块方向
 * @param parent 父对象
 * @author chiangyang
 */
PrecisionSlider::PrecisionSlider(Qt::Orientation orientation, QWidget *parent) 
    : QSlider(orientation, parent) {
    LOG_INFO(QString("PrecisionSlider instance created, orientation: %1")
             .arg(orientation == Qt::Horizontal ? "Horizontal" : "Vertical"));
}

/**
 * @brief 重写鼠标按下事件
 * @param event 鼠标事件
 * @author chiangyang
 */
void PrecisionSlider::mousePressEvent(QMouseEvent *event) {
    LOG_INFO(QString("Mouse press event at position: (%1,%2), orientation: %3")
             .arg(event->pos().x()).arg(event->pos().y())
             .arg(orientation() == Qt::Horizontal ? "Horizontal" : "Vertical"));
    if (orientation() == Qt::Horizontal) {
        // 计算鼠标点击位置对应的滑块值
        int x = event->pos().x();
        int w = width();
        if (x <= 0) {
            setValue(minimum());
            LOG_INFO(QString("Set value to minimum: %1").arg(minimum()));
        } else if (x >= w) {
            setValue(maximum());
            LOG_INFO(QString("Set value to maximum: %1").arg(maximum()));
        } else {
            int value = minimum() + ((maximum() - minimum()) * x) / w;
            setValue(value);
            LOG_INFO(QString("Set value based on position: %1").arg(value));
        }
    } else {
        // 垂直滑块的处理
        int y = event->pos().y();
        int h = height();
        if (y <= 0) {
            setValue(maximum());
            LOG_INFO(QString("Set value to maximum: %1").arg(maximum()));
        } else if (y >= h) {
            setValue(minimum());
            LOG_INFO(QString("Set value to minimum: %1").arg(minimum()));
        } else {
            int value = maximum() - ((maximum() - minimum()) * y) / h;
            setValue(value);
            LOG_INFO(QString("Set value based on position: %1").arg(value));
        }
    }
    // 调用父类的mousePressEvent，确保其他功能正常
    QSlider::mousePressEvent(event);
    LOG_INFO("Mouse press event handled");
}
