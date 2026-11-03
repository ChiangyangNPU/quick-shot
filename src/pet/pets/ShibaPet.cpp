#include "PetStyles.h"

PetSpec PetStyles::shiba() {
    PetSpec s;
    s.body = QColor(232, 167, 99);
    s.earFill = s.body;
    s.outline = QColor(176, 111, 44);
    s.belly = QColor(251, 235, 210);
    s.blush = QColor(240, 144, 112, 140);
    s.eye = QColor(70, 53, 31);
    s.mouthInner = QColor(110, 52, 34);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.earInner = QColor(249, 224, 188);
    s.muzzleMask = QColor(251, 235, 210);
    s.browSpots = QColor(251, 235, 210);
    s.tail = PetSpec::Tail::Curl;
    return s;
}
