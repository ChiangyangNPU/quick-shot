#include "PetWindow.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QTimer>
#include <QRandomGenerator>
#include <QtMath>

namespace {
// 身体中心（咀嚼挤压变换的锚点）
constexpr QPointF kBodyCenter(48.0, 54.0);
// 嘴部中心（局部坐标）
constexpr QPointF kMouthCenter(48.0, 62.0);
// 拖动判定阈值（96 基准像素）：超过则视为拖动而非点击
constexpr int kDragThreshold = 8;

/**
 * @brief 宠物外观规格：描述一个样式在 96 基准坐标系里的配色与特征部件
 *
 * 形体骨架（身体/肚皮/脚/眼睛/嘴的位置与动画）全样式共用，
 * 样式差异体现在配色、耳朵/尾巴形状与特征部件（眼圈/虎斑/面罩/豆眉/
 * 小翅膀/角/鸡冠/猪鼻/蛇信/马鬃）上。
 * @author chiangyang
 */
struct PetSpec {
    QColor body;            ///< 身体主色
    QColor earFill;         ///< 耳朵填充
    QColor outline;         ///< 描边
    QColor belly;           ///< 肚皮
    QColor blush;           ///< 腮红（含 alpha）
    QColor eye;             ///< 眼睛瞳色 / 眨眼横线颜色
    QColor mouthInner;      ///< 张嘴口腔
    QColor foot;            ///< 脚
    QColor smilePen;        ///< 闭嘴微笑弧线颜色
    QColor earInner = QColor(0, 0, 0, 0); ///< 耳内填充（alpha 0 表示不画）
    enum class Ear { Triangle, Round, Long, None } ear = Ear::Triangle;
    QColor eyePatch = QColor(0, 0, 0, 0); ///< 眼圈颜色（alpha 0 表示不画，熊猫/虎）
    QColor foreheadStripes = QColor(0, 0, 0, 0); ///< 额头虎斑条纹颜色（alpha 0 表示不画，猫/虎）
    QColor muzzleMask = QColor(0, 0, 0, 0); ///< 口鼻奶油面罩颜色（alpha 0 表示不画，柴犬/牛/马/猴）
    QColor browSpots = QColor(0, 0, 0, 0); ///< 眼上豆眉圆点颜色（alpha 0 表示不画，柴犬）
    enum class Tail { None, Cat, Curl } tail = Tail::None; ///< 尾巴：无 / 猫直尾 / 柴犬卷尾
    enum class Horns { None, Bull, Deer, Curl } horns = Horns::None; ///< 头顶角：无 / 牛角 / 鹿角(龙) / 卷角(羊)
    bool comb = false;      ///< 是否画头顶红色鸡冠（鸡）
    bool snout = false;     ///< 是否画鼻吻椭圆+鼻孔（猪）
    bool forkTongue = false; ///< 是否画嘴下分叉信子（蛇）
    bool legless = false;   ///< 是否不画脚（蛇）
    bool mane = false;      ///< 是否画头顶鬃毛（马，色取 outline）
    bool highlight = true;  ///< 是否画白色眼睛高光
    bool flippers = false;  ///< 是否画两侧小翅膀（企鹅）
};

PetSpec catSpec() {
    PetSpec s;
    s.body = QColor(255, 206, 115);
    s.earFill = s.body;
    s.outline = QColor(217, 138, 43);
    s.belly = QColor(255, 231, 184);
    s.blush = QColor(255, 138, 128, 140);
    s.eye = QColor(40, 32, 24);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.foreheadStripes = QColor(217, 138, 43, 170);
    s.tail = PetSpec::Tail::Cat;
    return s;
}

PetSpec shibaSpec() {
    PetSpec s;
    s.body = QColor(232, 167, 99);
    s.earFill = s.body;
    s.outline = QColor(176, 111, 44);
    s.belly = QColor(251, 235, 210);
    s.blush = QColor(240, 144, 112, 140);
    s.eye = QColor(70, 53, 31);
    s.mouthInner = QColor(110, 52, 34);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.earInner = QColor(249, 224, 188);
    s.muzzleMask = QColor(251, 235, 210);
    s.browSpots = QColor(251, 235, 210);
    s.tail = PetSpec::Tail::Curl;
    return s;
}

PetSpec pandaSpec() {
    PetSpec s;
    s.body = QColor(252, 252, 252);
    s.earFill = QColor(46, 46, 46);
    s.outline = QColor(69, 69, 69);
    s.belly = QColor(240, 240, 240);
    s.blush = QColor(255, 154, 162, 150);
    s.eye = QColor(252, 252, 252);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(46, 46, 46);
    s.smilePen = QColor(69, 69, 69);
    s.ear = PetSpec::Ear::Round;
    s.eyePatch = QColor(46, 46, 46);
    s.highlight = false;
    return s;
}

PetSpec bunnySpec() {
    PetSpec s;
    s.body = QColor(252, 252, 253);
    s.earFill = s.body;
    s.outline = QColor(184, 188, 201);
    s.belly = QColor(255, 233, 240);
    s.blush = QColor(255, 158, 181, 150);
    s.eye = QColor(58, 63, 82);
    s.mouthInner = QColor(199, 112, 128);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.earInner = QColor(255, 199, 212);
    s.ear = PetSpec::Ear::Long;
    return s;
}

PetSpec penguinSpec() {
    PetSpec s;
    s.body = QColor(78, 90, 107);
    s.earFill = s.body;
    s.outline = QColor(48, 56, 68);
    s.belly = QColor(245, 247, 250);
    s.blush = QColor(255, 138, 128, 120);
    s.eye = QColor(245, 247, 250);
    s.mouthInner = QColor(36, 32, 38);
    s.foot = QColor(240, 162, 74);
    s.smilePen = QColor(240, 162, 74);
    s.ear = PetSpec::Ear::None;
    s.highlight = false;
    s.flippers = true;
    return s;
}

PetSpec frogSpec() {
    PetSpec s;
    s.body = QColor(127, 200, 110);
    s.earFill = s.body;
    s.outline = QColor(76, 130, 67);
    s.belly = QColor(223, 242, 206);
    s.blush = QColor(247, 156, 141, 140);
    s.eye = QColor(36, 54, 31);
    s.mouthInner = QColor(94, 58, 66);
    s.foot = QColor(111, 179, 95);
    s.smilePen = s.eye;
    s.ear = PetSpec::Ear::None;
    return s;
}

// ===== 十二生肖（兔/狗归入生肖组：复用 Bunny/Shiba 造型，仅显示名改为"兔/狗"；以下为新绘制的十种） =====

PetSpec mouseSpec() {
    PetSpec s;
    s.body = QColor(160, 160, 170);
    s.earFill = s.body;
    s.outline = QColor(104, 104, 116);
    s.belly = QColor(214, 214, 222);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(36, 32, 40);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(238, 186, 192);
    s.smilePen = s.eye;
    s.ear = PetSpec::Ear::Round;
    s.earInner = QColor(240, 186, 192);
    s.tail = PetSpec::Tail::Cat;
    return s;
}

PetSpec oxSpec() {
    PetSpec s;
    s.body = QColor(247, 245, 238);
    s.earFill = QColor(88, 78, 72);
    s.outline = QColor(122, 110, 100);
    s.belly = QColor(250, 248, 242);
    s.blush = QColor(240, 144, 112, 120);
    s.eye = QColor(48, 40, 34);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(105, 95, 88);
    s.smilePen = s.eye;
    s.muzzleMask = QColor(244, 200, 192);
    s.horns = PetSpec::Horns::Bull;
    return s;
}

PetSpec tigerSpec() {
    PetSpec s;
    s.body = QColor(238, 150, 58);
    s.earFill = QColor(50, 38, 30);
    s.outline = QColor(140, 80, 30);
    s.belly = QColor(250, 224, 178);
    s.blush = QColor(240, 144, 112, 140);
    s.eye = QColor(40, 30, 22);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.foreheadStripes = QColor(40, 30, 20, 205);
    s.eyePatch = QColor(255, 250, 240, 180);
    s.ear = PetSpec::Ear::Round;
    s.tail = PetSpec::Tail::Cat;
    return s;
}

PetSpec dragonSpec() {
    PetSpec s;
    s.body = QColor(108, 188, 148);
    s.earFill = s.body;
    s.outline = QColor(58, 130, 95);
    s.belly = QColor(250, 222, 130);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(36, 42, 34);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(88, 164, 126);
    s.smilePen = s.eye;
    s.ear = PetSpec::Ear::None;
    s.horns = PetSpec::Horns::Deer;
    s.tail = PetSpec::Tail::Cat;
    return s;
}

PetSpec snakeSpec() {
    PetSpec s;
    s.body = QColor(88, 158, 148);
    s.earFill = s.body;
    s.outline = QColor(52, 110, 104);
    s.belly = QColor(214, 236, 226);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(30, 44, 40);
    s.mouthInner = QColor(110, 52, 40);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.ear = PetSpec::Ear::None;
    s.forkTongue = true;
    s.legless = true;
    return s;
}

PetSpec horseSpec() {
    PetSpec s;
    s.body = QColor(194, 140, 92);
    s.earFill = s.body;
    s.outline = QColor(135, 90, 52);
    s.belly = QColor(232, 210, 182);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(44, 34, 26);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(120, 84, 54);
    s.smilePen = s.eye;
    s.muzzleMask = QColor(240, 228, 210);
    s.mane = true;
    s.tail = PetSpec::Tail::Cat;
    return s;
}

PetSpec goatSpec() {
    PetSpec s;
    s.body = QColor(240, 236, 226);
    s.earFill = QColor(206, 196, 180);
    s.outline = QColor(176, 164, 146);
    s.belly = QColor(252, 250, 244);
    s.blush = QColor(255, 158, 181, 140);
    s.eye = QColor(52, 48, 44);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(196, 186, 170);
    s.smilePen = s.eye;
    s.horns = PetSpec::Horns::Curl;
    return s;
}

PetSpec monkeySpec() {
    PetSpec s;
    s.body = QColor(170, 122, 84);
    s.earFill = QColor(150, 104, 70);
    s.outline = QColor(112, 76, 48);
    s.belly = QColor(220, 184, 146);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(40, 30, 22);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(150, 104, 70);
    s.smilePen = s.eye;
    s.ear = PetSpec::Ear::Round;
    s.earInner = QColor(250, 226, 188);
    s.muzzleMask = QColor(248, 224, 186);
    return s;
}

PetSpec roosterSpec() {
    PetSpec s;
    s.body = QColor(247, 204, 94);
    s.earFill = s.body;
    s.outline = QColor(188, 142, 48);
    s.belly = QColor(252, 231, 166);
    s.blush = QColor(240, 144, 112, 130);
    s.eye = QColor(42, 34, 24);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(238, 158, 74);
    s.smilePen = QColor(238, 158, 74);
    s.ear = PetSpec::Ear::None;
    s.comb = true;
    return s;
}

PetSpec pigSpec() {
    PetSpec s;
    s.body = QColor(245, 172, 178);
    s.earFill = QColor(230, 148, 156);
    s.outline = QColor(196, 116, 124);
    s.belly = QColor(252, 214, 216);
    s.blush = QColor(240, 120, 130, 130);
    s.eye = QColor(46, 34, 34);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(230, 148, 156);
    s.smilePen = s.eye;
    s.earInner = QColor(210, 128, 138);
    s.snout = true;
    s.tail = PetSpec::Tail::Curl;
    return s;
}

/**
 * @brief 获取样式对应的绘制规格
 * @author chiangyang
 */
const PetSpec &specFor(PetWindow::PetStyle style) {
    static const PetSpec specs[PetWindow::petStyleCount()] = {
        catSpec(), shibaSpec(), pandaSpec(), bunnySpec(), penguinSpec(), frogSpec(),
        mouseSpec(), oxSpec(), tigerSpec(), dragonSpec(), snakeSpec(), horseSpec(),
        goatSpec(), monkeySpec(), roosterSpec(), pigSpec()
    };
    return specs[qBound(0, int(style), PetWindow::petStyleCount() - 1)];
}

/**
 * @brief 在 96 基准坐标系中绘制宠物本体（窗口 paintEvent 与样式预览共用）
 * @param p 画笔（调用方已完成 Antialiasing 与坐标缩放）
 * @param style 外观样式
 * @param mouthOpen 嘴张开程度 [0,1]
 * @param chewScale 咀嚼挤压系数（垂直方向）
 * @param eyesClosed 是否眨眼（眼睛画横线）
 * @param starProgress 左眼星星闪烁进度 [0,1]，0 为无星星
 * @author chiangyang
 */
void paintPetBody(QPainter &p, PetWindow::PetStyle style,
                  qreal mouthOpen, qreal chewScale,
                  bool eyesClosed, qreal starProgress) {
    const PetSpec &spec = specFor(style);

    // 地面软阴影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 32));
    p.drawEllipse(QRectF(18, 86, 60, 8));

    // 咀嚼挤压变换（绕身体中心，水平反向补偿保持体积感）
    p.save();
    p.translate(kBodyCenter);
    const qreal sx = 1.0 + (1.0 - chewScale) * 0.6;
    p.scale(sx, chewScale);
    p.translate(-kBodyCenter);

    const QPen outlinePen(spec.outline, 2);

    // 尾巴（最底层，身体盖住尾根；两遍描线模拟描边：外层描边色粗、内层体色细）
    switch (spec.tail) {
    case PetSpec::Tail::Cat: {
        // 猫直尾：左后方伸出向上翘的弯条
        QPainterPath catTail;
        catTail.moveTo(26, 74);
        catTail.quadTo(3, 84, 7, 59);
        p.setPen(QPen(spec.outline, 9, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(catTail);
        p.setPen(QPen(spec.body, 5.5, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(catTail);
        break;
    }
    case PetSpec::Tail::Curl: {
        // 柴犬卷尾：右上后方的圆环
        p.setPen(QPen(spec.outline, 11, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(83, 30), 8, 8);
        p.setPen(QPen(spec.body, 7.5, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QPointF(83, 30), 8, 8);
        break;
    }
    case PetSpec::Tail::None:
        break;
    }

    // 头顶角（牛/龙/羊，身体后层：两遍描线/描环模拟描边，角色固定）
    switch (spec.horns) {
    case PetSpec::Horns::Bull: {
        const QColor horn(222, 206, 180);
        p.setBrush(Qt::NoBrush);
        QPainterPath left, right;
        left.moveTo(25, 21);
        left.quadTo(19, 12, 17, 5);
        right.moveTo(71, 21);
        right.quadTo(77, 12, 79, 5);
        p.setPen(QPen(spec.outline, 7, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(left);
        p.drawPath(right);
        p.setPen(QPen(horn, 4, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(left);
        p.drawPath(right);
        break;
    }
    case PetSpec::Horns::Deer: {
        const QColor horn(218, 175, 96);
        QPainterPath left, right;
        left.moveTo(36, 19);
        left.lineTo(33, 4);
        left.moveTo(34, 11);
        left.lineTo(27, 6);
        right.moveTo(60, 19);
        right.lineTo(63, 4);
        right.moveTo(62, 11);
        right.lineTo(69, 6);
        p.setPen(QPen(spec.outline, 5.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(left);
        p.drawPath(right);
        p.setPen(QPen(horn, 2.8, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(left);
        p.drawPath(right);
        break;
    }
    case PetSpec::Horns::Curl: {
        const QColor horn(172, 140, 96);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(spec.outline, 9, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QPointF(28, 12), 5, 5);
        p.drawEllipse(QPointF(68, 12), 5, 5);
        p.setPen(QPen(horn, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QPointF(28, 12), 5, 5);
        p.drawEllipse(QPointF(68, 12), 5, 5);
        break;
    }
    case PetSpec::Horns::None:
        break;
    }

    // 鸡冠（鸡：头顶三圆锯齿，身体盖住下半融合）
    if (spec.comb) {
        p.setPen(QPen(spec.outline, 1.5));
        p.setBrush(QColor(228, 78, 68));
        p.drawEllipse(QPointF(39, 13), 4.5, 4.5);
        p.drawEllipse(QPointF(48, 10), 4.5, 4.5);
        p.drawEllipse(QPointF(57, 13), 4.5, 4.5);
    }

    // 鬃毛（马：头顶三撮短弧，色取描边色）
    if (spec.mane) {
        p.setPen(QPen(spec.outline, 4.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath mane;
        mane.moveTo(33, 22);
        mane.quadTo(28, 14, 28, 8);
        mane.moveTo(43, 19);
        mane.quadTo(40, 12, 39, 5);
        mane.moveTo(53, 18);
        mane.quadTo(51, 12, 50, 5);
        p.drawPath(mane);
    }

    // 小翅膀（企鹅：先画，身体盖住翅根）
    if (spec.flippers) {
        p.setPen(outlinePen);
        p.setBrush(spec.body);
        p.save();
        p.translate(13, 54);
        p.rotate(18);
        p.drawEllipse(QRectF(-6, -14, 12, 28));
        p.restore();
        p.save();
        p.translate(83, 54);
        p.rotate(-18);
        p.drawEllipse(QRectF(-6, -14, 12, 28));
        p.restore();
    }

    // 耳朵（先画，身体盖住耳根）
    p.setPen(outlinePen);
    p.setBrush(spec.earFill);
    switch (spec.ear) {
    case PetSpec::Ear::Triangle: {
        QPainterPath leftEar;
        leftEar.moveTo(22, 26);
        leftEar.lineTo(30, 6);
        leftEar.lineTo(44, 20);
        leftEar.closeSubpath();
        p.drawPath(leftEar);
        QPainterPath rightEar;
        rightEar.moveTo(52, 20);
        rightEar.lineTo(66, 6);
        rightEar.lineTo(74, 26);
        rightEar.closeSubpath();
        p.drawPath(rightEar);
        // 耳内（各顶点向耳道重心内缩的小三角）
        if (spec.earInner.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(spec.earInner);
            auto innerTri = [](QPainterPath &path, const QPointF &a,
                               const QPointF &b, const QPointF &c) {
                const QPointF g = (a + b + c) / 3.0;
                path.moveTo(a + (g - a) * 0.45);
                path.lineTo(b + (g - b) * 0.45);
                path.lineTo(c + (g - c) * 0.45);
                path.closeSubpath();
            };
            QPainterPath leftInner, rightInner;
            innerTri(leftInner, QPointF(22, 26), QPointF(30, 6), QPointF(44, 20));
            innerTri(rightInner, QPointF(52, 20), QPointF(66, 6), QPointF(74, 26));
            p.drawPath(leftInner);
            p.drawPath(rightInner);
        }
        break;
    }
    case PetSpec::Ear::Round:
        p.drawEllipse(QPointF(29, 15), 10.5, 10.5);
        p.drawEllipse(QPointF(67, 15), 10.5, 10.5);
        // 耳内（鼠/虎/猴：同心小圆）
        if (spec.earInner.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(spec.earInner);
            p.drawEllipse(QPointF(29, 15), 5.5, 5.5);
            p.drawEllipse(QPointF(67, 15), 5.5, 5.5);
        }
        break;
    case PetSpec::Ear::Long:
        // 兔耳：上竖长椭圆微外撇
        p.save();
        p.translate(33, 13);
        p.rotate(-10);
        p.drawEllipse(QRectF(-7, -13, 14, 26));
        p.restore();
        p.save();
        p.translate(63, 13);
        p.rotate(10);
        p.drawEllipse(QRectF(-7, -13, 14, 26));
        p.restore();
        if (spec.earInner.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(spec.earInner);
            p.save();
            p.translate(33, 13);
            p.rotate(-10);
            p.drawEllipse(QRectF(-3.4, -9, 6.8, 16));
            p.restore();
            p.save();
            p.translate(63, 13);
            p.rotate(10);
            p.drawEllipse(QRectF(-3.4, -9, 6.8, 16));
            p.restore();
        }
        break;
    case PetSpec::Ear::None:
        break;
    }

    // 身体
    p.setPen(outlinePen);
    p.setBrush(spec.body);
    p.drawEllipse(QRectF(10, 16, 76, 72));

    // 额头虎斑条纹（猫：三道短竖弧，中间长两侧短，位于额头中线避开两耳）
    if (spec.foreheadStripes.alpha() > 0) {
        p.setPen(QPen(spec.foreheadStripes, 2.2, Qt::SolidLine, Qt::RoundCap));
        auto stripe = [&p](const QPointF &top, const QPointF &bottom) {
            QPainterPath path;
            path.moveTo(top);
            path.quadTo(QPointF((top.x() + bottom.x()) / 2 + 1.5,
                                (top.y() + bottom.y()) / 2),
                        bottom);
            p.drawPath(path);
        };
        stripe(QPointF(48, 24), QPointF(48, 36));
        stripe(QPointF(40, 28), QPointF(40, 37));
        stripe(QPointF(56, 28), QPointF(56, 37));
    }

    // 口鼻奶油面罩（柴犬：覆盖嘴部与下颊，眼睛以上透出体色）
    if (spec.muzzleMask.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(spec.muzzleMask);
        p.drawEllipse(QPointF(48, 60), 17, 13);
    }

    // 肚皮
    p.setPen(Qt::NoPen);
    p.setBrush(spec.belly);
    p.drawEllipse(QRectF(28, 56, 40, 26));

    // 脚（蛇无脚）
    if (!spec.legless) {
        p.setPen(outlinePen);
        p.setBrush(spec.foot);
        p.drawEllipse(QRectF(26, 82, 16, 9));
        p.drawEllipse(QRectF(54, 82, 16, 9));
    }

    // 眼睛（眨眼时画横线；左眼在星星闪烁期间画星星）
    const bool withPatch = spec.eyePatch.alpha() > 0;
    if (withPatch) {
        p.setPen(Qt::NoPen);
        p.setBrush(spec.eyePatch);
        p.drawEllipse(QPointF(34, 46), 8.5, 9.5);
        p.drawEllipse(QPointF(62, 46), 8.5, 9.5);
    }
    p.setPen(QPen(spec.eye, 2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(spec.eye);
    if (eyesClosed) {
        p.drawLine(QPointF(29, 46), QPointF(39, 46));
        p.drawLine(QPointF(57, 46), QPointF(67, 46));
    } else {
        // 左眼：星星闪烁期间替换为金色四角星
        if (starProgress > 0.0) {
            const qreal t = starProgress;
            // 随进度从 0 长到最大再缩回 0，并旋转 120°
            const qreal scale = qSin(t * M_PI);
            p.save();
            p.translate(34, 46);
            p.rotate(t * 120.0);
            p.scale(scale, scale);
            QPainterPath star;
            constexpr qreal kOuter = 9.0, kInner = 2.8;
            for (int i = 0; i < 8; ++i) {
                const qreal radius = (i % 2 == 0) ? kOuter : kInner;
                const qreal angle = M_PI / 4 * i - M_PI / 2;
                const QPointF pt(radius * qCos(angle), radius * qSin(angle));
                if (i == 0) star.moveTo(pt); else star.lineTo(pt);
            }
            star.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 215, 64));
            p.drawPath(star);
            p.restore();
        } else {
            p.drawEllipse(QPointF(34, 46), 5, 5);
        }
        p.setBrush(spec.eye);
        p.drawEllipse(QPointF(62, 46), 5, 5);
        // 眼睛高光
        if (spec.highlight) {
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            if (starProgress <= 0.0) {
                p.drawEllipse(QPointF(35.5, 44.5), 1.6, 1.6);
            }
            p.drawEllipse(QPointF(63.5, 44.5), 1.6, 1.6);
        }
    }

    // 豆眉（柴犬：眼睛上方的奶油色圆点）
    if (spec.browSpots.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(spec.browSpots);
        p.drawEllipse(QPointF(34, 38.5), 2.2, 2.2);
        p.drawEllipse(QPointF(62, 38.5), 2.2, 2.2);
    }

    // 猪鼻（两眼之间：深粉椭圆 + 两个鼻孔）
    if (spec.snout) {
        p.setPen(QPen(spec.outline, 1.5));
        p.setBrush(QColor(230, 128, 140));
        p.drawEllipse(QPointF(48, 56), 8, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(spec.outline);
        p.drawEllipse(QPointF(45, 56), 1.3, 1.3);
        p.drawEllipse(QPointF(51, 56), 1.3, 1.3);
    }

    // 腮红
    p.setPen(Qt::NoPen);
    p.setBrush(spec.blush);
    p.drawEllipse(QRectF(24, 56, 11, 6));
    p.drawEllipse(QRectF(61, 56, 11, 6));

    // 嘴：闭嘴微笑弧线 ↔ 张嘴含舌椭圆，按 mouthOpen 插值
    if (mouthOpen < 0.05) {
        p.setPen(QPen(spec.smilePen, 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath smile;
        smile.moveTo(42, 58);
        smile.quadTo(48, 63, 54, 58);
        p.drawPath(smile);
    } else {
        const qreal w = 10 + 18 * mouthOpen;
        const qreal h = (4 + 18 * mouthOpen) / 2;
        QRectF mouthRect(kMouthCenter.x() - w / 2, kMouthCenter.y() - h, w, h * 2);
        p.setPen(QPen(spec.outline, 1.5));
        p.setBrush(spec.mouthInner);
        p.drawEllipse(mouthRect);
        // 舌头
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 138, 128));
        QRectF tongueRect(kMouthCenter.x() - w * 0.28,
                          kMouthCenter.y() + h * 0.15, w * 0.56, h * 0.7);
        p.drawEllipse(mouthRect.intersected(tongueRect));
    }

    // 分叉信子（蛇：嘴下伸出的红色分叉线，闭嘴/张嘴都画）
    if (spec.forkTongue) {
        p.setPen(QPen(QColor(255, 120, 110), 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath tongue;
        tongue.moveTo(48, 64);
        tongue.lineTo(48, 69);
        tongue.moveTo(48, 69);
        tongue.lineTo(45, 73);
        tongue.moveTo(48, 69);
        tongue.lineTo(51, 73);
        p.drawPath(tongue);
    }

    p.restore();
}
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
#ifdef Q_OS_MACOS
    // macOS 上 Qt::Tool 映射为 NSPanel，默认应用失活（点击其他应用/桌面）即被系统隐藏；
    // 宠物需常驻桌面，此属性关掉该行为
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(petSize(), petSize());
    setCursor(Qt::OpenHandCursor);

    scheduleBlink();
}

/**
 * @brief 渲染某样式的静态预览图（设置页样式选择下拉框图标用）
 * @author chiangyang
 */
QPixmap PetWindow::renderStylePreview(PetStyle style, int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 与窗口同一 96 基准坐标系绘制，保证预览与实际外观一致
    const qreal s = size / 96.0;
    p.scale(s, s);
    paintPetBody(p, style, 0.0, 1.0, false, 0.0);
    return pix;
}

/**
 * @brief 切换宠物外观样式（立即重绘）
 * @author chiangyang
 */
void PetWindow::setStyle(PetStyle style) {
    if (m_style == style) {
        return;
    }
    m_style = style;
    update();
}

/**
 * @brief 嘴部中心的全局屏幕坐标
 * @author chiangyang
 */
QPoint PetWindow::mouthCenterGlobal() const {
    // 窗口边长按 DPI 缩放，96 为绘制基准坐标系，嘴部坐标随之等比放大
    const qreal s = width() / 96.0;
    return mapToGlobal(QPointF((kBodyCenter + QPointF(0.0, 8.0)) * s).toPoint());
}

/**
 * @brief 设置嘴张开程度
 * @author chiangyang
 */
void PetWindow::setMouthOpen(qreal v) {
    m_mouthOpen = v;
    update();
}

/**
 * @brief 设置咀嚼挤压系数
 * @author chiangyang
 */
void PetWindow::setChewScale(qreal v) {
    m_chewScale = v;
    update();
}

/**
 * @brief 设置星星闪烁进度
 * @author chiangyang
 */
void PetWindow::setStarProgress(qreal v) {
    m_starProgress = v;
    update();
}

/**
 * @brief 张嘴等待食物
 * @author chiangyang
 */
void PetWindow::openMouth() {
    // 停掉进行中的嘴部/咀嚼动画，避免两个动画同时驱动 mouthOpen 互相覆写
    if (m_mouthAnim) {
        m_mouthAnim->stop(); // DeleteWhenStopped，stop 即销毁
    }
    QPropertyAnimation *anim = new QPropertyAnimation(this, "mouthOpen", this);
    anim->setDuration(180);
    anim->setStartValue(m_mouthOpen);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutBack);
    m_mouthAnim = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 咀嚼吞下：嘴巴反复张合咀嚼（3 次，拉长咀嚼感），
 *        身体挤压回弹保持原时长并与嘴巴并行，完成后左眼闪星星
 * @author chiangyang
 */
void PetWindow::chew() {
    // 停掉进行中的嘴部/咀嚼动画，避免两个动画同时驱动 mouthOpen 互相覆写
    if (m_mouthAnim) {
        m_mouthAnim->stop(); // DeleteWhenStopped，stop 即销毁
    }
    // 嘴巴咀嚼：先含住食物，再反复张合 3 次（总时长约 870ms）
    auto *mouthSeq = new QSequentialAnimationGroup;
    QPropertyAnimation *closeMouth = new QPropertyAnimation(this, "mouthOpen");
    closeMouth->setDuration(120);
    closeMouth->setStartValue(m_mouthOpen);
    closeMouth->setEndValue(0.0);
    mouthSeq->addAnimation(closeMouth);
    for (int i = 0; i < 3; ++i) {
        QPropertyAnimation *open = new QPropertyAnimation(this, "mouthOpen");
        open->setDuration(120);
        open->setStartValue(0.0);
        open->setEndValue(0.65);
        mouthSeq->addAnimation(open);
        QPropertyAnimation *close = new QPropertyAnimation(this, "mouthOpen");
        close->setDuration(130);
        close->setStartValue(0.65);
        close->setEndValue(0.0);
        mouthSeq->addAnimation(close);
    }

    // 身体挤压回弹（保持原来的时长）：1.0 → 0.82 → 1.08 → 1.0
    QPropertyAnimation *squish = new QPropertyAnimation(this, "chewScale");
    squish->setDuration(360);
    squish->setKeyValueAt(0.0, 1.0);
    squish->setKeyValueAt(0.35, 0.82);
    squish->setKeyValueAt(0.7, 1.08);
    squish->setKeyValueAt(1.0, 1.0);

    // 嘴巴咀嚼与身体回弹并行，全部完成后闪星星
    auto *group = new QParallelAnimationGroup(this);
    group->addAnimation(mouthSeq);
    group->addAnimation(squish);
    connect(group, &QParallelAnimationGroup::finished, this, &PetWindow::sparkleStar);
    m_mouthAnim = group;
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 左眼星星闪烁动画：星星从小到大旋转闪现再消失（约 800ms）
 * @author chiangyang
 */
void PetWindow::sparkleStar() {
    QPropertyAnimation *anim = new QPropertyAnimation(this, "starProgress", this);
    anim->setDuration(800);
    anim->setStartValue(0.01); // 0 表示无星星，从非 0 开始
    anim->setEndValue(1.0);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        // 闪完复位，恢复正常眨眼状态
        m_starProgress = 0.0;
        update();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

/**
 * @brief 调度下一次眨眼（星星闪烁期间自动跳过）
 * @author chiangyang
 */
void PetWindow::scheduleBlink() {
    const int interval = 2400 + QRandomGenerator::global()->bounded(2600);
    QTimer::singleShot(interval, this, [this]() {
        if (m_starProgress > 0.0) {
            // 星星闪烁中，跳过本次眨眼
            scheduleBlink();
            return;
        }
        m_eyesClosed = true;
        update();
        QTimer::singleShot(160, this, [this]() {
            m_eyesClosed = false;
            update();
            scheduleBlink();
        });
    });
}

/**
 * @brief 绘制宠物
 * @author chiangyang
 */
void PetWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 窗口边长按 DPI 缩放（96 为绘制基准坐标系），整体等比放大
    const qreal s = width() / 96.0;
    p.scale(s, s);

    paintPetBody(p, m_style, m_mouthOpen, m_chewScale, m_eyesClosed, m_starProgress);
}

/**
 * @brief 鼠标按下：记录拖动起点
 * @author chiangyang
 */
void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_pressing = true;
        m_dragging = false;
        m_dragOffset = event->pos();
        m_pressGlobalPos = event->globalPosition().toPoint();
    }
    QWidget::mousePressEvent(event);
}

/**
 * @brief 鼠标移动：超阈值后按拖动处理
 * @author chiangyang
 */
void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_pressing && (event->buttons() & Qt::LeftButton)) {
        // 拖动阈值随 DPI 缩放（物理像素坐标模型下固定 8px 在高分屏上偏小）
        const int threshold = PetUi::scaled(kDragThreshold);
        if (!m_dragging
            && (event->globalPosition().toPoint() - m_pressGlobalPos).manhattanLength() > threshold) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragging) {
            move(event->globalPosition().toPoint() - m_dragOffset);
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief 鼠标释放：区分点击与拖动结束
 * @author chiangyang
 */
void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_pressing) {
        if (m_dragging) {
            emit dragFinished(pos());
        } else {
            emit clicked();
        }
    }
    m_pressing = false;
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    QWidget::mouseReleaseEvent(event);
}

/**
 * @brief 悬停进入：提示可拖动
 * @author chiangyang
 */
void PetWindow::enterEvent(QEnterEvent *event) {
    setCursor(Qt::OpenHandCursor);
    QWidget::enterEvent(event);
}

/**
 * @brief 悬停离开：恢复默认光标
 * @author chiangyang
 */
void PetWindow::leaveEvent(QEvent *event) {
    if (!m_dragging) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}
