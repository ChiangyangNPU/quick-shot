#include "PetStyles.h"

PetSpec PetStyles::snake() {
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
