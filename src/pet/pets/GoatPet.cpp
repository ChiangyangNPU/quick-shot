#include "PetStyles.h"

PetSpec PetStyles::goat() {
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
