#include "PetStyles.h"

PetSpec PetStyles::rooster() {
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
