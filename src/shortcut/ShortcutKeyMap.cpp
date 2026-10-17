#include "ShortcutKeyMap.h"

#include <QHash>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#endif

namespace ShortcutKeyMap {

#ifdef Q_OS_WIN

/**
 * @brief Qt 键 → Windows 虚拟键码
 *
 * 字母/数字的 Qt::Key 与 VK 同值；F1-F24 的 VK 连续（0x70-0x87）可算术映射；
 * 标点键经 OEM 虚拟键码表转换（解决反引号等无法注册的问题）。
 * @author chiangyang
 */
int qtKeyToVirtualKeyCode(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return key;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return key;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return VK_F1 + (key - Qt::Key_F1);
    }

    // 常用标点键映射表（OEM 虚拟键码）
    static const QHash<int, UINT> extraKeyMap = {
        { Qt::Key_QuoteLeft,   VK_OEM_3 },      // ` 反引号
        { Qt::Key_Minus,       VK_OEM_MINUS },  // - 减号
        { Qt::Key_Equal,       VK_OEM_PLUS },   // = 等号
        { Qt::Key_BracketLeft,  VK_OEM_4 },     // [ 左方括号
        { Qt::Key_BracketRight, VK_OEM_6 },     // ] 右方括号
        { Qt::Key_Semicolon,   VK_OEM_1 },      // ; 分号
        { Qt::Key_Apostrophe,  VK_OEM_7 },      // ' 单引号
        { Qt::Key_Comma,       VK_OEM_COMMA },  // , 逗号
        { Qt::Key_Period,      VK_OEM_PERIOD }, // . 句号
        { Qt::Key_Slash,       VK_OEM_2 },      // / 斜杠
        { Qt::Key_Backslash,   VK_OEM_5 },      // \ 反斜杠
    };
    auto it = extraKeyMap.find(key);
    if (it != extraKeyMap.end()) {
        return it.value();
    }
    return -1;
}

#elif defined(Q_OS_MACOS)

/**
 * @brief Qt 键 → macOS Carbon 虚拟键码
 *
 * 字母/数字/标点经 ANSI 布局映射表；F 键注意 Carbon 的 kVK_F* 宏
 * **不连续**（kVK_F1=0x7A, kVK_F2=0x78, kVK_F3=0x63…，旧实现的
 * kVK_F1+(n-1) 算术映射 F2-F24 全部错键），必须显式查表；
 * Events.h 仅定义到 kVK_F20，F21-F24 无对应键码 → 不支持。
 * @author chiangyang
 */
int qtKeyToVirtualKeyCode(Qt::Key key) {
    // 字母/数字/标点（Qt::Key → kVK_ANSI_*，与 Carbon 一致）
    static const QHash<int, int> keyMapping = {
        // 字母键
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_S, kVK_ANSI_S},
        {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_F, kVK_ANSI_F},
        {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_G, kVK_ANSI_G},
        {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_V, kVK_ANSI_V},
        {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_Q, kVK_ANSI_Q},
        {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_E, kVK_ANSI_E},
        {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_Y, kVK_ANSI_Y},
        {Qt::Key_T, kVK_ANSI_T}, {Qt::Key_O, kVK_ANSI_O},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_I, kVK_ANSI_I},
        {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K},
        {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_M, kVK_ANSI_M},
        // 数字键
        {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2},
        {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4},
        {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6},
        {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8},
        {Qt::Key_9, kVK_ANSI_9}, {Qt::Key_0, kVK_ANSI_0},
        // 标点键
        {Qt::Key_QuoteLeft,   kVK_ANSI_Grave},
        {Qt::Key_Minus,       kVK_ANSI_Minus},
        {Qt::Key_Equal,       kVK_ANSI_Equal},
        {Qt::Key_BracketLeft, kVK_ANSI_LeftBracket},
        {Qt::Key_BracketRight,kVK_ANSI_RightBracket},
        {Qt::Key_Semicolon,   kVK_ANSI_Semicolon},
        {Qt::Key_Apostrophe,  kVK_ANSI_Quote},
        {Qt::Key_Comma,       kVK_ANSI_Comma},
        {Qt::Key_Period,      kVK_ANSI_Period},
        {Qt::Key_Slash,       kVK_ANSI_Slash},
        {Qt::Key_Backslash,   kVK_ANSI_Backslash},
    };

    if (keyMapping.contains(key)) {
        return keyMapping.value(key);
    }

    // F 键：显式查表（kVK_F* 不连续，见函数注释）
    static const QHash<int, int> fKeyMapping = {
        {Qt::Key_F1,  kVK_F1},  {Qt::Key_F2,  kVK_F2},
        {Qt::Key_F3,  kVK_F3},  {Qt::Key_F4,  kVK_F4},
        {Qt::Key_F5,  kVK_F5},  {Qt::Key_F6,  kVK_F6},
        {Qt::Key_F7,  kVK_F7},  {Qt::Key_F8,  kVK_F8},
        {Qt::Key_F9,  kVK_F9},  {Qt::Key_F10, kVK_F10},
        {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14},
        {Qt::Key_F15, kVK_F15}, {Qt::Key_F16, kVK_F16},
        {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18},
        {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20},
    };
    if (fKeyMapping.contains(key)) {
        return fKeyMapping.value(key);
    }

    return -1;
}

#else

int qtKeyToVirtualKeyCode(Qt::Key key) {
    Q_UNUSED(key);
    return -1;
}

#endif

} // namespace ShortcutKeyMap
