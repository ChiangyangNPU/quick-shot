#include "PetStyles.h"

PetSpec PetStyles::monkey() {
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
