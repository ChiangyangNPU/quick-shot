#include "PetStyles.h"

PetSpec PetStyles::pig() {
    PetSpec s;
    s.body = QColor(245, 172, 178);
    s.earFill = QColor(230, 148, 156);
    s.outline = QColor(196, 116, 124);
    s.belly = QColor(252, 214, 216);
    s.blush = QColor(240, 120, 130, 130);
    s.eye = QColor(46, 34, 34);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(230, 148, 156);
    s.smilePen = s.eye;
    s.earInner = QColor(210, 128, 138);
    s.snout = true;
    s.tail = PetSpec::Tail::Curl;
    return s;
}
