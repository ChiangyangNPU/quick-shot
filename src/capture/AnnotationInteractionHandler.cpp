#include "AnnotationInteractionHandler.h"
#include "Annotation.h"
#include "OverlayTextEdit.h"

/**
 * @brief 判断当前工具是否支持拖动已提交的标注
 *
 * 橡皮擦和马赛克工具不支持拖动（它们是涂抹式工具），
 * 其他工具（矩形/椭圆/箭头/画笔/直线/文本）支持拖动栈顶标注。
 *
 * @return 是否支持拖动
 * @author chiangyang
 */
bool AnnotationInteractionHandler::canDragCurrentTool() const {
    int tid = static_cast<int>(m_currentTool);
    return (tid == ToolIds::RECTANGLE || tid == ToolIds::ELLIPSE ||
            tid == ToolIds::ARROW || tid == ToolIds::PEN ||
            tid == ToolIds::LINE || tid == ToolIds::TEXT);
}

// ============================================================
// 交互入口（鼠标事件转发）
// ============================================================

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
bool AnnotationInteractionHandler::handleMousePress(const QPoint &pos, Qt::KeyboardModifiers modifiers) {
    Q_UNUSED(modifiers);
    int toolId = static_cast<int>(m_currentTool);

    // ---- 工具栏检查（SnipScreen 需要让工具栏处理点击，PinWindow 恒 false）----
    if (m_host.isInToolBar(pos)) {
        return false;
    }

    // ---- 文本编辑框活动时：点击外部完成编辑（最优先，防止工具切换后遗留文本框）----
    OverlayTextEdit *te = m_host.activeTextEdit();
    if (te) {
        QPoint localInEdit = te->mapFromParent(pos);
        if (!te->rect().contains(localInEdit)) {
            // 点击在文本框外部，完成文本编辑
            m_host.finalizeTextEdit();
            m_host.requestUpdate();
            return true;
        }
        // 点击在文本框内部，让 OverlayTextEdit 自己处理
        return false;
    }

    // ---- 橡皮擦工具 ----
    if (toolId == ToolIds::ERASER) {
        if (!m_host.isInSelection(pos)) {
            return false;
        }
        m_isErasing = true;
        m_annotationManager.eraseAt(pos, m_currentPenWidth * 2);
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        return true;
    }

    // ---- 马赛克工具 ----
    if (toolId == ToolIds::MOSAIC) {
        if (!m_host.isInSelection(pos)) {
            return false;
        }
        m_isMosing = true;
        m_annotationManager.mosaicAt(pos, m_currentPenWidth * 2);
        m_host.requestUpdate();
        return true;
    }

    // ---- 控制点命中检测（仅栈顶标注可调节，仅选区内）----
    if (m_host.isInSelection(pos)) {
        int cpIndex = m_annotationManager.hitControlPoint(pos);
        if (cpIndex >= 0) {
            m_annotationManager.beginMove();  // 保存原始状态用于撤销
            m_isDraggingControlPoint = true;
            m_draggedControlPointIndex = cpIndex;
            return true;
        }
    }

    // ---- 文本工具：创建文本框或拖动已提交标注 ----
    if (m_currentTool == AnnotationType::Text) {
        if (m_host.isInSelection(pos)) {
            // 命中栈顶标注 → 拖动（支持移动已提交的文本等标注）
            Annotation *last = m_annotationManager.lastAnnotation();
            if (last && last->hitTest(pos)) {
                m_annotationManager.beginMove();  // 保存原始位置用于撤销
                m_isDraggingAnnotation = true;
                m_annotationDragLastPos = pos;
                return true;
            }
            // 空白处 → 创建新文本框
            m_host.createTextEdit(pos);
        }
        return true;
    }

    // ---- 命中栈顶标注 → 开始拖动 ----
    if (m_host.isInSelection(pos)) {
        Annotation *last = m_annotationManager.lastAnnotation();
        if (last && last->hitTest(pos)) {
            m_annotationManager.beginMove();  // 保存原始位置用于撤销
            m_isDraggingAnnotation = true;
            m_annotationDragLastPos = pos;
            return true;
        }
    }

    // ---- 创建新标注 ----
    if (m_host.isInSelection(pos)) {
        m_isAnnotating = true;
        createAnnotation(pos);
        m_host.requestUpdate();
        return true;
    }

    return false;
}

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
bool AnnotationInteractionHandler::handleMouseMove(const QPoint &pos, Qt::KeyboardModifiers modifiers) {
    // ---- 控制点拖拽（优先于标注拖动和绘制）----
    if (m_isDraggingControlPoint) {
        QPoint clampedPos = m_host.clampPos(pos);
        m_annotationManager.moveLastControlPoint(m_draggedControlPointIndex, clampedPos);
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // ---- 拖动标注（优先于绘制）----
    if (m_isDraggingAnnotation) {
        QPoint clampedPos = m_host.clampPos(pos);
        QPoint delta = clampedPos - m_annotationDragLastPos;
        m_annotationManager.translateLast(delta);
        m_annotationDragLastPos = clampedPos;
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步（拖动过程也要体现在视频中）
        return true;
    }

    // ---- 擦除中 ----
    if (m_isErasing) {
        QPoint clampedPos = m_host.clampPos(pos);
        m_annotationManager.eraseAt(clampedPos, m_currentPenWidth * 2);
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // ---- 马赛克涂抹中 ----
    if (m_isMosing) {
        QPoint clampedPos = m_host.clampPos(pos);
        m_annotationManager.mosaicAt(clampedPos, m_currentPenWidth * 2);
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // ---- 绘制中 ----
    if (m_isAnnotating) {
        QPoint clampedPos = m_host.clampPos(pos);

        // 画笔工具特殊处理：连续添加路径点
        if (m_currentTool == AnnotationType::Pen) {
            Annotation *lastAnnotation = m_annotationManager.lastAnnotation();
            if (lastAnnotation && lastAnnotation->type() == AnnotationType::Pen) {
                PenAnnotation *penAnnotation = static_cast<PenAnnotation *>(lastAnnotation);
                penAnnotation->addPoint(clampedPos);
            }
        } else {
            // 其他工具：应用 Shift/Alt 修饰键约束后更新起止点
            Annotation *lastAnnotation = m_annotationManager.lastAnnotation();
            if (lastAnnotation) {
                // 以按下点（m_annotationAnchor）为基准做约束，避免 Alt 中心化逐帧漂移
                QPoint startPt = m_annotationAnchor;
                QPoint endPt = clampedPos;
                AnnotationManager::applyModifierConstraints(startPt, endPt,
                                                             modifiers, m_currentTool,
                                                             m_host.selectionRect());
                // Alt 中心化会修改 start，需同步写回
                lastAnnotation->setStart(startPt);
                m_annotationManager.updateLast(endPt);
            } else {
                m_annotationManager.updateLast(clampedPos);
            }
        }

        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步（绘制过程也要体现在视频中）
        return true;
    }

    // ---- 悬停光标反馈：命中控制点或栈顶标注显示对应光标 ----
    if (m_host.isInSelection(pos)) {
        if (canDragCurrentTool()) {
            // 优先检测控制点：返回对应方向光标
            int cpIdx = m_annotationManager.hitControlPoint(pos);
            if (cpIdx >= 0) {
                m_host.setCursor(m_annotationManager.controlPointCursorAt(pos));
                return true;
            }
            // 再检测标注本体
            Annotation *last = m_annotationManager.lastAnnotation();
            if (last && last->hitTest(pos)) {
                m_host.setCursor(Qt::SizeAllCursor);
                return true;  // 拦截，避免 Selector 覆盖光标
            }
        }
        m_host.setCursor(Qt::CrossCursor);
    }

    return false;
}

/**
 * @brief 处理鼠标释放事件
 *
 * 结束当前进行的操作（控制点拖拽/标注拖动/擦除/马赛克/绘制），
 * 提交操作记录用于撤销/重做。
 *
 * 绘制结束时额外检测单击未拖动产生的空标注（几何标注 start==end，
 * 画笔标注路径点数 ≤ 1），若为空则 rollback() 丢弃，避免在起始点
 * 残留控制点；只有完整拖动出痕迹后才 commit() 提交并显示控制点。
 *
 * @return true 表示事件已处理
 * @author chiangyang
 */
bool AnnotationInteractionHandler::handleMouseRelease() {
    // 控制点拖拽结束
    if (m_isDraggingControlPoint) {
        m_isDraggingControlPoint = false;
        m_draggedControlPointIndex = -1;
        m_annotationManager.endMove();  // 保存修改记录用于撤销
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // 拖动标注结束
    if (m_isDraggingAnnotation) {
        m_isDraggingAnnotation = false;
        m_annotationManager.endMove();  // 保存移动记录用于撤销
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // 擦除结束
    if (m_isErasing) {
        m_isErasing = false;
        m_annotationManager.endEraserStroke();
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // 马赛克涂抹结束
    if (m_isMosing) {
        m_isMosing = false;
        m_annotationManager.endMosaicStroke();
        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    // 绘制结束
    if (m_isAnnotating) {
        m_isAnnotating = false;

        // 检测单击未拖动产生的空标注，丢弃避免在起始点残留控制点
        // - 几何标注（矩形/椭圆/三角形/箭头/直线）：start == end 表示零尺寸
        // - 画笔标注：路径点为空或仅 1 个点（等同于零长度线段，不可见）
        Annotation *last = m_annotationManager.lastAnnotation();
        bool isEmpty = false;
        if (last) {
            if (last->type() == AnnotationType::Pen) {
                const auto &pts = static_cast<PenAnnotation *>(last)->points();
                isEmpty = (pts.size() <= 1);
            } else {
                isEmpty = (last->start() == last->end());
            }
        }

        if (isEmpty) {
            m_annotationManager.rollback();  // 移除空标注，不提交
        } else {
            m_annotationManager.commit();  // 提交标注，清空 redo 栈
        }

        m_host.updateToolBarState(m_annotationManager.canUndo(), m_annotationManager.canRedo());
        m_host.requestUpdate();
        m_host.syncOverlay();  // 录屏模式同步
        return true;
    }

    return false;
}

// ============================================================
// 绘制
// ============================================================

/**
 * @brief 绘制标注和控制点（非马赛克分支）
 *
 * 在已画好背景的 painter 上直接绘制标注和控制点。
 *
 * 控制点显示策略：
 * 1. 仅在无任何交互进行时绘制（isInteractionActive() 为 false）。创建标注、拖动标注、
 *    拖拽控制点、擦除/马赛克涂抹等交互过程中均隐藏控制点，等鼠标释放（交互结束）后才显示，
 *    避免控制点干扰绘制视觉。
 * 2. 仅在当前工具支持调节时绘制（canDragCurrentTool() 为 true）。橡皮擦和马赛克为涂抹式
 *    工具，切换到这两个工具后栈顶标注不再可调节，控制点应当隐藏。
 *
 * @param painter 目标画笔
 * @param offset 坐标偏移（SnipScreen 为 globalOffset，PinWindow 为 QPoint(0,0)）
 * @author chiangyang
 */
void AnnotationInteractionHandler::drawAnnotations(QPainter &painter, const QPoint &offset) {
    if (!m_annotationManager.hasAnnotations()) {
        return;
    }
    painter.save();
    painter.translate(offset);
    m_annotationManager.draw(painter);
    // 仅在无交互进行且当前工具支持调节时绘制控制点
    if (!isInteractionActive() && canDragCurrentTool()) {
        m_annotationManager.drawControlPoints(painter);
    }
    painter.restore();
}

/**
 * @brief 绘制标注、马赛克和控制点（马赛克分支）
 *
 * 在已画好背景的 canvas 上依次绘制：标注 → 马赛克（像素化）→ 控制点（不被像素化）。
 * 控制点在马赛克之后绘制，确保控制点清晰可见。
 *
 * 控制点显示策略：
 * 1. 仅在无任何交互进行时绘制（isInteractionActive() 为 false）。创建标注、拖动标注、
 *    拖拽控制点、擦除/马赛克涂抹等交互过程中均隐藏控制点，等鼠标释放（交互结束）后才显示，
 *    避免控制点干扰绘制视觉。
 * 2. 仅在当前工具支持调节时绘制（canDragCurrentTool() 为 true）。橡皮擦和马赛克为涂抹式
 *    工具，切换到这两个工具后栈顶标注不再可调节，控制点应当隐藏。
 *
 * @param canvasPainter canvas 的画笔
 * @param canvas 已画好背景的离屏画布（马赛克以此为背景采样）
 * @param blockSize 马赛克块大小（下采样缩放因子）
 * @param offset 坐标偏移
 * @author chiangyang
 */
void AnnotationInteractionHandler::drawWithMosaic(QPainter &canvasPainter, QPixmap &canvas,
                                                   int blockSize, const QPoint &offset) {
    // 1. 绘制标注（转换为本地坐标）
    if (m_annotationManager.hasAnnotations()) {
        canvasPainter.save();
        canvasPainter.translate(offset);
        m_annotationManager.draw(canvasPainter);
        canvasPainter.restore();
    }

    // 2. 在合成图上绘制马赛克（同时像素化背景和标注）
    m_annotationManager.drawMosaic(canvasPainter, canvas, blockSize, offset);

    // 3. 在马赛克之后绘制控制点（确保控制点不被像素化）
    //    仅在无交互进行且当前工具支持调节时绘制
    if (!isInteractionActive() && canDragCurrentTool()) {
        canvasPainter.save();
        canvasPainter.translate(offset);
        m_annotationManager.drawControlPoints(canvasPainter);
        canvasPainter.restore();
    }
}

// ============================================================
// 标注创建
// ============================================================

/**
 * @brief 根据当前工具类型创建标注对象
 *
 * 根据 m_currentTool 和 m_currentShapeType 创建对应的标注对象并添加到管理器。
 * 文本工具和马赛克工具不由此方法创建（分别由 createTextEdit 和 mosaicAt 处理）。
 *
 * @param start 标注起始点
 * @author chiangyang
 */
void AnnotationInteractionHandler::createAnnotation(const QPoint &start) {
    // 保存绘制基准点（按下点），供 mouseMove 时 Shift/Alt 约束使用
    // Alt 中心化会修改 annotation 的 start，必须用按下点作为基准才能避免漂移
    m_annotationAnchor = start;

    std::unique_ptr<Annotation> annotation;

    if (m_currentTool == AnnotationType::Rectangle) {
        switch (m_currentShapeType) {
            case 1:
                annotation = std::make_unique<RectAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case 2:
                annotation = std::make_unique<EllipseAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case 3:
                annotation = std::make_unique<TriangleAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            default:
                annotation = std::make_unique<RectAnnotation>(start, m_currentColor, m_currentPenWidth);
        }
    } else {
        switch (m_currentTool) {
            case AnnotationType::Ellipse:
                annotation = std::make_unique<EllipseAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case AnnotationType::Arrow:
                annotation = std::make_unique<ArrowAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case AnnotationType::Pen:
                annotation = std::make_unique<PenAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case AnnotationType::Line:
                annotation = std::make_unique<LineAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
            case AnnotationType::Text:
                return;  // 文本工具由 createTextEdit 处理
            case AnnotationType::Mosaic:
                return;  // 马赛克工具由 mosaicAt 处理
            default:
                annotation = std::make_unique<RectAnnotation>(start, m_currentColor, m_currentPenWidth);
                break;
        }
    }

    m_annotationManager.add(std::move(annotation));
}

// ============================================================
// 退出与清理
// ============================================================

/**
 * @brief 退出标注模式，重置所有交互状态
 *
 * 重置绘制/擦除/拖拽等状态标志。注意：不清理标注数据（标注保留供后续查看），
 * 不清理文本编辑框和工具栏（由宿主负责）。
 *
 * @author chiangyang
 */
void AnnotationInteractionHandler::exitAnnotation() {
    m_isAnnotating = false;
    m_isMosing = false;
    m_isErasing = false;
    m_isDraggingAnnotation = false;
    m_isDraggingControlPoint = false;
    m_draggedControlPointIndex = -1;
}
