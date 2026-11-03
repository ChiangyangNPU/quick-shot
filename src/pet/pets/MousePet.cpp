#include "PetStyles.h"

// ===== 十二生肖（兔/狗归入生肖组：复用 Bunny/Shiba 造型，仅显示名改为"兔/狗"；以下为新绘制的十种） =====
PetSpec PetStyles::mouse() {
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
