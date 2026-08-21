#ifndef ANNOTATIONINTERACTIONHANDLER_H
#define ANNOTATIONINTERACTIONHANDLER_H

#include "AnnotationManager.h"
#include <QPoint>
#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <Qt>
#include <functional>

class OverlayTextEdit;

/**
 * @brief 标注交互处理器
 *
 * 提取 SnipScreen（截屏/录屏）和 PinWindow（贴图窗口）中高度重复的标注交互逻辑，
 * 通过 Host 回调接口注入窗口特有差异（坐标系、选区限制、录屏同步等），
 * 实现一份代码两处复用。
 *
 * 设计模式：策略模式（通过 Host 回调注入差异行为）
 *
 * 使用方法：
 * 1. 宿主窗口持有 AnnotationInteractionHandler 实例
 * 2. 通过 setHost() 注入回调（坐标系转换、选区限制、刷新、录屏同步等）
 * 3. 鼠标事件转发给 handleMousePress/Move/Release()
 * 4. paintEvent 中调用 drawAnnotations() 或 drawWithMosaic()
 * 5. 退出时调用 exitAnnotation()
 *
 * @author chiangyang
 */
class AnnotationInteractionHandler {
public:
    /**
     * @brief 宿主回调接口
     *
     * 由各窗口实现并注入，用于解决坐标系、选区、录屏同步等差异。
     * 所有回调均为 std::function，宿主通过 lambda 注入具体实现。
     * @author chiangyang
     */
    struct Host {
        /// 坐标限制：将任意坐标限制到有效选区边界内（SnipScreen→选区，PinWindow→窗口rect）
        std::function<QPoint(const QPoint&)> clampPos;

        /// 判断坐标是否在有效选区内（SnipScreen→选区contains，PinWindow→恒true）
        std::function<bool(const QPoint&)> isInSelection;

        /// 判断坐标是否在工具栏上（SnipScreen→isMouseInToolBar，PinWindow→恒false，工具栏为独立顶层窗口）
        std::function<bool(const QPoint&)> isInToolBar;

        /// 获取选区矩形（用于 Shift/Alt 修饰键约束）
        std::function<QRect()> selectionRect;

        /// 请求宿主刷新界面（调用 update()）
        std::function<void()> requestUpdate;

        /// 录屏 overlay 同步（SnipScreen→pushAnnotationOverlay，PinWindow→空操作）
        std::function<void()> syncOverlay;

        /// 更新工具栏撤销/重做按钮状态
        std::function<void(bool canUndo, bool canRedo)> updateToolBarState;

        /// 创建文本编辑框（各窗口坐标转换不同，由宿主实现）
        std::function<void(const QPoint& pos)> createTextEdit;

        /// 完成文本编辑，将文本内容创建为标注（各窗口基线计算不同，由宿主实现）
        std::function<void()> finalizeTextEdit;

        /// 获取当前活动的文本编辑框指针（nullptr 表示无活动文本框）
        std::function<OverlayTextEdit*()> activeTextEdit;

        /// 设置光标样式
        std::function<void(Qt::CursorShape)> setCursor;

        /// 禁用/恢复标注快捷键裸键（文本编辑框获得/失去焦点时调用）
        std::function<void(bool enabled)> setBareKeysEnabled;
    };

    /**
     * @brief 构造函数
     * @author chiangyang
     */
    AnnotationInteractionHandler() = default;

    /**
     * @brief 设置宿主回调接口
     * @param host 宿主回调结构体
     * @author chiangyang
     */
    void setHost(Host host) { m_host = std::move(host); }

    // ---------- 交互入口（鼠标事件转发） ----------

    /**
     * @brief 处理鼠标按下事件
     *
     * 交互优先级：工具栏 → 文本框外部完成 → 橡皮擦 → 马赛克 → 控制点命中 →
     *             文本工具（创建/拖动）→ 标注拖动 → 创建新标注
     *
     * @param pos 鼠标位置（SnipScreen 为全局坐标，PinWindow 为窗口本地坐标）
     * @param modifiers 键盘修饰键
     * @return true 表示事件已处理，宿主不应继续传播
     * @author chiangyang
     */
    bool handleMousePress(const QPoint &pos, Qt::KeyboardModifiers modifiers);

    /**
     * @brief 处理鼠标移动事件
     *
     * 交互优先级：控制点拖拽 → 标注拖动 → 擦除 → 马赛克涂抹 → 绘制中 → 悬停光标反馈
     *
     * @param pos 鼠标位置
     * @param modifiers 键盘修饰键
     * @return true 表示事件已处理
     * @author chiangyang
     */
    bool handleMouseMove(const QPoint &pos, Qt::KeyboardModifiers modifiers);

    /**
     * @brief 处理鼠标释放事件
     *
     * 结束当前进行的操作（控制点拖拽/标注拖动/擦除/马赛克/绘制），
     * 提交操作记录用于撤销/重做。
     *
     * @return true 表示事件已处理
     * @author chiangyang
     */
    bool handleMouseRelease();

    // ---------- 绘制 ----------

    /**
     * @brief 绘制标注和控制点（非马赛克分支）
     *
     * 在已画好背景的 painter 上直接绘制标注和控制点。
     *
     * @param painter 目标画笔
     * @param offset 坐标偏移（SnipScreen 为 globalOffset，PinWindow 为 QPoint(0,0)）
     * @author chiangyang
     */
    void drawAnnotations(QPainter &painter, const QPoint &offset);

    /**
     * @brief 绘制标注、马赛克和控制点（马赛克分支）
     *
     * 在已画好背景的 canvas 上依次绘制：标注 → 马赛克（像素化）→ 控制点（不被像素化）。
     * 控制点在马赛克之后绘制，确保控制点清晰可见。
     *
     * @param canvasPainter canvas 的画笔
     * @param canvas 已画好背景的离屏画布（马赛克以此为背景采样）
     * @param blockSize 马赛克块大小（下采样缩放因子）
     * @param offset 坐标偏移
     * @author chiangyang
     */
    void drawWithMosaic(QPainter &canvasPainter, QPixmap &canvas,
                        int blockSize, const QPoint &offset);

    // ---------- 标注创建 ----------

    /**
     * @brief 根据当前工具类型创建标注对象
     *
     * 根据 m_currentTool 和 m_currentShapeType 创建对应的标注对象并添加到管理器。
     * 文本工具和马赛克工具不由此方法创建（分别由 createTextEdit 和 mosaicAt 处理）。
     *
     * @param start 标注起始点
     * @author chiangyang
     */
    void createAnnotation(const QPoint &start);

    // ---------- 退出与清理 ----------

    /**
     * @brief 退出标注模式，重置所有交互状态
     *
     * 重置绘制/擦除/拖拽等状态标志。注意：不清理标注数据（标注保留供后续查看），
     * 不清理文本编辑框和工具栏（由宿主负责）。
     *
     * @author chiangyang
     */
    void exitAnnotation();

    // ---------- 属性访问 ----------

    /**
     * @brief 获取标注管理器引用（可写版本）
     * @return AnnotationManager 引用，用于直接访问标注栈和撤销/重做
     * @author chiangyang
     */
    AnnotationManager& manager() { return m_annotationManager; }

    /**
     * @brief 获取标注管理器引用（只读版本）
     * @return const AnnotationManager 引用
     * @author chiangyang
     */
    const AnnotationManager& manager() const { return m_annotationManager; }

    /**
     * @brief 获取当前选中的标注工具类型
     * @return 工具类型枚举
     * @author chiangyang
     */
    AnnotationType tool() const { return m_currentTool; }

    /**
     * @brief 设置当前标注工具
     * @param tool 工具类型枚举
     * @note 工具栏切换工具时调用，不影响当前正在进行的绘制操作。
     *       切换工具后立即触发界面刷新，使控制点的显示/隐藏同步生效
     *       （如切换到橡皮擦/马赛克后栈顶标注的控制点立即消失）。
     * @author chiangyang
     */
    void setTool(AnnotationType tool) {
        m_currentTool = tool;
        if (m_host.requestUpdate) m_host.requestUpdate();
    }

    /**
     * @brief 获取当前画笔颜色
     * @return 颜色引用
     * @author chiangyang
     */
    const QColor& color() const { return m_currentColor; }

    /**
     * @brief 设置画笔颜色
     * @param color 新颜色值
     * @note 工具栏颜色面板切换时调用，影响下一个创建的标注
     * @author chiangyang
     */
    void setColor(const QColor &color) { m_currentColor = color; }

    /**
     * @brief 获取当前画笔宽度
     * @return 画笔粗细值（像素）
     * @author chiangyang
     */
    int penWidth() const { return m_currentPenWidth; }

    /**
     * @brief 设置画笔宽度
     * @param width 画笔粗细值（像素），建议 1~20
     * @note 影响矩形/椭圆/箭头/直线/画笔的描边宽度，以及马赛克和橡皮擦的涂抹半径
     * @author chiangyang
     */
    void setPenWidth(int width) { m_currentPenWidth = width; }

    /**
     * @brief 获取当前文本字号
     * @return 字号（像素）
     * @author chiangyang
     */
    int fontSize() const { return m_currentFontSize; }

    /**
     * @brief 设置文本字号
     * @param size 字号（像素），建议 1~20
     * @note 仅影响文本工具创建的 TextAnnotation
     * @author chiangyang
     */
    void setFontSize(int size) { m_currentFontSize = size; }

    /**
     * @brief 获取当前形状子类型
     * @return 形状类型值：1=矩形 2=椭圆 3=三角形
     * @note 工具栏的形状切换按钮会把三种几何工具合并，用此值区分选中的具体形状
     * @author chiangyang
     */
    int shapeType() const { return m_currentShapeType; }

    /**
     * @brief 设置当前形状子类型
     * @param type 形状类型值：1=矩形 2=椭圆 3=三角形
     * @author chiangyang
     */
    void setShapeType(int type) { m_currentShapeType = type; }

    /**
     * @brief 是否正在绘制几何/画笔/文本标注
     * @return true=鼠标按下尚未释放，正在绘制过程中
     * @author chiangyang
     */
    bool isAnnotating() const { return m_isAnnotating; }

    /**
     * @brief 是否正在执行擦除操作
     * @return true=橡皮擦涂抹中
     * @author chiangyang
     */
    bool isErasing() const { return m_isErasing; }

    /**
     * @brief 是否正在执行马赛克涂抹
     * @return true=马赛克涂抹中
     * @author chiangyang
     */
    bool isMosing() const { return m_isMosing; }

    /**
     * @brief 是否正在拖动已提交的标注（整体平移）
     * @return true=拖动移动中
     * @author chiangyang
     */
    bool isDraggingAnnotation() const { return m_isDraggingAnnotation; }

    /**
     * @brief 是否正在拖拽标注的控制点（改变形状/大小）
     * @return true=控制点拖拽中
     * @author chiangyang
     */
    bool isDraggingControlPoint() const { return m_isDraggingControlPoint; }

    /// 是否有任何交互操作正在进行（用于判断是否需要阻止其他事件处理）
    bool isInteractionActive() const {
        return m_isAnnotating || m_isErasing || m_isMosing ||
               m_isDraggingAnnotation || m_isDraggingControlPoint;
    }

private:
    /**
     * @brief 判断当前工具是否支持拖动已提交的标注
     *
     * 橡皮擦和马赛克工具不支持拖动（它们是涂抹式工具），
     * 其他工具（矩形/椭圆/箭头/画笔/直线/文本）支持拖动栈顶标注。
     *
     * @return 是否支持拖动
     * @author chiangyang
     */
    bool canDragCurrentTool() const;

private:
    AnnotationManager m_annotationManager;  ///< 标注管理器

    // ---------- 交互状态 ----------
    bool m_isAnnotating = false;             ///< 是否正在绘制标注
    bool m_isErasing = false;                ///< 是否正在擦除标注
    bool m_isMosing = false;                 ///< 是否正在绘制马赛克
    bool m_isDraggingAnnotation = false;     ///< 是否正在拖动标注
    QPoint m_annotationDragLastPos;          ///< 拖动标注时上一次鼠标位置
    bool m_isDraggingControlPoint = false;   ///< 是否正在拖拽控制点
    int m_draggedControlPointIndex = -1;     ///< 当前拖拽的控制点索引
    QPoint m_annotationAnchor;               ///< 标注绘制基准点（按下点），用于 Shift/Alt 约束

    // ---------- 工具属性 ----------
    AnnotationType m_currentTool = AnnotationType::Rectangle;  ///< 当前标注工具
    QColor m_currentColor = Qt::red;         ///< 当前画笔颜色
    int m_currentPenWidth = 5;               ///< 当前画笔宽度
    int m_currentFontSize = 16;              ///< 当前字体大小
    int m_currentShapeType = 1;              ///< 当前形状类型（1=矩形, 2=椭圆, 3=三角形）

    Host m_host;  ///< 宿主回调接口
};

#endif // ANNOTATIONINTERACTIONHANDLER_H
