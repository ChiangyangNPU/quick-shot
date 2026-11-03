#include "PetStyles.h"

PetSpec PetStyles::ox() {
    PetSpec s;
    s.body = QColor(247, 245, 238);
    s.earFill = QColor(88, 78, 72);
    s.outline = QColor(122, 110, 100);
    s.belly = QColor(250, 248, 242);
    s.blush = QColor(240, 144, 112, 120);
    s.eye = QColor(48, 40, 34);
    s.mouthInner = QColor(122, 46, 34);
    s.foot = QColor(105, 95, 88);
    s.smilePen = s.eye;
    s.muzzleMask = QColor(244, 200, 192);
    s.horns = PetSpec::Horns::Bull;
    return s;
}
