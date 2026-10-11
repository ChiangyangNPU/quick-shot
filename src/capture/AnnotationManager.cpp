#include "AnnotationManager.h"

#include <QPainter>
#include <QRegion>
#include <algorithm>

// ============ AnnotationManager.h 内联实现迁入 ============

void AnnotationManager::add(std::unique_ptr<Annotation> annotation) {
        m_undoStack.push_back(std::move(annotation));
        m_annotationSeqs.push_back(m_nextOpSeq++);
        // 新标注使移动历史失效
        m_undoMoveState.valid = false;
        m_redoMoveState.valid = false;
        // 新标注使清除重做快照失效（新操作丢弃重做历史）
        m_clearRedoSnapshot.valid = false;
}

void AnnotationManager::updateLast(const QPoint &end) {
        if (!m_undoStack.empty()) {
            m_undoStack.back()->setEnd(end);
        }
}

void AnnotationManager::commit() {
        clearRedo();
        m_clearRedoSnapshot.valid = false;   // 新操作使清除重做快照失效
}

void AnnotationManager::rollback() {
        if (!m_undoStack.empty()) {
            m_undoStack.pop_back();
            m_annotationSeqs.pop_back();
        }
        m_undoMoveState.valid = false;
        m_redoMoveState.valid = false;
        m_clearRedoSnapshot.valid = false;
}

void AnnotationManager::undo() {
        // 优先级1：撤销清除操作
        if (m_clearUndoSnapshot.valid) {
            // 保存当前清除后的空状态到重做快照，使 redo 可再次清除
            m_clearRedoSnapshot.valid = true;
            m_clearRedoSnapshot.undoStack = std::move(m_undoStack);
            m_clearRedoSnapshot.redoStack = std::move(m_redoStack);
            m_clearRedoSnapshot.annotationSeqs = std::move(m_annotationSeqs);
            m_clearRedoSnapshot.redoAnnotationSeqs = std::move(m_redoAnnotationSeqs);
            m_clearRedoSnapshot.eraserGroups = std::move(m_eraserGroups);
            m_clearRedoSnapshot.mosaicGroups = std::move(m_mosaicGroups);
            m_clearRedoSnapshot.redoEraserGroups = std::move(m_redoEraserGroups);
            m_clearRedoSnapshot.redoMosaicGroups = std::move(m_redoMosaicGroups);
            m_clearRedoSnapshot.undoMoveState = m_undoMoveState;
            m_clearRedoSnapshot.redoMoveState = m_redoMoveState;
            m_clearRedoSnapshot.nextOpSeq = m_nextOpSeq;

            // 恢复清除前的完整状态
            m_undoStack = std::move(m_clearUndoSnapshot.undoStack);
            m_redoStack = std::move(m_clearUndoSnapshot.redoStack);
            m_annotationSeqs = std::move(m_clearUndoSnapshot.annotationSeqs);
            m_redoAnnotationSeqs = std::move(m_clearUndoSnapshot.redoAnnotationSeqs);
            m_eraserGroups = std::move(m_clearUndoSnapshot.eraserGroups);
            m_mosaicGroups = std::move(m_clearUndoSnapshot.mosaicGroups);
            m_redoEraserGroups = std::move(m_clearUndoSnapshot.redoEraserGroups);
            m_redoMosaicGroups = std::move(m_clearUndoSnapshot.redoMosaicGroups);
            m_undoMoveState = m_clearUndoSnapshot.undoMoveState;
            m_redoMoveState = m_clearUndoSnapshot.redoMoveState;
            m_nextOpSeq = m_clearUndoSnapshot.nextOpSeq;

            m_clearUndoSnapshot.valid = false;
            return;
        }

        // 优先级2：撤销移动操作
        if (m_undoMoveState.valid && !m_undoStack.empty()) {
            Annotation *last = m_undoStack.back().get();
            m_redoMoveState = saveAnnotationState(last);  // 保存当前（移动后）状态
            restoreAnnotationState(last, m_undoMoveState);  // 恢复到原始位置
            m_undoMoveState.valid = false;
            return;
        }

        // 优先级3：撤销最后一步操作（标注/橡皮擦组/马赛克组中序号最大的）
        undoLastOperation();
}

void AnnotationManager::redo() {
        // 优先级1：重做清除操作
        if (m_clearRedoSnapshot.valid) {
            // 保存当前状态到撤销快照，使 undo 可再次恢复
            m_clearUndoSnapshot.valid = true;
            m_clearUndoSnapshot.undoStack = std::move(m_undoStack);
            m_clearUndoSnapshot.redoStack = std::move(m_redoStack);
            m_clearUndoSnapshot.annotationSeqs = std::move(m_annotationSeqs);
            m_clearUndoSnapshot.redoAnnotationSeqs = std::move(m_redoAnnotationSeqs);
            m_clearUndoSnapshot.eraserGroups = std::move(m_eraserGroups);
            m_clearUndoSnapshot.mosaicGroups = std::move(m_mosaicGroups);
            m_clearUndoSnapshot.redoEraserGroups = std::move(m_redoEraserGroups);
            m_clearUndoSnapshot.redoMosaicGroups = std::move(m_redoMosaicGroups);
            m_clearUndoSnapshot.undoMoveState = m_undoMoveState;
            m_clearUndoSnapshot.redoMoveState = m_redoMoveState;
            m_clearUndoSnapshot.nextOpSeq = m_nextOpSeq;

            // 恢复清除后的空状态（即再次清除）
            m_undoStack = std::move(m_clearRedoSnapshot.undoStack);
            m_redoStack = std::move(m_clearRedoSnapshot.redoStack);
            m_annotationSeqs = std::move(m_clearRedoSnapshot.annotationSeqs);
            m_redoAnnotationSeqs = std::move(m_clearRedoSnapshot.redoAnnotationSeqs);
            m_eraserGroups = std::move(m_clearRedoSnapshot.eraserGroups);
            m_mosaicGroups = std::move(m_clearRedoSnapshot.mosaicGroups);
            m_redoEraserGroups = std::move(m_clearRedoSnapshot.redoEraserGroups);
            m_redoMosaicGroups = std::move(m_clearRedoSnapshot.redoMosaicGroups);
            m_undoMoveState = m_clearRedoSnapshot.undoMoveState;
            m_redoMoveState = m_clearRedoSnapshot.redoMoveState;
            m_nextOpSeq = m_clearRedoSnapshot.nextOpSeq;

            m_clearRedoSnapshot.valid = false;
            return;
        }

        // 优先级2：重做移动操作
        if (m_redoMoveState.valid && !m_undoStack.empty()) {
            Annotation *last = m_undoStack.back().get();
            m_undoMoveState = saveAnnotationState(last);  // 保存当前（移动前）状态
            restoreAnnotationState(last, m_redoMoveState);  // 恢复到移动后位置
            m_redoMoveState.valid = false;
            return;
        }

        // 优先级3：重做最早被撤销的操作（redo栈中序号最小的）
        redoEarliestOperation();
}

void AnnotationManager::clear() {
        // 如果当前没有任何内容，无需保存快照
        if (m_undoStack.empty() && m_redoStack.empty() &&
            m_eraserGroups.empty() && m_mosaicGroups.empty() &&
            m_redoEraserGroups.empty() && m_redoMosaicGroups.empty() &&
            m_activeEraserStroke.empty() && m_activeMosaicStroke.empty() &&
            !m_undoMoveState.valid && !m_redoMoveState.valid) {
            m_clearUndoSnapshot.valid = false;
            m_clearRedoSnapshot.valid = false;
            return;
        }

        // 保存当前状态到清除撤销快照
        m_clearUndoSnapshot.valid = true;
        m_clearUndoSnapshot.undoStack = std::move(m_undoStack);
        m_clearUndoSnapshot.redoStack = std::move(m_redoStack);
        m_clearUndoSnapshot.annotationSeqs = std::move(m_annotationSeqs);
        m_clearUndoSnapshot.redoAnnotationSeqs = std::move(m_redoAnnotationSeqs);
        m_clearUndoSnapshot.eraserGroups = std::move(m_eraserGroups);
        m_clearUndoSnapshot.mosaicGroups = std::move(m_mosaicGroups);
        m_clearUndoSnapshot.redoEraserGroups = std::move(m_redoEraserGroups);
        m_clearUndoSnapshot.redoMosaicGroups = std::move(m_redoMosaicGroups);
        m_clearUndoSnapshot.undoMoveState = m_undoMoveState;
        m_clearUndoSnapshot.redoMoveState = m_redoMoveState;
        m_clearUndoSnapshot.nextOpSeq = m_nextOpSeq;

        // 清空当前状态
        m_undoStack.clear();
        m_redoStack.clear();
        m_annotationSeqs.clear();
        m_redoAnnotationSeqs.clear();
        m_eraserGroups.clear();
        m_mosaicGroups.clear();
        m_redoEraserGroups.clear();
        m_redoMosaicGroups.clear();
        m_activeEraserStroke.clear();
        m_activeMosaicStroke.clear();
        m_undoMoveState.valid = false;
        m_redoMoveState.valid = false;

        // 新清除操作使重做快照失效
        m_clearRedoSnapshot.valid = false;
}

void AnnotationManager::scaleAll(double sx, double sy) {
        if (sx == 1.0 && sy == 1.0) return;
        for (auto &ann : m_undoStack) ann->scale(sx, sy);
        for (auto &ann : m_redoStack) ann->scale(sx, sy);
        scaleEraserGroups(m_eraserGroups, sx, sy);
        scaleEraserGroups(m_redoEraserGroups, sx, sy);
        scaleMosaicGroups(m_mosaicGroups, sx, sy);
        scaleMosaicGroups(m_redoMosaicGroups, sx, sy);
        m_undoMoveState.valid = false;
        m_redoMoveState.valid = false;

        // 同步缩放清除撤销快照中的内容
        if (m_clearUndoSnapshot.valid) {
            for (auto &ann : m_clearUndoSnapshot.undoStack) ann->scale(sx, sy);
            for (auto &ann : m_clearUndoSnapshot.redoStack) ann->scale(sx, sy);
            scaleEraserGroups(m_clearUndoSnapshot.eraserGroups, sx, sy);
            scaleEraserGroups(m_clearUndoSnapshot.redoEraserGroups, sx, sy);
            scaleMosaicGroups(m_clearUndoSnapshot.mosaicGroups, sx, sy);
            scaleMosaicGroups(m_clearUndoSnapshot.redoMosaicGroups, sx, sy);
            m_clearUndoSnapshot.undoMoveState.valid = false;
            m_clearUndoSnapshot.redoMoveState.valid = false;
        }
        // 同步缩放清除重做快照中的内容
        if (m_clearRedoSnapshot.valid) {
            for (auto &ann : m_clearRedoSnapshot.undoStack) ann->scale(sx, sy);
            for (auto &ann : m_clearRedoSnapshot.redoStack) ann->scale(sx, sy);
            scaleEraserGroups(m_clearRedoSnapshot.eraserGroups, sx, sy);
            scaleEraserGroups(m_clearRedoSnapshot.redoEraserGroups, sx, sy);
            scaleMosaicGroups(m_clearRedoSnapshot.mosaicGroups, sx, sy);
            scaleMosaicGroups(m_clearRedoSnapshot.redoMosaicGroups, sx, sy);
            m_clearRedoSnapshot.undoMoveState.valid = false;
            m_clearRedoSnapshot.redoMoveState.valid = false;
        }
}

void AnnotationManager::eraseAt(const QPoint &pos, const int size) {
        QRect eraserRect(pos.x() - size, pos.y() - size, size * 2, size * 2);
        m_activeEraserStroke.push_back(eraserRect);
}

void AnnotationManager::endEraserStroke() {
        if (!m_activeEraserStroke.empty()) {
            m_eraserGroups.push_back({std::move(m_activeEraserStroke), m_nextOpSeq++});
            m_activeEraserStroke.clear();
            clearRedo();
            m_clearRedoSnapshot.valid = false;
        }
}

QRegion AnnotationManager::eraserRegion() const {
        QRegion region;
        for (const auto &group : m_eraserGroups) {
            for (const QRect &r : group.rects) {
                region += r;
            }
        }
        for (const QRect &r : m_activeEraserStroke) {
            region += r;
        }
        return region;
}

void AnnotationManager::mosaicAt(const QPoint &pos, const int size) {
        QRect mosaicRect(pos.x() - size, pos.y() - size, size * 2, size * 2);
        m_activeMosaicStroke.push_back(mosaicRect);
}

void AnnotationManager::endMosaicStroke() {
        if (!m_activeMosaicStroke.empty()) {
            m_mosaicGroups.push_back({std::move(m_activeMosaicStroke), m_nextOpSeq++});
            m_activeMosaicStroke.clear();
            clearRedo();
            m_clearRedoSnapshot.valid = false;
        }
}

void AnnotationManager::draw(QPainter &painter) const {
        bool hasEraser = !m_eraserGroups.empty() || !m_activeEraserStroke.empty();
        if (!hasEraser) {
            for (const auto &annotation : m_undoStack) {
                annotation->draw(painter);
            }
            return;
        }

        // 有橡皮擦笔迹时，设置裁剪区域排除擦除部分
        painter.save();
        QRegion clipRegion = painter.clipRegion();
        QRegion eraser = eraserRegion();
        if (clipRegion.isEmpty()) {
            // 如果当前没有裁剪区域，用一个足够大的区域减去擦除区域
            clipRegion = QRegion(QRect(-100000, -100000, 200000, 200000));
        }
        painter.setClipRegion(clipRegion.subtracted(eraser));
        for (const auto &annotation : m_undoStack) {
            annotation->draw(painter);
        }
        painter.restore();
}

Annotation * AnnotationManager::lastAnnotation() const {
        if (!m_undoStack.empty()) {
            return m_undoStack.back().get();
        }
        return nullptr;
}

void AnnotationManager::drawControlPoints(QPainter &painter) const {
        Annotation *last = lastAnnotation();
        if (!last || !last->hasControlPoints()) return;
        auto cps = last->controlPoints();
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::white);
        painter.setPen(QPen(QColor(80, 80, 80), 1));
        const int r = 5;
        for (const auto &cp : cps) {
            painter.drawEllipse(cp.pos, r, r);
        }
        painter.restore();
}

int AnnotationManager::hitControlPoint(const QPoint &pos, const int tolerance) const {
        Annotation *last = lastAnnotation();
        if (!last || !last->hasControlPoints()) return -1;
        auto cps = last->controlPoints();
        for (int i = 0; i < cps.size(); ++i) {
            if (QLineF(pos, cps[i].pos).length() <= tolerance) return i;
        }
        return -1;
}

Qt::CursorShape AnnotationManager::controlPointCursorAt(const QPoint &pos) const {
        int idx = hitControlPoint(pos);
        if (idx < 0) return Qt::ArrowCursor;
        Annotation *last = lastAnnotation();
        return last ? last->controlPointCursor(idx) : Qt::ArrowCursor;
}

void AnnotationManager::moveLastControlPoint(int index, const QPoint &pos) {
        Annotation *last = lastAnnotation();
        if (last) last->moveControlPoint(index, pos);
}

void AnnotationManager::beginMove() {
        Annotation *last = lastAnnotation();
        if (!last) return;
        m_undoMoveState = saveAnnotationState(last);
}

void AnnotationManager::translateLast(const QPoint &offset) {
        Annotation *last = lastAnnotation();
        if (last) {
            last->translate(offset);
        }
}

void AnnotationManager::endMove() {
        Annotation *last = lastAnnotation();
        if (!last || !m_undoMoveState.valid) return;
        // 判断位置是否变化
        MoveRecord now = saveAnnotationState(last);
        if (now.start != m_undoMoveState.start || now.end != m_undoMoveState.end ||
            now.points != m_undoMoveState.points) {
            // 位置变了，保留 m_undoMoveState（存原始位置），清空 redo
            m_redoMoveState.valid = false;
            m_clearRedoSnapshot.valid = false;   // 新操作使清除重做快照失效
        } else {
            // 位置没变，取消移动记录
            m_undoMoveState.valid = false;
        }
}

std::vector<QRect> AnnotationManager::mosaicStrokes() const {
        std::vector<QRect> result;
        for (const auto &group : m_mosaicGroups) {
            for (const QRect &r : group.rects) {
                result.push_back(r);
            }
        }
        for (const QRect &r : m_activeMosaicStroke) {
            result.push_back(r);
        }
        return result;
}

bool AnnotationManager::canUndo() const {
        return m_clearUndoSnapshot.valid || !m_undoStack.empty() ||
               !m_eraserGroups.empty() || !m_mosaicGroups.empty() ||
               m_undoMoveState.valid;
}

bool AnnotationManager::canRedo() const {
        return m_clearRedoSnapshot.valid || !m_redoStack.empty() ||
               !m_redoEraserGroups.empty() || !m_redoMosaicGroups.empty() ||
               m_redoMoveState.valid;
}

AnnotationManager::MoveRecord AnnotationManager::saveAnnotationState(const Annotation *ann) const {
        MoveRecord record;
        record.valid = true;
        record.start = ann->start();
        record.end = ann->end();
        if (ann->type() == AnnotationType::Pen) {
            record.points = static_cast<const PenAnnotation *>(ann)->points();
        }
        // 三角形标注通过 dynamic_cast 识别（其 type 仍为 Rectangle）
        if (auto *tri = dynamic_cast<const TriangleAnnotation *>(ann)) {
            record.vertices = tri->vertices();
        }
        return record;
}

void AnnotationManager::restoreAnnotationState(Annotation *ann, const AnnotationManager::MoveRecord &record) const {
        ann->setStart(record.start);
        ann->setEnd(record.end);
        if (ann->type() == AnnotationType::Pen) {
            static_cast<PenAnnotation *>(ann)->setPoints(record.points);
        }
        // 三角形：先 setEnd（会触发 syncVerticesFromRect 重置为等腰），
        // 再 setVertices 覆盖为保存的独立顶点，确保恢复正确
        if (auto *tri = dynamic_cast<TriangleAnnotation *>(ann)) {
            tri->setVertices(record.vertices);
        }
}

void AnnotationManager::clearRedo() {
        m_redoStack.clear();
        m_redoAnnotationSeqs.clear();
        m_redoEraserGroups.clear();
        m_redoMosaicGroups.clear();
        m_redoMoveState.valid = false;
}

void AnnotationManager::scaleEraserGroups(std::vector<EraserStrokeGroup> &groups, double sx, double sy) {
        for (auto &group : groups) {
            for (auto &r : group.rects) {
                r = QRect(qRound(r.x() * sx), qRound(r.y() * sy),
                          qRound(r.width() * sx), qRound(r.height() * sy));
            }
        }
}

void AnnotationManager::scaleMosaicGroups(std::vector<MosaicStrokeGroup> &groups, double sx, double sy) {
        for (auto &group : groups) {
            for (auto &r : group.rects) {
                r = QRect(qRound(r.x() * sx), qRound(r.y() * sy),
                          qRound(r.width() * sx), qRound(r.height() * sy));
            }
        }
}

void AnnotationManager::undoLastOperation() {
        bool hasAnn = !m_undoStack.empty();
        bool hasEraser = !m_eraserGroups.empty();
        bool hasMosaic = !m_mosaicGroups.empty();

        if (!hasAnn && !hasEraser && !hasMosaic) return;

        // 找序号最大的操作
        uint64_t maxSeq = 0;
        if (hasAnn) maxSeq = std::max(maxSeq, m_annotationSeqs.back());
        if (hasEraser) maxSeq = std::max(maxSeq, m_eraserGroups.back().seq);
        if (hasMosaic) maxSeq = std::max(maxSeq, m_mosaicGroups.back().seq);

        // 撤销对应操作
        if (hasAnn && m_annotationSeqs.back() == maxSeq) {
            m_redoStack.push_back(std::move(m_undoStack.back()));
            m_undoStack.pop_back();
            m_redoAnnotationSeqs.push_back(m_annotationSeqs.back());
            m_annotationSeqs.pop_back();
        } else if (hasEraser && m_eraserGroups.back().seq == maxSeq) {
            m_redoEraserGroups.push_back(std::move(m_eraserGroups.back()));
            m_eraserGroups.pop_back();
        } else if (hasMosaic && m_mosaicGroups.back().seq == maxSeq) {
            m_redoMosaicGroups.push_back(std::move(m_mosaicGroups.back()));
            m_mosaicGroups.pop_back();
        }
}

void AnnotationManager::redoEarliestOperation() {
        bool hasAnn = !m_redoStack.empty();
        bool hasEraser = !m_redoEraserGroups.empty();
        bool hasMosaic = !m_redoMosaicGroups.empty();

        if (!hasAnn && !hasEraser && !hasMosaic) return;

        // 找序号最小的操作（最早被撤销的）
        uint64_t minSeq = UINT64_MAX;
        if (hasAnn) minSeq = std::min(minSeq, m_redoAnnotationSeqs.back());
        if (hasEraser) minSeq = std::min(minSeq, m_redoEraserGroups.back().seq);
        if (hasMosaic) minSeq = std::min(minSeq, m_redoMosaicGroups.back().seq);

        // 重做对应操作
        if (hasAnn && m_redoAnnotationSeqs.back() == minSeq) {
            m_undoStack.push_back(std::move(m_redoStack.back()));
            m_redoStack.pop_back();
            m_annotationSeqs.push_back(m_redoAnnotationSeqs.back());
            m_redoAnnotationSeqs.pop_back();
        } else if (hasEraser && m_redoEraserGroups.back().seq == minSeq) {
            m_eraserGroups.push_back(std::move(m_redoEraserGroups.back()));
            m_redoEraserGroups.pop_back();
        } else if (hasMosaic && m_redoMosaicGroups.back().seq == minSeq) {
            m_mosaicGroups.push_back(std::move(m_redoMosaicGroups.back()));
            m_redoMosaicGroups.pop_back();
        }
}

