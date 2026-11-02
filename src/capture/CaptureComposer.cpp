#include "CaptureComposer.h"
#include "AnnotationManager.h"

#include <QPainter>

namespace CaptureComposer {

QPixmap composeCanvas(const QPixmap &background,
                      const AnnotationManager &annotations,
                      const QPoint &globalOffset) {
    QPixmap canvas(background.size());
    // 画布继承背景 DPR：Retina 下保持物理像素分辨率（此前画布为 dpr=1，
    // 背景被按逻辑尺寸绘入，二次裁剪后只剩 1x 逻辑分辨率，贴图时被系统放大而模糊）
    canvas.setDevicePixelRatio(background.devicePixelRatio());
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);

    // 1. 背景（画布与背景 DPR 一致，按逻辑坐标 1:1 铺满）
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
