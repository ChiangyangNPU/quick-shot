#include "CaptureComposer.h"
#include "AnnotationManager.h"

#include <QPainter>

namespace CaptureComposer {

QPixmap composeCanvas(const QPixmap &background,
                      const AnnotationManager &annotations,
                      const QPoint &globalOffset) {
    QPixmap canvas(background.size());
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);

    // 1. 背景
    if (!background.isNull()) {
        painter.drawPixmap(0, 0, background);
    }

    // 2. 标注（全局坐标 → 背景本地坐标）
    painter.save();
    painter.translate(globalOffset);
    if (annotations.hasAnnotations()) {
        annotations.draw(painter);
    }
    painter.restore();

    // 3. 马赛克（同时像素化背景和标注）
    if (annotations.hasMosaicStrokes()) {
        annotations.drawMosaic(painter, canvas,
                               AnnotationManager::kDefaultMosaicBlockSize,
                               globalOffset);
    }

    painter.end();
    return canvas;
}

} // namespace CaptureComposer
