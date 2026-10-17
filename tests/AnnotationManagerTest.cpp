#include <QtTest>
#include "capture/AnnotationManager.h"
#include "capture/Annotation.h"

/**
 * @brief AnnotationManager 撤销/重做状态机的单元测试
 *
 * 覆盖三操作流（标注/橡皮擦组/马赛克组）的序号合并撤销重做、
 * 清除快照（undo 恢复/redo 再清除）、移动记录（undo/redo 恢复位置、
 * 无位移不记录）、新操作失效语义（移动历史/清除重做快照/redo 栈）、
 * scaleAll 全局缩放对栈内与快照内容的同步。
 * 纯逻辑无 GUI 依赖，QTEST_GUILESS_MAIN 即可。
 * @author chiangyang
 */
class AnnotationManagerTest : public QObject {
    Q_OBJECT

    static std::unique_ptr<Annotation> makeRect(const QPoint &start, const QPoint &end) {
        auto rect = std::make_unique<RectAnnotation>(start, QColor(Qt::red), 3);
        rect->setEnd(end);
        return rect;
    }

private slots:
    void addCommitUndoRedo_annotationBasics() {
        AnnotationManager m;
        QCOMPARE(m.canUndo(), false);
        QCOMPARE(m.canRedo(), false);
        QCOMPARE(m.hasAnnotations(), false);

        m.add(makeRect(QPoint(10, 10), QPoint(30, 30)));
        m.commit();
        QVERIFY(m.hasAnnotations());
        QVERIFY(m.canUndo());

        m.add(makeRect(QPoint(0, 0), QPoint(5, 5)));
        m.commit();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(0, 0));

        // 撤销：后进先出
        m.undo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(10, 10));
        QVERIFY(m.canRedo());
        m.undo();
        QCOMPARE(m.hasAnnotations(), false);
        QCOMPARE(m.canUndo(), false);

        // 重做：先进先出（最早被撤销的先恢复）
        m.redo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(10, 10));
        m.redo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(0, 0));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(5, 5));
        QCOMPARE(m.canRedo(), false);
    }

    void rollback_removesLastAnnotation() {
        AnnotationManager m;
        m.add(makeRect(QPoint(1, 1), QPoint(2, 2)));
        m.commit();
        // 未提交的绘制（用户取消）
        m.add(makeRect(QPoint(8, 8), QPoint(9, 9)));
        m.rollback();
        QCOMPARE(m.hasAnnotations(), true);
        QCOMPARE(m.lastAnnotation()->start(), QPoint(1, 1));
        QCOMPARE(m.canRedo(), false);
    }

    void commit_clearsRedoStack() {
        AnnotationManager m;
        m.add(makeRect(QPoint(1, 1), QPoint(2, 2)));
        m.commit();
        m.undo();
        QVERIFY(m.canRedo());
        // 新操作提交后丢弃重做历史
        m.add(makeRect(QPoint(3, 3), QPoint(4, 4)));
        m.commit();
        QCOMPARE(m.canRedo(), false);
    }

    void undoRedo_sequenceAcrossAnnotationAndEraser() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(30, 30)));
        m.commit();                                   // seq 0
        m.eraseAt(QPoint(15, 15), 2);
        m.endEraserStroke();                          // seq 1
        QVERIFY(!m.eraserRegion().isEmpty());
        QVERIFY(m.hasAnnotations());

        // 撤销序号最大的：橡皮擦组
        m.undo();
        QVERIFY(m.eraserRegion().isEmpty());
        QVERIFY(m.hasAnnotations());

        // 撤销标注
        m.undo();
        QCOMPARE(m.hasAnnotations(), false);
        QCOMPARE(m.eraserRegion().isEmpty(), true);
        QCOMPARE(m.canUndo(), false);

        // 重做最早被撤销的：标注先回来，橡皮擦后回来
        m.redo();
        QCOMPARE(m.hasAnnotations(), true);
        QVERIFY(m.eraserRegion().isEmpty());
        m.redo();
        QVERIFY(!m.eraserRegion().isEmpty());
        QCOMPARE(m.canRedo(), false);
    }

    void endEraserStroke_invalidatesRedo() {
        AnnotationManager m;
        m.add(makeRect(QPoint(0, 0), QPoint(5, 5)));
        m.commit();
        m.undo();
        QVERIFY(m.canRedo());
        m.eraseAt(QPoint(2, 2), 2);
        m.endEraserStroke();     // 新笔迹清空重做
        QCOMPARE(m.canRedo(), false);
        QVERIFY(m.canUndo());
    }

    void mosaicStroke_undoRedo() {
        AnnotationManager m;
        m.mosaicAt(QPoint(20, 20), 5);
        m.endMosaicStroke();     // rect = (15,15,10,10)
        QVERIFY(m.hasMosaicStrokes());
        auto strokes = m.mosaicStrokes();
        QCOMPARE(int(strokes.size()), 1);
        QCOMPARE(strokes[0], QRect(15, 15, 10, 10));

        m.undo();
        QCOMPARE(m.hasMosaicStrokes(), false);
        m.redo();
        QVERIFY(m.hasMosaicStrokes());
        QCOMPARE(m.mosaicStrokes()[0], QRect(15, 15, 10, 10));
    }

    void clear_snapshotUndoRestores_redoApplies() {
        AnnotationManager m;
        m.add(makeRect(QPoint(1, 1), QPoint(2, 2)));
        m.commit();
        m.mosaicAt(QPoint(9, 9), 2);
        m.endMosaicStroke();

        m.clear();
        QCOMPARE(m.hasAnnotations(), false);
        QCOMPARE(m.hasMosaicStrokes(), false);
        QVERIFY(m.canUndo());
        QCOMPARE(m.canRedo(), false);

        // 撤销清除：完整状态恢复
        m.undo();
        QVERIFY(m.hasAnnotations());
        QVERIFY(m.hasMosaicStrokes());

        // 重做清除：再次清空
        QVERIFY(m.canRedo());
        m.redo();
        QCOMPARE(m.hasAnnotations(), false);
        QCOMPARE(m.hasMosaicStrokes(), false);
    }

    void clear_onEmpty_isNoOp() {
        AnnotationManager m;
        m.clear();
        QVERIFY(!m.canUndo());
        QVERIFY(!m.canRedo());
        // 清除后撤销不可用：空清除不产生可撤销快照
        m.undo();
        QVERIFY(!m.hasAnnotations());
    }

    void move_undoRedoRestoresPosition() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(20, 20)));
        m.commit();
        QVERIFY(m.hasUndoMove() == false);

        m.beginMove();
        m.translateLast(QPoint(5, 7));
        m.endMove();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(15, 17));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(25, 27));

        // 撤销移动：恢复原始位置（优先于撤销标注创建）
        m.undo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(10, 10));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(20, 20));

        // 重做移动
        QVERIFY(m.hasRedoMove());
        m.redo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(15, 17));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(25, 27));
    }

    void move_withoutTranslation_notRecorded() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(20, 20)));
        m.commit();
        m.beginMove();
        m.endMove();     // 位置未变，移动记录取消
        QCOMPARE(m.hasUndoMove(), false);
        // 撤销直接作用于标注创建而非移动
        m.undo();
        QCOMPARE(m.hasAnnotations(), false);
    }

    void move_penAnnotationRestoresPoints() {
        AnnotationManager m;
        auto pen = std::make_unique<PenAnnotation>(QPoint(0, 0), QColor(Qt::red), 3);
        pen->addPoint(QPoint(0, 0));
        pen->addPoint(QPoint(10, 10));
        m.add(std::move(pen));
        m.commit();

        m.beginMove();
        m.translateLast(QPoint(3, 3));
        m.endMove();
        auto *moved = static_cast<PenAnnotation *>(m.lastAnnotation());
        QCOMPARE(moved->points()[0], QPoint(3, 3));
        QCOMPARE(moved->points()[1], QPoint(13, 13));

        m.undo();
        auto *restored = static_cast<PenAnnotation *>(m.lastAnnotation());
        QCOMPARE(restored->points()[0], QPoint(0, 0));
        QCOMPARE(restored->points()[1], QPoint(10, 10));
    }

    void newOperation_invalidatesMoveState() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(20, 20)));
        m.commit();
        m.beginMove();
        m.translateLast(QPoint(5, 0));
        m.endMove();
        QVERIFY(m.hasUndoMove());

        // 新标注使移动历史失效
        m.add(makeRect(QPoint(50, 50), QPoint(60, 60)));
        m.commit();
        QCOMPARE(m.hasUndoMove(), false);

        // 撤销作用于最新的标注（栈顶 rectB），移动记录不再介入：
        // rectA 的移动已随新标注提交而固化，停留在移动后位置 (15,10)
        m.undo();
        QCOMPARE(m.lastAnnotation()->start(), QPoint(15, 10));
    }

    void scaleAll_scalesStackAndActiveGroups() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(30, 30)));
        m.commit();
        m.mosaicAt(QPoint(20, 20), 5);   // (15,15,10,10)
        m.endMosaicStroke();

        m.scaleAll(2.0, 2.0);

        // 栈内标注同步缩放
        QCOMPARE(m.lastAnnotation()->start(), QPoint(20, 20));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(60, 60));
        // 马赛克笔迹同步缩放
        QCOMPARE(m.mosaicStrokes()[0], QRect(30, 30, 20, 20));

        // 撤销/重做均在缩放后坐标系上进行
        m.undo();   // 撤销马赛克组（序号大）
        QCOMPARE(m.hasMosaicStrokes(), false);
        m.undo();   // 撤销标注
        QCOMPARE(m.hasAnnotations(), false);
        m.redo();   // 重做标注：位置保持缩放后
        QCOMPARE(m.lastAnnotation()->start(), QPoint(20, 20));
    }

    void scaleAll_identity_noOp() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(30, 30)));
        m.commit();
        m.scaleAll(1.0, 1.0);
        QCOMPARE(m.lastAnnotation()->start(), QPoint(10, 10));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(30, 30));
    }

    void scaleAll_scalesClearSnapshot() {
        AnnotationManager m;
        m.add(makeRect(QPoint(10, 10), QPoint(30, 30)));
        m.commit();
        m.clear();               // 内容进入清除撤销快照
        m.scaleAll(2.0, 2.0);    // 缩放需同步进快照（PinWindow 缩放场景）
        m.undo();
        QVERIFY(m.hasAnnotations());
        QCOMPARE(m.lastAnnotation()->start(), QPoint(20, 20));
        QCOMPARE(m.lastAnnotation()->end(), QPoint(60, 60));
    }
};

QTEST_GUILESS_MAIN(AnnotationManagerTest)
#include "AnnotationManagerTest.moc"
