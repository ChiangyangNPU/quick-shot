#include "PetStyles.h"

PetSpec PetStyles::cat() {
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
