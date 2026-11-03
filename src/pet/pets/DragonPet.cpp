#include "PetStyles.h"

PetSpec PetStyles::dragon() {
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
