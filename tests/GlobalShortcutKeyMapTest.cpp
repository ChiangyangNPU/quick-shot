#include <QtTest>
#include "shortcut/ShortcutKeyMap.h"

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif

/**
 * @brief GlobalShortcut 键码映射表（ShortcutKeyMap）的单元测试
 *
 * 映射表是纯数据：注册错键只在对应平台运行时暴露（键码写错只会让
 * 热键"永远不触发"，无任何报错）。本测试逐键断言 Qt::Key → 平台虚拟
 * 键码的映射，防止表被改坏：
 * - 期望值直接使用平台常量（mac kVK_*）或字面量（win 0x..），其中
 *   kVK_F* 为显式查表——Carbon 的 kVK_F* 宏不连续，旧实现的
 *   kVK_F1+(n-1) 算术映射 F2-F24 全部错键，此处固定回归。
 * - 期望值用字面量的键（如 Windows F 键 0x70-0x87）同时校验平台常量
 *   本身的稳定性。
 * 平台分支：mac 跑 kVK 表 + F21-F24 不支持；win 跑 VK 表。
 * @author chiangyang
 */
class GlobalShortcutKeyMapTest : public QObject {
    Q_OBJECT

private:
    struct Case {
        Qt::Key key;
        const char *name;
        int expected;
    };

    static QList<Case> letterCases() {
#ifdef Q_OS_WIN
        return {
            {Qt::Key_A, "A", 0x41}, {Qt::Key_B, "B", 0x42},
            {Qt::Key_C, "C", 0x43}, {Qt::Key_D, "D", 0x44},
            {Qt::Key_E, "E", 0x45}, {Qt::Key_F, "F", 0x46},
            {Qt::Key_G, "G", 0x47}, {Qt::Key_H, "H", 0x48},
            {Qt::Key_I, "I", 0x49}, {Qt::Key_J, "J", 0x4A},
            {Qt::Key_K, "K", 0x4B}, {Qt::Key_L, "L", 0x4C},
            {Qt::Key_M, "M", 0x4D}, {Qt::Key_N, "N", 0x4E},
            {Qt::Key_O, "O", 0x4F}, {Qt::Key_P, "P", 0x50},
            {Qt::Key_Q, "Q", 0x51}, {Qt::Key_R, "R", 0x52},
            {Qt::Key_S, "S", 0x53}, {Qt::Key_T, "T", 0x54},
            {Qt::Key_U, "U", 0x55}, {Qt::Key_V, "V", 0x56},
            {Qt::Key_W, "W", 0x57}, {Qt::Key_X, "X", 0x58},
            {Qt::Key_Y, "Y", 0x59}, {Qt::Key_Z, "Z", 0x5A},
        };
#else
        return {
            {Qt::Key_A, "A", kVK_ANSI_A}, {Qt::Key_B, "B", kVK_ANSI_B},
            {Qt::Key_C, "C", kVK_ANSI_C}, {Qt::Key_D, "D", kVK_ANSI_D},
            {Qt::Key_E, "E", kVK_ANSI_E}, {Qt::Key_F, "F", kVK_ANSI_F},
            {Qt::Key_G, "G", kVK_ANSI_G}, {Qt::Key_H, "H", kVK_ANSI_H},
            {Qt::Key_I, "I", kVK_ANSI_I}, {Qt::Key_J, "J", kVK_ANSI_J},
            {Qt::Key_K, "K", kVK_ANSI_K}, {Qt::Key_L, "L", kVK_ANSI_L},
            {Qt::Key_M, "M", kVK_ANSI_M}, {Qt::Key_N, "N", kVK_ANSI_N},
            {Qt::Key_O, "O", kVK_ANSI_O}, {Qt::Key_P, "P", kVK_ANSI_P},
            {Qt::Key_Q, "Q", kVK_ANSI_Q}, {Qt::Key_R, "R", kVK_ANSI_R},
            {Qt::Key_S, "S", kVK_ANSI_S}, {Qt::Key_T, "T", kVK_ANSI_T},
            {Qt::Key_U, "U", kVK_ANSI_U}, {Qt::Key_V, "V", kVK_ANSI_V},
            {Qt::Key_W, "W", kVK_ANSI_W}, {Qt::Key_X, "X", kVK_ANSI_X},
            {Qt::Key_Y, "Y", kVK_ANSI_Y}, {Qt::Key_Z, "Z", kVK_ANSI_Z},
        };
#endif
    }

    static QList<Case> digitCases() {
#ifdef Q_OS_WIN
        return {
            {Qt::Key_0, "0", 0x30}, {Qt::Key_1, "1", 0x31},
            {Qt::Key_2, "2", 0x32}, {Qt::Key_3, "3", 0x33},
            {Qt::Key_4, "4", 0x34}, {Qt::Key_5, "5", 0x35},
            {Qt::Key_6, "6", 0x36}, {Qt::Key_7, "7", 0x37},
            {Qt::Key_8, "8", 0x38}, {Qt::Key_9, "9", 0x39},
        };
#else
        return {
            {Qt::Key_0, "0", kVK_ANSI_0}, {Qt::Key_1, "1", kVK_ANSI_1},
            {Qt::Key_2, "2", kVK_ANSI_2}, {Qt::Key_3, "3", kVK_ANSI_3},
            {Qt::Key_4, "4", kVK_ANSI_4}, {Qt::Key_5, "5", kVK_ANSI_5},
            {Qt::Key_6, "6", kVK_ANSI_6}, {Qt::Key_7, "7", kVK_ANSI_7},
            {Qt::Key_8, "8", kVK_ANSI_8}, {Qt::Key_9, "9", kVK_ANSI_9},
        };
#endif
    }

    static QList<Case> punctuationCases() {
#ifdef Q_OS_WIN
        return {
            {Qt::Key_QuoteLeft,   "`", 0xC0}, // VK_OEM_3
            {Qt::Key_Minus,       "-", 0xBD}, // VK_OEM_MINUS
            {Qt::Key_Equal,       "=", 0xBB}, // VK_OEM_PLUS
            {Qt::Key_BracketLeft, "[", 0xDB}, // VK_OEM_4
            {Qt::Key_BracketRight,"]", 0xDD}, // VK_OEM_6
            {Qt::Key_Semicolon,   ";", 0xBA}, // VK_OEM_1
            {Qt::Key_Apostrophe,  "'", 0xDE}, // VK_OEM_7
            {Qt::Key_Comma,       ",", 0xBC}, // VK_OEM_COMMA
            {Qt::Key_Period,      ".", 0xBE}, // VK_OEM_PERIOD
            {Qt::Key_Slash,       "/", 0xBF}, // VK_OEM_2
            {Qt::Key_Backslash,   "\\", 0xDC}, // VK_OEM_5
        };
#else
        return {
            {Qt::Key_QuoteLeft,   "`", kVK_ANSI_Grave},
            {Qt::Key_Minus,       "-", kVK_ANSI_Minus},
            {Qt::Key_Equal,       "=", kVK_ANSI_Equal},
            {Qt::Key_BracketLeft, "[", kVK_ANSI_LeftBracket},
            {Qt::Key_BracketRight,"]", kVK_ANSI_RightBracket},
            {Qt::Key_Semicolon,   ";", kVK_ANSI_Semicolon},
            {Qt::Key_Apostrophe,  "'", kVK_ANSI_Quote},
            {Qt::Key_Comma,       ",", kVK_ANSI_Comma},
            {Qt::Key_Period,      ".", kVK_ANSI_Period},
            {Qt::Key_Slash,       "/", kVK_ANSI_Slash},
            {Qt::Key_Backslash,   "\\", kVK_ANSI_Backslash},
        };
#endif
    }

    static QList<Case> fKeyCases() {
#ifdef Q_OS_WIN
        // VK_F1-F24 连续（0x70-0x87），字面量防平台头文件变动
        return {
            {Qt::Key_F1,  "F1",  0x70}, {Qt::Key_F2,  "F2",  0x71},
            {Qt::Key_F3,  "F3",  0x72}, {Qt::Key_F4,  "F4",  0x73},
            {Qt::Key_F5,  "F5",  0x74}, {Qt::Key_F6,  "F6",  0x75},
            {Qt::Key_F7,  "F7",  0x76}, {Qt::Key_F8,  "F8",  0x77},
            {Qt::Key_F9,  "F9",  0x78}, {Qt::Key_F10, "F10", 0x79},
            {Qt::Key_F11, "F11", 0x7A}, {Qt::Key_F12, "F12", 0x7B},
            {Qt::Key_F13, "F13", 0x7C}, {Qt::Key_F14, "F14", 0x7D},
            {Qt::Key_F15, "F15", 0x7E}, {Qt::Key_F16, "F16", 0x7F},
            {Qt::Key_F17, "F17", 0x80}, {Qt::Key_F18, "F18", 0x81},
            {Qt::Key_F19, "F19", 0x82}, {Qt::Key_F20, "F20", 0x83},
            {Qt::Key_F21, "F21", 0x84}, {Qt::Key_F22, "F22", 0x85},
            {Qt::Key_F23, "F23", 0x86}, {Qt::Key_F24, "F24", 0x87},
        };
#else
        // Carbon kVK_F* 不连续，必须显式表；Events.h 仅定义到 F20
        return {
            {Qt::Key_F1,  "F1",  kVK_F1},  {Qt::Key_F2,  "F2",  kVK_F2},
            {Qt::Key_F3,  "F3",  kVK_F3},  {Qt::Key_F4,  "F4",  kVK_F4},
            {Qt::Key_F5,  "F5",  kVK_F5},  {Qt::Key_F6,  "F6",  kVK_F6},
            {Qt::Key_F7,  "F7",  kVK_F7},  {Qt::Key_F8,  "F8",  kVK_F8},
            {Qt::Key_F9,  "F9",  kVK_F9},  {Qt::Key_F10, "F10", kVK_F10},
            {Qt::Key_F11, "F11", kVK_F11}, {Qt::Key_F12, "F12", kVK_F12},
            {Qt::Key_F13, "F13", kVK_F13}, {Qt::Key_F14, "F14", kVK_F14},
            {Qt::Key_F15, "F15", kVK_F15}, {Qt::Key_F16, "F16", kVK_F16},
            {Qt::Key_F17, "F17", kVK_F17}, {Qt::Key_F18, "F18", kVK_F18},
            {Qt::Key_F19, "F19", kVK_F19}, {Qt::Key_F20, "F20", kVK_F20},
        };
#endif
    }

    static void assertCases(const QList<Case> &cases, const char *what) {
        for (const Case &c : cases) {
            const int actual = ShortcutKeyMap::qtKeyToVirtualKeyCode(c.key);
            QVERIFY2(actual == c.expected,
                     qPrintable(QString("%1 %2: expected 0x%3, got 0x%4")
                                .arg(what, c.name)
                                .arg(c.expected, 0, 16)
                                .arg(actual, 0, 16)));
        }
    }

private slots:
    void letters_mapToPlatformCodes() {
        assertCases(letterCases(), "letter");
    }

    void digits_mapToPlatformCodes() {
        assertCases(digitCases(), "digit");
    }

    void punctuation_mapToPlatformCodes() {
        assertCases(punctuationCases(), "punctuation");
    }

    void fKeys_mapToPlatformCodes() {
        assertCases(fKeyCases(), "F-key");
#ifdef Q_OS_MACOS
        // Carbon 仅定义到 kVK_F20，F21-F24 无法注册全局热键
        QCOMPARE(ShortcutKeyMap::qtKeyToVirtualKeyCode(Qt::Key_F21), -1);
        QCOMPARE(ShortcutKeyMap::qtKeyToVirtualKeyCode(Qt::Key_F22), -1);
        QCOMPARE(ShortcutKeyMap::qtKeyToVirtualKeyCode(Qt::Key_F23), -1);
        QCOMPARE(ShortcutKeyMap::qtKeyToVirtualKeyCode(Qt::Key_F24), -1);
#endif
    }

    void unsupportedKeys_returnMinus1() {
        // 非字母/数字/F 键/已知标点的键在两个平台都不可注册全局热键
        const QList<Qt::Key> unsupported = {
            Qt::Key_Tab, Qt::Key_Space, Qt::Key_Delete,
            Qt::Key_Escape, Qt::Key_CapsLock, Qt::Key_unknown,
        };
        for (Qt::Key key : unsupported) {
            QVERIFY2(ShortcutKeyMap::qtKeyToVirtualKeyCode(key) == -1,
                     qPrintable(QString("key %1 should be unsupported").arg(key)));
        }
    }
};

QTEST_GUILESS_MAIN(GlobalShortcutKeyMapTest)
#include "GlobalShortcutKeyMapTest.moc"
