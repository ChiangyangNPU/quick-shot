#ifndef OVERLAYTEXTEDIT_H
#define OVERLAYTEXTEDIT_H

#include <QTextEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QCursor>
#include <QToolButton>
#include <QGraphicsOpacityEffect>

/**
 * @brief 覆盖层文本编辑类
 *
 * 用于在截图上添加和编辑文本的浮动文本编辑框，支持拖拽移动和调整大小
 * @author chiangyang
 */
class OverlayTextEdit : public QTextEdit {
    Q_OBJECT

signals:
    /**
     * @brief 文本框几何形状变化信号
     * @param rect 新的几何形状
     * @author chiangyang
     */
    void geometryChanged(const QRect &rect);

    /**
     * @brief 文本框旋转角度变化信号
     * @param rect 新的几何形状
     * @param rotation 旋转角度（度）
     * @author chiangyang
     */
    void geometryChanged(const QRect &rect, qreal rotation);

    /**
     * @brief 关闭请求信号（点击右上角 X 手柄时发出）
     * @author chiangyang
     */
    void closeRequested();

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit OverlayTextEdit(QWidget *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~OverlayTextEdit() override;

    /**
     * @brief 获取旋转角度
     * @return 旋转角度（度）
     * @author chiangyang
     */
    qreal rotationDegrees() const;

    /**
     * @brief 调整文本框大小以适应内容
     * @author chiangyang
     */
    void adjustSizeToContent();

    /**
     * @brief 设置编辑器字体大小
     * @param px 像素大小
     * @author chiangyang
     */
    void setFontSize(int px);

    /**
     * @brief 获取当前字体大小
     * @return 像素大小
     * @author chiangyang
     */
    int fontSize() const;

    /**
     * @brief 设置编辑器颜色
     * @param color 颜色
     * @author chiangyang
     */
    void setEditorColor(const QColor &color);

    /**
     * @brief 设置允许移动和缩放的边界矩形（父控件坐标系）
     * @param rect 边界矩形，控件将被限制在此区域内
     * @author chiangyang
     */
    void setBoundaryRect(const QRect &rect);

    /**
     * @brief 处理鼠标按下事件
     * @param event 鼠标事件
     * @author chiangyang
     */
    void handleMousePress(QMouseEvent *event);

    /**
     * @brief 处理鼠标移动事件
     * @param event 鼠标事件
     * @author chiangyang
     */
    void handleMouseMove(QMouseEvent *event);

    /**
     * @brief 处理鼠标释放事件
     * @param event 鼠标事件
     * @author chiangyang
     */
    void handleMouseRelease(QMouseEvent *event);

protected:
    /**
     * @brief 鼠标移动事件处理
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标按下事件处理
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件处理
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 绘制事件处理
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 重写调整大小事件
     * @param event 调整大小事件
     * @author chiangyang
     */
    void resizeEvent(QResizeEvent *event) override;

    /**
     * @brief 重写移动事件
     * @param event 移动事件
     * @author chiangyang
     */
    void moveEvent(QMoveEvent *event) override;

    /**
     * @brief 重写显示事件
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

    /**
     * @brief 重写隐藏事件
     * @param event 隐藏事件
     * @author chiangyang
     */
    void hideEvent(QHideEvent *event) override;

    /**
     * @brief 重写事件过滤器
     * @param watched 被监视的对象
     * @param event 事件
     * @return 是否处理了事件
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
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

    /**
     * @brief 命中检测
     * @param pos 控件内坐标
     * @return 命中目标
     * @author chiangyang
     */
    HitTarget hitTest(const QPoint &pos) const;

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

    /**
     * @brief 更新样式表
     * @author chiangyang
     */
    void updateStyleSheet();

    /**
     * @brief 更新手柄覆盖层位置
     * @author chiangyang
     */
    void updateHandleOverlayPosition();

    /**
     * @brief 更新旋转按钮位置
     * @author chiangyang
     */
    void updateRotateButtonPosition();

    /**
     * @brief 更新旋转预览
     * @author chiangyang
     */
    void updateRotationPreview();

    // 手柄可视参数
    static constexpr int kHandleRadius = 6;
    static constexpr int kHandleHitPadding = 5;
    static constexpr int kMinWidth = 50;
    static constexpr int kMinHeight = 30;
    static constexpr int kMinFontSize = 8;

    HitTarget m_hitTarget;              ///< 当前命中目标
    bool m_isMoving;                    ///< 是否正在移动
    QPoint m_dragStartPosition;         ///< 移动拖拽起始位置（控件坐标）
    QPointF m_dragStartGlobalPos;       ///< 移动拖拽起始全局坐标
    QPoint m_widgetStartPos;            ///< 移动拖拽时控件初始位置
    QRect m_originalGeometry;           ///< 原始几何形状（缩放前）
    QPointF m_pressGlobalPos;           ///< 缩放按下时全局鼠标位置
    qreal m_diagOrigLength;             ///< 缩放起始对角线长度
    int m_origFontSize;                 ///< 缩放起始字体大小

    int m_fontSize;                     ///< 当前字体像素大小
    QColor m_editorColor;               ///< 当前编辑器颜色

    QToolButton *rotateButton;          ///< 旋转按钮
    bool m_isRotating;                  ///< 是否正在旋转
    bool m_rotationPreviewActive;       ///< 旋转预览是否激活
    QPointF m_rotateCenterGlobal;       ///< 旋转中心全局坐标
    QPointF m_rotatePressGlobal;        ///< 旋转按下时全局坐标
    qreal m_currentRotationDegrees;     ///< 当前旋转角度

    QGraphicsOpacityEffect *m_editorOpacityEffect; ///< 编辑器透明度效果
    QWidget *m_rotationPreview;         ///< 旋转预览窗口
    QWidget *m_handleOverlay;           ///< 手柄绘制覆盖层（位于viewport之上）
    QRect m_boundaryRect;               ///< 允许移动和缩放的边界矩形（父控件坐标系）
};

#endif // OVERLAYTEXTEDIT_H
