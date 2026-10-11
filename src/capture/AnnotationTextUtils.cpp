#include "AnnotationTextUtils.h"
#include "Annotation.h"
#include "OverlayTextEdit.h"
#include "../log/Logger.h"

#include <QFontMetrics>
#include <QtMath>

namespace AnnotationTextUtils {

std::unique_ptr<TextAnnotation> makeTextAnnotation(OverlayTextEdit *edit,
                                                   const QPoint &widgetPosGlobal,
                                                   const QColor &color) {
    const QString text = edit->toPlainText().trimmed();
    if (text.isEmpty()) {
        return nullptr;
    }

    // 计算文本在 OverlayTextEdit 中的实际渲染位置（基线坐标）
    const int frameWidth = edit->frameWidth();
    const qreal docMargin = edit->document()->documentMargin();
    QFont font;
    font.setPixelSize(edit->fontSize());
    QFontMetrics fm(font);

    // 文本相对于控件左上角的偏移（控件坐标）
    const int textOffsetX = frameWidth + static_cast<int>(docMargin);
    const int textOffsetY = frameWidth + static_cast<int>(docMargin) + fm.ascent();

    // 获取旋转角度（OverlayTextEdit 的旋转中心是控件中心）
    const qreal rotation = edit->rotationDegrees();

    QPoint textPos;
    if (rotation != 0.0) {
        const int widgetWidth = edit->width();
        const int widgetHeight = edit->height();
        const QPoint widgetCenter = widgetPosGlobal + QPoint(widgetWidth / 2, widgetHeight / 2);

        // 文本基线相对于旋转中心的偏移（控件坐标）
        const int dx = textOffsetX - widgetWidth / 2;
        const int dy = textOffsetY - widgetHeight / 2;

        // 应用旋转变换（以控件中心为旋转中心）
        const qreal rad = qDegreesToRadians(rotation);
        const qreal cosRad = std::cos(rad);
        const qreal sinRad = std::sin(rad);

        const int rotatedDx = static_cast<int>(dx * cosRad - dy * sinRad);
        const int rotatedDy = static_cast<int>(dx * sinRad + dy * cosRad);

        textPos = widgetCenter + QPoint(rotatedDx, rotatedDy);
    } else {
        textPos = QPoint(widgetPosGlobal.x() + textOffsetX, widgetPosGlobal.y() + textOffsetY);
    }

    LOG_INFO(QString("[AnnotationTextUtils] Text annotation pos: widget=(%1,%2) offset=(%3,%4) rotation=%5 -> final=(%6,%7)")
        .arg(widgetPosGlobal.x()).arg(widgetPosGlobal.y())
        .arg(textOffsetX).arg(textOffsetY)
        .arg(rotation)
        .arg(textPos.x()).arg(textPos.y()));

    auto textAnnotation = std::make_unique<TextAnnotation>(textPos, color, edit->fontSize());
    textAnnotation->setText(text);
    textAnnotation->setRotation(rotation);
    return textAnnotation;
}

} // namespace AnnotationTextUtils
