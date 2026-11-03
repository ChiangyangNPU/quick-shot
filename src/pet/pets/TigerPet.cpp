#include "PetStyles.h"

PetSpec PetStyles::tiger() {
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
