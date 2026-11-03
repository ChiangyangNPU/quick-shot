#include "PetStyles.h"

PetSpec PetStyles::penguin() {
    PetSpec s;
    s.body = QColor(78, 90, 107);
    s.earFill = s.body;
    s.outline = QColor(48, 56, 68);
    s.belly = QColor(245, 247, 250);
    s.blush = QColor(255, 138, 128, 120);
    s.eye = QColor(245, 247, 250);
    s.mouthInner = QColor(36, 32, 38);
    s.foot = QColor(240, 162, 74);
    s.smilePen = QColor(240, 162, 74);
    s.ear = PetSpec::Ear::None;
    s.highlight = false;
    s.flippers = true;
    return s;
}
