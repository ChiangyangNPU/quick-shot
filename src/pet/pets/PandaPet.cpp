#include "PetStyles.h"

PetSpec PetStyles::panda() {
    PetSpec s;
    s.body = QColor(252, 252, 252);
    s.earFill = QColor(46, 46, 46);
    s.outline = QColor(69, 69, 69);
    s.belly = QColor(240, 240, 240);
    s.blush = QColor(255, 154, 162, 150);
    s.eye = QColor(252, 252, 252);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(46, 46, 46);
    s.smilePen = QColor(69, 69, 69);
    s.ear = PetSpec::Ear::Round;
    s.eyePatch = QColor(46, 46, 46);
    s.highlight = false;
    return s;
}
