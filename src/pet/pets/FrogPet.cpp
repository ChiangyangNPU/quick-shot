#include "PetStyles.h"

PetSpec PetStyles::frog() {
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
