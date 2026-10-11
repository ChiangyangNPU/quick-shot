#ifndef FLOATINGWIDGET_H
#define FLOATINGWIDGET_H

#include <QTextEdit>
#include <QMouseEvent>

/**
 * @brief 可拖拽移动/角点缩放的浮动文本控件基类（模板方法）
 *
 * 承载覆盖层浮动件的完整交互层：四角命中与对角缩放、拖拽移动、
 * 边界矩形钳制、手柄绘制覆盖层、右上角关闭手柄。
 * 子类（OverlayTextEdit）通过三个钩子注入自身状态：
 * - currentRotation()：geometryChanged 信号的旋转角载荷
 * - onFloatingResizeStarted()：角缩放开始时快照子类状态（如字体原值）
 * - onFloatingScaled()：按对角线缩放比例应用子类状态
 * @author chiangyang
 */
class FloatingWidget : public QTextEdit {
    Q_OBJECT

signals:
    /**
     * @brief 几何形状变化信号（无旋转角载荷的钩子，保留供未来订阅）
     * @author chiangyang
     */
    void geometryChanged(const QRect &rect);

    /**
     * @brief 几何形状变化信号（移动/缩放/旋转都会触发）
     * @param rect 新的几何形状
     * @param rotationDegrees 当前旋转角度（度，由子类 currentRotation 提供）
     * @author chiangyang
     */
    void geometryChanged(const QRect &rect, qreal rotationDegrees);

    /**
     * @brief 关闭请求信号（点击右上角 X 手柄时发出）
     * @author chiangyang
     */
    void closeRequested();

public:
    /**
     * @brief 构造函数
     * @param parent 父控件（手柄覆盖层挂在其上，避免被 viewport 裁切）
     * @author chiangyang
     */
    explicit FloatingWidget(QWidget *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~FloatingWidget() override;

    /**
     * @brief 设置允许移动和缩放的边界矩形（父控件坐标系）
     * @param rect 边界矩形，控件将被限制在此区域内；空矩形表示不限制
     * @author chiangyang
     */
    void setBoundaryRect(const QRect &rect);

    /**
     * @brief 处理鼠标按下事件（手柄覆盖层事件转发入口）
     * @author chiangyang
     */
    void handleMousePress(QMouseEvent *event);

    /**
     * @brief 处理鼠标移动事件
     * @author chiangyang
     */
    void handleMouseMove(QMouseEvent *event);

    /**
     * @brief 处理鼠标释放事件
     * @author chiangyang
     */
    void handleMouseRelease(QMouseEvent *event);

protected:
    /**
     * @brief 命中目标枚举
     * @author chiangyang
     */
    enum class HitTarget {
        None,           ///< 无目标（文本编辑区）
        TopLeft,        ///< 左上角（缩放）
        TopRight,       ///< 右上角（关闭按钮）
        BottomLeft,     ///< 左下角（缩放）
        BottomRight,    ///< 右下角（缩放）
        Move            ///< 移动（四边边缘区域）
    };

    // 手柄可视参数（单一事实来源，HandleOverlay 与命中检测共用）
    static constexpr int kHandleRadius = 6;
    static constexpr int kHandleHitPadding = 5;
    static constexpr int kMinWidth = 50;
    static constexpr int kMinHeight = 30;

    /**
     * @brief 当前旋转角度（geometryChanged 载荷；子类可重写）
     * @author chiangyang
     */
    virtual qreal currentRotation() const;

    /**
     * @brief 角缩放开始钩子（子类快照自身缩放基准状态）
     * @author chiangyang
     */
    virtual void onFloatingResizeStarted();

    /**
     * @brief 角缩放钩子（按对角线缩放比例应用子类状态）
     * @param diagScaleFactor 对角线缩放比例（新对角线/起始对角线）
     * @author chiangyang
     */
    virtual void onFloatingScaled(qreal diagScaleFactor);

    /**
     * @brief 命中检测
     * @param pos 控件内坐标
     * @return 命中目标
     * @author chiangyang
     */
    HitTarget hitTest(const QPoint &pos) const;

    /**
     * @brief 手柄覆盖层应覆盖的帧（编辑框本地坐标，含手柄半径余量）
     *
     * 默认为未旋转的包围框；旋转子类重写返回旋转后的包围盒帧，
     * 使手柄视觉与命中跟随旋转。
     * @author chiangyang
     */
    virtual QRectF handleOverlayFrame() const;

    /**
     * @brief 是否有拖拽/缩放交互进行中（子类可据此挂起内容自适应等副作用）
     * @author chiangyang
     */
    bool isInteractionActive() const { return m_hitTarget != HitTarget::None || m_isMoving; }

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

protected:
    /**
     * @brief 更新手柄覆盖层位置、几何与旋转帧
     * @author chiangyang
     */
    void updateHandleOverlayPosition();

private:
    /**
     * @brief 获取对角线长度
     * @return 对角线长度
     * @author chiangyang
     */
    qreal diagonalLength() const;

    /**
     * @brief 处理角手柄拖拽缩放
     * @param currentGlobal 当前鼠标全局坐标
     * @author chiangyang
     */
    void handleCornerResize(const QPointF &currentGlobal);

    QWidget *m_handleOverlay = nullptr; ///< 手柄绘制覆盖层（父控件的子控件）
    QRect m_boundaryRect;               ///< 允许移动和缩放的边界矩形（父控件坐标系）
    HitTarget m_hitTarget = HitTarget::None; ///< 当前命中目标
    bool m_isMoving = false;            ///< 是否正在移动
    QPoint m_dragStartPosition;         ///< 移动拖拽起始位置（控件坐标）
    QPointF m_dragStartGlobalPos;       ///< 移动拖拽起始全局坐标
    QPoint m_widgetStartPos;            ///< 移动拖拽时控件初始位置
    QRect m_originalGeometry;           ///< 原始几何形状（缩放前）
    QPointF m_pressGlobalPos;           ///< 缩放按下时全局鼠标位置
    qreal m_diagOrigLength = 0.0;       ///< 缩放起始对角线长度
};

#endif // FLOATINGWIDGET_H
