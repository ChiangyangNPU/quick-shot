#include "PetStyles.h"

PetSpec PetStyles::bunny() {
    PetSpec s;
    s.body = QColor(252, 252, 253);
    s.earFill = s.body;
    s.outline = QColor(184, 188, 201);
    s.belly = QColor(255, 233, 240);
    s.blush = QColor(255, 158, 181, 150);
    s.eye = QColor(58, 63, 82);
    s.mouthInner = QColor(199, 112, 128);
    s.foot = s.body;
    s.smilePen = s.eye;
    s.earInner = QColor(255, 199, 212);
    s.ear = PetSpec::Ear::Long;
    return s;
}
