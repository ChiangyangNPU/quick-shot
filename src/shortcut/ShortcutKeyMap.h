#ifndef SHORTCUTKEYMAP_H
#define SHORTCUTKEYMAP_H

#include <Qt>

/**
 * @brief Qt 键码 → 平台虚拟键码映射（纯函数，可表驱动单测）
 *
 * GlobalShortcut::nativeRegister 的键码转换层。此前两张映射表内联在
 * nativeRegister 里（Windows VK 表 / macOS kVK_ANSI 表），注册错键只能
 * 在对应平台运行时暴露；提取为纯函数后可独立单测。
 *
 * @return 平台虚拟键码（Windows VK_* / macOS Carbon kVK_*）；
 *         该键无法注册全局热键时返回 -1
 * @author chiangyang
 */
namespace ShortcutKeyMap {
int qtKeyToVirtualKeyCode(Qt::Key key);
}

#endif // SHORTCUTKEYMAP_H
