#ifndef WEBFLYOUTWIDGET_H
#define WEBFLYOUTWIDGET_H

#include <QRect>
#include <QRectF>
#include <QString>
#include <QWidget>

/**
 * @brief 「吐出网页」飞出动画窗口
 *
 * 无边框、透明背景、鼠标穿透的置顶窗口：携带一张自绘的网页卡片，
 * 从宠物嘴边（mouthGlobal）沿屏幕中心方向飞出并放大，末段淡出。
 * 淡出开始时发出 opening()，供调用方打开浏览器；结束后自毁。
 * @author chiangyang
 */
class WebFlyOutWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    /**
     * @brief 构造函数（仅准备，不显示；调用 start() 开始动画）
     * @param domain 卡片上显示的域名
     * @param mouthGlobal 宠物嘴部全局坐标（动画起点）
     * @param screenRect 目标屏幕可用区域（用于收敛终点位置）
     * @author chiangyang
     */
    WebFlyOutWidget(const QString &domain, const QPoint &mouthGlobal, const QRect &screenRect);

    /**
     * @brief 开始飞出动画
     * @author chiangyang
     */
    void start();

    /**
     * @brief 当前动画进度（0 → 1）
     * @return 动画进度
     * @author chiangyang
     */
    qreal progress() const { return m_progress; }

    /**
     * @brief 设置动画进度：同步几何、透明度，并在淡出开始时发出 opening()
     * @param p 动画进度
     * @author chiangyang
     */
    void setProgress(qreal p);

signals:
    /**
     * @brief 卡片开始淡出（此时打开浏览器，避免动效结束后可见空档）
     * @author chiangyang
     */
    void opening();

    /**
     * @brief 动画结束（窗口即将自毁）
     * @author chiangyang
     */
    void finished();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 按进度插值当前窗口矩形（中心位置与尺寸同时变化）
     * @param p 动画进度
     * @return 当前矩形
     * @author chiangyang
     */
    QRectF geometryAt(qreal p) const;

    QString m_domain;      ///< 卡片显示的域名
    QRectF m_startRect;    ///< 起始矩形（宠物嘴边的小卡片）
    QRectF m_endRect;      ///< 结束矩形（屏幕中央方向的大卡片）
    qreal m_progress = 0.0; ///< 动画进度
    bool m_opened = false;  ///< opening() 是否已发出（只发一次）
};

#endif // WEBFLYOUTWIDGET_H
