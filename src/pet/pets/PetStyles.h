#ifndef PETSTYLES_H
#define PETSTYLES_H

#include <QColor>

/**
 * @brief 宠物外观规格：描述一个样式在 96 基准坐标系里的配色与特征部件
 *
 * 形体骨架（身体/肚皮/脚/眼睛/嘴的位置与动画）全样式共用，
 * 样式差异体现在配色、耳朵/尾巴形状与特征部件（眼圈/虎斑/面罩/豆眉/
 * 小翅膀/角/鸡冠/猪鼻/蛇信/马鬃）上。
 * @author chiangyang
 */
struct PetSpec {
    QColor body;            ///< 身体主色
    QColor earFill;         ///< 耳朵填充
    QColor outline;         ///< 描边
    QColor belly;           ///< 肚皮
    QColor blush;           ///< 腮红（含 alpha）
    QColor eye;             ///< 眼睛瞳色 / 眨眼横线颜色
    QColor mouthInner;      ///< 张嘴口腔
    QColor foot;            ///< 脚
    QColor smilePen;        ///< 闭嘴微笑弧线颜色
    QColor earInner = QColor(0, 0, 0, 0); ///< 耳内填充（alpha 0 表示不画）
    enum class Ear { Triangle, Round, Long, None } ear = Ear::Triangle;
    QColor eyePatch = QColor(0, 0, 0, 0); ///< 眼圈颜色（alpha 0 表示不画，熊猫/虎）
    QColor foreheadStripes = QColor(0, 0, 0, 0); ///< 额头虎斑条纹颜色（alpha 0 表示不画，猫/虎）
    QColor muzzleMask = QColor(0, 0, 0, 0); ///< 口鼻奶油面罩颜色（alpha 0 表示不画，柴犬/牛/马/猴）
    QColor browSpots = QColor(0, 0, 0, 0); ///< 眼上豆眉圆点颜色（alpha 0 表示不画，柴犬）
    enum class Tail { None, Cat, Curl } tail = Tail::None; ///< 尾巴：无 / 猫直尾 / 柴犬卷尾
    enum class Horns { None, Bull, Deer, Curl } horns = Horns::None; ///< 头顶角：无 / 牛角 / 鹿角(龙) / 卷角(羊)
    bool comb = false;      ///< 是否画头顶红色鸡冠（鸡）
    bool snout = false;     ///< 是否画鼻吻椭圆+鼻孔（猪）
    bool forkTongue = false; ///< 是否画嘴下分叉信子（蛇）
    bool legless = false;   ///< 是否不画脚（蛇）
    bool mane = false;      ///< 是否画头顶鬃毛（马，色取 outline）
    bool highlight = true;  ///< 是否画白色眼睛高光
    bool flippers = false;  ///< 是否画两侧小翅膀（企鹅）
};

/**
 * @brief 内置宠物样式的规格工厂
 *
 * 每个样式由独立源文件定义（src/pet/pets/ 目录下每个样式一个 cpp），此处只做声明，
 * 供 PetWindow 的 specFor 表按 PetStyle 枚举值组装。
 * @author chiangyang
 */
namespace PetStyles {

PetSpec cat();
PetSpec shiba();
PetSpec panda();
PetSpec bunny();
PetSpec penguin();
PetSpec frog();
PetSpec mouse();
PetSpec ox();
PetSpec tiger();
PetSpec dragon();
PetSpec snake();
PetSpec horse();
PetSpec goat();
PetSpec monkey();
PetSpec rooster();
PetSpec pig();

} // namespace PetStyles

#endif // PETSTYLES_H
