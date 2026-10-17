#ifndef ANNOTATIONMANAGER_H
#define ANNOTATIONMANAGER_H

#include "Annotation.h"
#include <QPainter>
#include <memory>
#include <vector>
#include <cstdint>
#include <algorithm>

/**
 * @brief 标注管理器
 *
 * 管理所有标注对象，提供添加、撤销、重做、清除等操作。
 * 使用两个栈（undo栈和redo栈）实现撤销/重做功能。
 *
 * 设计模式：命令模式（通过栈存储操作历史）
 *
 * 橡皮擦和马赛克工具的笔迹按"一次完整拖拽"分组存储（StrokeGroup），
 * 每组分配一个全局递增的操作序号（seq），与标注的序号统一比较，
 * 使 undo 撤销最后一步操作（seq 最大），redo 重做最早被撤销的操作（seq 最小）。
 *
 * 使用方法：
 * 1. 用户开始标注时，调用 add() 添加新标注
 * 2. 用户拖拽时，调用 updateLast() 更新最后一个标注的结束点
 * 3. 用户完成标注后，调用 commit() 提交（清空redo栈）
 * 4. 用户取消标注时，调用 rollback() 回滚（从undo栈移除）
 * 5. 调用 undo()/redo() 进行撤销/重做
 * 6. 调用 draw() 绘制所有标注
 * 7. 橡皮擦：拖拽中调用 eraseAt()，释放时调用 endEraserStroke() 提交
 * 8. 马赛克：拖拽中调用 mosaicAt()，释放时调用 endMosaicStroke() 提交
 * @author chiangyang
 */
class AnnotationManager {
public:
    /**
     * @brief 马赛克默认块大小（下采样缩放因子）
     *
     * 值越大，马赛克方块越大、模糊/遮挡程度越强。
     *
     * 全局统一常量：一处修改，所有马赛克场景（截屏/PinWindow/录屏 overlay）同步生效。
     *   - 3  ：颗粒最细，轻度遮挡
     *   - 5  ：中等
     *   - 7  ：默认值，日常打码更合适（辨识度更低）
     *   - 10+：非常模糊，强遮挡
     * @author chiangyang
     */
    static constexpr int kDefaultMosaicBlockSize = 7;

    /**
     * @brief 默认构造函数
     *
     * 初始化空的撤销/重做栈和橡皮擦/马赛克笔迹列表。
     * @author chiangyang
     */
    AnnotationManager() = default;

    // ---------- 内容版本计数（供绘制层缓存失效判断） ----------
    /**
     * @brief 基础内容版本（标注 + 橡皮擦，影响"背景+标注"合成层）
     *
     * 任何改变可见基础内容的操作（增删标注、移动、控制点、擦除）递增。
     * @author chiangyang
     */
    quint64 contentVersion() const { return m_contentVersion; }

    /**
     * @brief 马赛克笔迹版本（影响马赛克区域与全局像素化图层）
     * @author chiangyang
     */
    quint64 mosaicVersion() const { return m_mosaicVersion; }

    /**
     * @brief 添加新标注
     *
     * 将标注添加到undo栈。如果用户取消绘制，可通过 rollback() 移除。
     *
     * @param annotation 标注对象（所有权转移）
     * @author chiangyang
     */
    void add(std::unique_ptr<Annotation> annotation);

    /**
     * @brief 更新最后一个标注的结束点
     *
     * 用户拖拽时调用，实时更新标注大小。
     *
     * @param end 结束点坐标
     * @author chiangyang
     */
    void updateLast(const QPoint &end);

    /**
     * @brief 提交标注
     *
     * 标注绘制完成后调用，清空redo栈（新的操作会丢弃重做历史）。
     * @author chiangyang
     */
    void commit();

    /**
     * @brief 回滚最后一个标注
     *
     * 用户取消绘制时调用，移除最后一个未完成的标注。
     * @author chiangyang
     */
    void rollback();

    /**
     * @brief 撤销最后一个操作
     *
     * 优先级：撤销清除操作 > 撤销移动操作 > 撤销最后一步操作（标注/橡皮擦组/马赛克组）。
     * 撤销清除时，将清除前的完整状态恢复；同时保存清除后的空状态到重做快照，使重做可再次清除。
     * 撤销最后一步操作时，比较 undo 栈顶标注、橡皮擦组、马赛克组的序号，撤销序号最大的。
     * @author chiangyang
     */
    void undo();

    /**
     * @brief 重做最后一个操作
     *
     * 优先级：重做清除操作 > 重做移动操作 > 重做最早被撤销的操作。
     * 重做清除时，再次应用清除并将当前状态保存到撤销快照。
     * 重做单个操作时，比较 redo 栈顶标注、橡皮擦组、马赛克组的序号，重做序号最小的（最早被撤销的）。
     * @author chiangyang
     */
    void redo();

    /**
     * @brief 清除所有标注、橡皮擦和马赛克笔迹（支持撤销）
     *
     * 清除前保存当前完整状态到撤销快照，使后续调用 undo() 可恢复被清除的内容。
     * 新的清除操作会使重做快照失效（与 commit() 语义一致）。
     * @author chiangyang
     */
    void clear();

    /**
     * @brief 缩放所有标注及笔迹坐标（相对窗口原点 0,0）
     *
     * 用于窗口尺寸变化时（如 PinWindow 滚轮缩放）使标注、橡皮擦/马赛克笔迹随图像同步缩放。
     * 同时缩放 undo/redo 两个栈中的标注，保证撤销/重做后位置一致。
     * 全局缩放不属于单标注移动操作，会使移动撤销/重做记录失效。
     * 清除快照中的内容也会同步缩放，保证撤销清除后位置正确。
     * @param sx 水平缩放因子
     * @param sy 垂直缩放因子
     * @author chiangyang
     */
    void scaleAll(double sx, double sy);

    /**
     * @brief 记录橡皮擦笔迹（拖拽中）
     *
     * 在指定位置添加一个擦除区域（小矩形）到活跃缓冲。
     * 橡皮擦拖拽时连续调用，形成擦除路径。
     * 绘制标注时会裁剪掉这些区域，实现局部擦除效果。
     * 拖拽结束时需调用 endEraserStroke() 提交笔迹到撤销历史。
     *
     * @param pos 橡皮擦中心坐标
     * @param size 橡皮擦大小（边长的一半）
     * @author chiangyang
     */
    void eraseAt(const QPoint &pos, int size = 10);

    /**
     * @brief 提交橡皮擦笔迹到撤销历史
     *
     * 鼠标释放时调用，将活跃缓冲中的笔迹作为一组提交到 m_eraserGroups，
     * 分配操作序号，并清空 redo 历史（新操作丢弃重做）。
     * 如果活跃缓冲为空，不做任何操作。
     * @author chiangyang
     */
    void endEraserStroke();

    /**
     * @brief 构建橡皮擦裁剪区域
     *
     * 将所有橡皮擦笔迹（含活跃缓冲）合并为一个 QRegion。
     *
     * @return 橡皮擦覆盖的区域
     * @author chiangyang
     */
    QRegion eraserRegion() const;

    /**
     * @brief 记录马赛克笔迹（拖拽中）
     *
     * 在指定位置添加一个马赛克区域（小矩形）到活跃缓冲。
     * 马赛克工具拖拽时连续调用，形成马赛克路径。
     * 拖拽结束时需调用 endMosaicStroke() 提交笔迹到撤销历史。
     *
     * @param pos 马赛克中心坐标
     * @param size 马赛克块大小（边长的一半）
     * @author chiangyang
     */
    void mosaicAt(const QPoint &pos, int size = 10);

    /**
     * @brief 提交马赛克笔迹到撤销历史
     *
     * 鼠标释放时调用，将活跃缓冲中的笔迹作为一组提交到 m_mosaicGroups，
     * 分配操作序号，并清空 redo 历史（新操作丢弃重做）。
     * 如果活跃缓冲为空，不做任何操作。
     * @author chiangyang
     */
    void endMosaicStroke();

    /**
     * @brief 检查是否有马赛克笔迹
     * @return true 如果有已提交的马赛克组或活跃缓冲中的马赛克笔迹
     * @author chiangyang
     */
    bool hasMosaicStrokes() const {
        return !m_mosaicGroups.empty() || !m_activeMosaicStroke.empty();
    }

    /**
     * @brief 在目标画布上绘制马赛克效果
     *
     * 对马赛克区域内的背景图像进行像素化处理。
     * 将区域缩小后放大，产生马赛克块效果。
     *
     * @param painter 目标画笔
     * @param background 原始背景图（用于采样像素）
     * @param blockSize 马赛克块大小（像素）
     * @author chiangyang
     */
    void drawMosaic(QPainter &painter, const QPixmap &background,
                    int blockSize = kDefaultMosaicBlockSize, const QPoint &offset = QPoint()) const {
        if ((m_mosaicGroups.empty() && m_activeMosaicStroke.empty()) || background.isNull())
            return;

        // 绘制已提交的马赛克组
        for (const auto &group : m_mosaicGroups) {
            drawMosaicRects(painter, background, group.rects, blockSize, offset);
        }
        // 绘制活跃缓冲中的马赛克（正在拖拽）
        if (!m_activeMosaicStroke.empty()) {
            drawMosaicRects(painter, background, m_activeMosaicStroke, blockSize, offset);
        }
    }

    /**
     * @brief 绘制所有标注
     *
     * 按顺序绘制undo栈中的标注。
     * 如果存在橡皮擦笔迹，会设置裁剪区域排除擦除部分，
     * 实现局部擦除效果（只擦除橡皮擦经过的区域）。
     *
     * @param painter 画笔对象
     * @author chiangyang
     */
    void draw(QPainter &painter) const;

    /**
     * @brief 检查是否有标注
     * @return true 如果有标注
     * @author chiangyang
     */
    bool hasAnnotations() const {
        return !m_undoStack.empty();
    }

    /**
     * @brief 获取最后一个标注的指针（不转移所有权）
     * @return 最后一个标注的指针，如果栈为空返回 nullptr
     * @author chiangyang
     */
    Annotation * lastAnnotation() const;

    // ============================================================
    // 标注控制点调节（仅支持栈顶标注）
    // ============================================================

    /**
     * @brief 为栈顶标注绘制控制点
     *
     * 在调用方当前坐标系下绘制（与 draw() 同坐标系）。
     * 控制点样式：白色填充 + 灰色边框的小圆圈（半径 5px）。
     * 启用抗锯齿（Antialiasing）使圆圈边缘平滑无锯齿。
     * 仅当栈顶标注支持控制点（hasControlPoints() 为 true）时绘制。
     * @param painter 画笔对象
     * @author chiangyang
     */
    void drawControlPoints(QPainter &painter) const;

    /**
     * @brief 命中栈顶标注的控制点
     * @param pos 测试点（标注坐标系，与 drawControlPoints 一致）
     * @param tolerance 命中容差（默认 6px，略大于绘制半径 5 便于点击）
     * @return 命中的控制点索引（>=0），未命中返回 -1
     * @author chiangyang
     */
    int hitControlPoint(const QPoint &pos, int tolerance = 6) const;

    /**
     * @brief 获取指定位置命中的控制点对应的光标样式
     *
     * 供悬停光标反馈使用：命中控制点返回对应方向光标，未命中返回 ArrowCursor。
     * @param pos 测试点（标注坐标系）
     * @return 光标样式
     * @author chiangyang
     */
    Qt::CursorShape controlPointCursorAt(const QPoint &pos) const;

    /**
     * @brief 拖拽栈顶标注的控制点
     * @param index 控制点索引
     * @param pos  新位置（标注坐标系）
     * @author chiangyang
     */
    void moveLastControlPoint(int index, const QPoint &pos);

    // ============================================================
    // 标注拖动移动（仅支持栈顶标注）
    // ============================================================

    /**
     * @brief 开始拖动栈顶标注
     *
     * 保存栈顶标注的原始位置，用于拖动结束后支持撤销。
     * 在鼠标按下并命中标注时调用。
     * @author chiangyang
     */
    void beginMove();

    /**
     * @brief 平移栈顶标注
     *
     * 拖动过程中调用，实时平移标注。
     * @param offset 平移偏移量
     * @author chiangyang
     */
    void translateLast(const QPoint &offset);

    /**
     * @brief 结束拖动
     *
     * 拖动结束时调用。若标注位置发生变化，标记移动记录有效，
     * 并清空重做状态（新操作使重做历史失效）。
     * @author chiangyang
     */
    void endMove();

    /**
     * @brief 检查是否有可撤销的移动操作
     * @return true 如果有未撤销的移动
     * @author chiangyang
     */
    bool hasUndoMove() const { return m_undoMoveState.valid; }

    /**
     * @brief 检查是否有可重做的移动操作
     * @return true 如果有可重做的移动
     * @author chiangyang
     */
    bool hasRedoMove() const { return m_redoMoveState.valid; }

    /**
     * @brief 获取马赛克笔迹列表（扁平化）
     *
     * 将所有马赛克组中的矩形扁平化为一个列表，便于外部遍历。
     * 包含活跃缓冲中的笔迹。
     * @return 马赛克笔迹矩形列表
     * @author chiangyang
     */
    std::vector<QRect> mosaicStrokes() const;

    /**
     * @brief 检查是否可以撤销
     * @return true 如果可以撤销（清除撤销、移动撤销、标注/橡皮擦/马赛克撤销）
     * @author chiangyang
     */
    bool canUndo() const;

    /**
     * @brief 检查是否可以重做
     * @return true 如果可以重做（清除重做、移动重做、标注/橡皮擦/马赛克重做）
     * @author chiangyang
     */
    bool canRedo() const;

    /**
     * @brief 应用 Shift/Alt 修饰键对标注几何的约束
     *
     * 仅对 Rectangle/Ellipse/Arrow/Line 生效，其他工具直接返回。
     * - Shift 等比：以 start 为基，dx/dy 取大者为主轴，另一轴拉到等长（保持符号）；
     *   Line 额外吸附到 0/45/90/135°。
     * - Alt 中心：按下点 start 变为对称中心，标注向两侧延伸 offset。
     * - Shift+Alt：先 Shift 等比（基于原 start）→ 再 Alt 中心化（用 Shift 后的 end）。
     *
     * 越界保护：所有约束均在 selection 选区内求解，保证变换后的 start/end 不会超出选区：
     * - Shift 等比：按各方向允许的最大比例 scale 同步收缩 dx/dy，保持等比；
     * - Line 45° 吸附：按吸附方向上的最大长度收缩 len；
     * - Alt 中心：对 offset 做双向 clamp（|offset.x|≤maxX, |offset.y|≤maxY），保持对称性。
     *
     * @param start 起始点（入参/出参，Alt 中心化时会被修改为对称中心的对侧）
     * @param end   结束点（入参为鼠标当前位置，出参为约束后的结束点）
     * @param mods  当前键盘修饰键
     * @param tool  当前标注工具类型
     * @param selection 选区矩形（含边界），用于约束变换后不越界
     * @author chiangyang
     */
    static void applyModifierConstraints(QPoint &start, QPoint &end,
                                         Qt::KeyboardModifiers mods, AnnotationType tool,
                                         const QRect &selection) {
        // 仅对矩形/椭圆/箭头/直线生效，画笔/文本/马赛克不约束
        if (tool != AnnotationType::Rectangle && tool != AnnotationType::Ellipse &&
            tool != AnnotationType::Arrow && tool != AnnotationType::Line) {
            return;
        }

        const QPoint current = end;  // 原始鼠标位置（已 clamp 到选区）
        const bool shift = mods & Qt::ShiftModifier;
        const bool alt = mods & Qt::AltModifier;
        QPoint finalEnd = current;

        // 选区边界（含），用于约束变换后不越界
        const int selLeft = selection.left();
        const int selRight = selection.right();
        const int selTop = selection.top();
        const int selBottom = selection.bottom();

        // Shift 等比约束（基于原始 start）
        if (shift) {
            int dx = current.x() - start.x();
            int dy = current.y() - start.y();
            if (tool == AnnotationType::Line) {
                // 直线：吸附到 0/45/90/135° 方向
                const double PI = 3.14159265358979323846;
                double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 180.0 / PI;
                double snapped = std::round(angle / 45.0) * 45.0;
                double len = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
                double rad = snapped * PI / 180.0;
                double cosA = std::cos(rad);
                double sinA = std::sin(rad);
                // 收缩 len 使终点不超出选区：沿吸附方向取各轴允许的最大长度
                double maxLen = len;
                if (std::abs(cosA) > 1e-9) {
                    double limitX = (cosA > 0)
                        ? (selRight - start.x()) / cosA
                        : (selLeft - start.x()) / cosA;
                    if (limitX < maxLen) maxLen = limitX;
                }
                if (std::abs(sinA) > 1e-9) {
                    double limitY = (sinA > 0)
                        ? (selBottom - start.y()) / sinA
                        : (selTop - start.y()) / sinA;
                    if (limitY < maxLen) maxLen = limitY;
                }
                if (maxLen < 0) maxLen = 0;
                finalEnd = QPoint(start.x() + qRound(cosA * maxLen),
                                  start.y() + qRound(sinA * maxLen));
            } else {
                // 矩形/椭圆/箭头：等比（正方形/圆），以较大轴为主轴
                int adx = std::abs(dx), ady = std::abs(dy);
                if (adx >= ady) {
                    dy = (dy >= 0 ? 1 : -1) * adx;
                } else {
                    dx = (dx >= 0 ? 1 : -1) * ady;
                }
                // 收缩等比偏移使终点不超出选区：dx/dy 同比例缩放，保持等比
                double scale = 1.0;
                if (dx > 0) {
                    double limit = (selRight - start.x()) / static_cast<double>(dx);
                    if (limit < scale) scale = limit;
                } else if (dx < 0) {
                    double limit = (selLeft - start.x()) / static_cast<double>(dx);
                    if (limit < scale) scale = limit;
                }
                if (dy > 0) {
                    double limit = (selBottom - start.y()) / static_cast<double>(dy);
                    if (limit < scale) scale = limit;
                } else if (dy < 0) {
                    double limit = (selTop - start.y()) / static_cast<double>(dy);
                    if (limit < scale) scale = limit;
                }
                if (scale < 0) scale = 0;
                finalEnd = QPoint(start.x() + qRound(dx * scale),
                                  start.y() + qRound(dy * scale));
            }
        }

        // Alt 中心绘制：按下点 start 变为对称中心，标注向两侧延伸 offset
        // 收缩 offset 使 start ± offset 均落在选区内，保持对称性
        if (alt) {
            const QPoint originalStart = start;
            QPoint offset = finalEnd - originalStart;
            // originalStart 两侧可用范围（保证 originalStart ± offset 都在选区内）
            int maxX = std::min(selRight - originalStart.x(), originalStart.x() - selLeft);
            int maxY = std::min(selBottom - originalStart.y(), originalStart.y() - selTop);
            if (maxX < 0) maxX = 0;
            if (maxY < 0) maxY = 0;
            if (offset.x() > maxX) offset.setX(maxX);
            if (offset.x() < -maxX) offset.setX(-maxX);
            if (offset.y() > maxY) offset.setY(maxY);
            if (offset.y() < -maxY) offset.setY(-maxY);
            start = originalStart - offset;     // 对称中心的对侧
            finalEnd = originalStart + offset;  // 对称中心的同侧
        }

        // 兜底：浮点取整误差可能导致 1px 越界，强制 clamp 终点
        if (finalEnd.x() < selLeft) finalEnd.setX(selLeft);
        if (finalEnd.x() > selRight) finalEnd.setX(selRight);
        if (finalEnd.y() < selTop) finalEnd.setY(selTop);
        if (finalEnd.y() > selBottom) finalEnd.setY(selBottom);

        end = finalEnd;
    }

private:
    /**
     * @brief 橡皮擦笔迹组（一次完整拖拽）
     * @author chiangyang
     */
    struct EraserStrokeGroup {
        std::vector<QRect> rects;  ///< 笔迹矩形列表
        uint64_t seq = 0;          ///< 操作序号（用于跨类型 undo/redo 排序）
    };

    /**
     * @brief 马赛克笔迹组（一次完整拖拽）
     * @author chiangyang
     */
    struct MosaicStrokeGroup {
        std::vector<QRect> rects;  ///< 笔迹矩形列表
        uint64_t seq = 0;          ///< 操作序号（用于跨类型 undo/redo 排序）
    };

    /**
     * @brief 移动记录结构
     *
     * 保存标注的完整位置状态（start/end/points），用于拖动撤销/重做。
     * @author chiangyang
     */
    struct MoveRecord {
        bool valid = false;          ///< 记录是否有效
        QPoint start;                 ///< 起始点
        QPoint end;                   ///< 结束点
        QVector<QPoint> points;       ///< 路径点列表（仅 PenAnnotation 使用）
        QVector<QPoint> vertices;     ///< 三角形独立顶点（仅 TriangleAnnotation 使用）
    };

    /**
     * @brief 清除快照结构
     *
     * 保存清除前的完整状态（标注栈、橡皮擦/马赛克笔迹组、操作序号），用于支持撤销清除操作。
     * @author chiangyang
     */
    struct ClearSnapshot {
        bool valid = false;                                        ///< 快照是否有效
        std::vector<std::unique_ptr<Annotation>> undoStack;        ///< 清除前的撤销栈
        std::vector<std::unique_ptr<Annotation>> redoStack;        ///< 清除前的重做栈
        std::vector<uint64_t> annotationSeqs;                      ///< 清除前的标注序号
        std::vector<uint64_t> redoAnnotationSeqs;                  ///< 清除前的重做标注序号
        std::vector<EraserStrokeGroup> eraserGroups;               ///< 清除前的橡皮擦笔迹组
        std::vector<EraserStrokeGroup> redoEraserGroups;           ///< 清除前的重做橡皮擦笔迹组
        std::vector<MosaicStrokeGroup> mosaicGroups;               ///< 清除前的马赛克笔迹组
        std::vector<MosaicStrokeGroup> redoMosaicGroups;           ///< 清除前的重做马赛克笔迹组
        MoveRecord undoMoveState;                                  ///< 清除前的移动撤销状态
        MoveRecord redoMoveState;                                  ///< 清除前的移动重做状态
        uint64_t nextOpSeq = 0;                                    ///< 清除前的全局序号
    };

    /**
     * @brief 保存标注当前状态为移动记录
     * @param ann 标注指针
     * @return 移动记录
     * @author chiangyang
     */
    MoveRecord saveAnnotationState(const Annotation *ann) const;

    /**
     * @brief 从移动记录恢复标注状态
     * @param ann 标注指针
     * @param record 移动记录
     * @author chiangyang
     */
    void restoreAnnotationState(Annotation *ann, const MoveRecord &record) const;

    /**
     * @brief 清空所有 redo 历史（标注栈、橡皮擦组、马赛克组）
     * @author chiangyang
     */
    void clearRedo();

    /**
     * @brief 缩放橡皮擦笔迹组中的所有矩形
     * @author chiangyang
     */
    static void scaleEraserGroups(std::vector<EraserStrokeGroup> &groups, double sx, double sy);

    /**
     * @brief 缩放马赛克笔迹组中的所有矩形
     * @author chiangyang
     */
    static void scaleMosaicGroups(std::vector<MosaicStrokeGroup> &groups, double sx, double sy);

    /**
     * @brief 对一组马赛克矩形执行像素化绘制
     *
     * 改进算法：
     * 1. 先将完整的背景图整体做一次马赛克预处理（缩放到 1/scale 再放大回原尺寸），
     *    得到与背景尺寸完全对齐的全局马赛克纹理，保证所有马赛克块全局连续。
     * 2. 将所有笔迹小矩形合并为一个 QRegion（并集），作为 painter 的裁剪区域。
     * 3. 一次性绘制整张预处理马赛克图，由于 clip 限制，仅笔迹覆盖区域被绘出，
     *    从而消除了逐块缩放导致的接缝和错位问题。
     *
     * @param painter    目标画笔
     * @param background 完整背景图（已包含原图与标注合成，用于整体预处理）
     * @param rects      马赛克笔迹矩形列表
     * @param blockSize  马赛克块大小参数（实际使用 scale = blockSize，默认 5 对应 1/5 缩放下采样）
     * @param offset     笔迹坐标到 background 坐标的平移偏移（如 SnipScreen 的 globalOffset）
     * @author chiangyang
     */
    static void drawMosaicRects(QPainter &painter, const QPixmap &background,
                                const std::vector<QRect> &rects, int blockSize,
                                const QPoint &offset) {
        if (background.isNull() || rects.empty())
            return;

        const int scale = qMax(2, blockSize); // 至少 2x，避免 1x 无效果

        // ------------------------------------------------------------
        // 第 1 步：合并所有笔迹矩形为一个 QRegion（并集），减少重复绘制
        // ------------------------------------------------------------
        QRegion mosaicRegion;
        for (const QRect &stroke : rects) {
            QRect adjusted = offset.isNull() ? stroke : stroke.translated(offset);
            QRect clipped = adjusted.intersected(background.rect());
            if (!clipped.isEmpty())
                mosaicRegion += clipped;
        }
        if (mosaicRegion.isEmpty())
            return;

        // ------------------------------------------------------------
        // 第 2 步：对完整背景一次性做全局马赛克预处理
        //   先缩小（Smooth 让块颜色是邻域平均，比 Fast 自然）后放大（Fast 保留硬方块感）
        // ------------------------------------------------------------
        const QSize bgSize = background.size();
        const QSize smallSize(qMax(1, bgSize.width() / scale),
                              qMax(1, bgSize.height() / scale));
        QPixmap small = background.scaled(smallSize,
                                          Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
        QPixmap mosaicizedFull = small.scaled(bgSize,
                                              Qt::IgnoreAspectRatio,
                                              Qt::FastTransformation);

        // ------------------------------------------------------------
        // 第 3 步：设置 clip，一次性把全局马赛克图绘入（仅笔迹区域生效）
        // ------------------------------------------------------------
        painter.save();
        painter.setClipRegion(mosaicRegion);
        painter.drawPixmap(0, 0, mosaicizedFull);
        painter.restore();
    }

    /**
     * @brief 撤销最后一步操作（undo 栈顶中序号最大的）
     *
     * 比较 undo 栈顶标注、橡皮擦组栈顶、马赛克组栈顶的序号，
     * 将序号最大的操作移到对应的 redo 栈。
     * @author chiangyang
     */
    void undoLastOperation();

    /**
     * @brief 重做最早被撤销的操作（redo 栈顶中序号最小的）
     *
     * 比较 redo 栈顶标注、橡皮擦组栈顶、马赛克组栈顶的序号，
     * 将序号最小的操作移回对应的 undo 栈。
     * @author chiangyang
     */
    void redoEarliestOperation();

    std::vector<std::unique_ptr<Annotation>> m_undoStack;  ///< 撤销栈
    std::vector<std::unique_ptr<Annotation>> m_redoStack;  ///< 重做栈
    std::vector<uint64_t> m_annotationSeqs;                ///< 标注操作序号（与 m_undoStack 平行）
    std::vector<uint64_t> m_redoAnnotationSeqs;            ///< 重做标注序号（与 m_redoStack 平行）
    std::vector<EraserStrokeGroup> m_eraserGroups;         ///< 橡皮擦笔迹组（已提交）
    std::vector<EraserStrokeGroup> m_redoEraserGroups;     ///< 重做橡皮擦笔迹组
    std::vector<MosaicStrokeGroup> m_mosaicGroups;         ///< 马赛克笔迹组（已提交）
    std::vector<MosaicStrokeGroup> m_redoMosaicGroups;     ///< 重做马赛克笔迹组
    std::vector<QRect> m_activeEraserStroke;               ///< 活跃橡皮擦笔迹缓冲（拖拽中）
    std::vector<QRect> m_activeMosaicStroke;               ///< 活跃马赛克笔迹缓冲（拖拽中）
    uint64_t m_nextOpSeq = 0;                              ///< 全局操作序号（递增）
    quint64 m_contentVersion = 0;                          ///< 基础内容版本（标注/橡皮擦变化递增）
    quint64 m_mosaicVersion = 0;                           ///< 马赛克笔迹版本（马赛克变化递增）
    MoveRecord m_undoMoveState;                            ///< undo 目标状态（移动前原始位置）
    MoveRecord m_redoMoveState;                            ///< redo 目标状态（移动后位置）
    ClearSnapshot m_clearUndoSnapshot;                     ///< 清除操作的撤销快照（保存清除前状态）
    ClearSnapshot m_clearRedoSnapshot;                     ///< 清除操作的重做快照（保存重做清除的前置状态）
};

#endif // ANNOTATIONMANAGER_H
