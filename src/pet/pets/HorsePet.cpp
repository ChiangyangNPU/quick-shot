#include "PetStyles.h"

PetSpec PetStyles::horse() {
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
