#include "ShortcutTypes.h"

#include "../capture/SnipScreen.h"
#include "../utils/Utils.h"
#include "../widgets/PinWindow.h"
#include "../widgets/HistoryWindow.h"

/**
 * @brief 执行快捷键对应的动作（全局热键与托盘菜单共用的单一实现）
 * @author chiangyang
 */
void dispatchShortcutAction(ShortcutType type,
                            SnipScreen *snipScreen,
                            HistoryWindow *historyWindow) {
    switch (type) {
    case ShortcutType::Snip:
        if (snipScreen) snipScreen->start();
        break;
    case ShortcutType::Record:
        if (snipScreen) snipScreen->startRecording();
        break;
    case ShortcutType::History:
        if (historyWindow) {
            // 全局热键/托盘打开时应用多处于未激活状态：不能 raise/activateWindow，
            // 否则整个应用（含被盖住的设置窗口）会被系统整体抬到前台。
            // orderFrontRegardless 只把历史窗口提到可见位置，键盘输入由用户
            // 点击窗口时按 macOS 标准行为激活后生效
            Utils::showFrontNoActivate(historyWindow);
        }
        break;
    case ShortcutType::Pin:
        if (snipScreen) snipScreen->pinClipboard();
        break;
    case ShortcutType::Fullscreen:
        if (snipScreen) snipScreen->grabFullscreen();
        break;
    case ShortcutType::ActiveWindow:
        if (snipScreen) snipScreen->grabActiveWindow();
        break;
    case ShortcutType::RecordPause:
        if (snipScreen) snipScreen->togglePauseRecording();
        break;
    case ShortcutType::RecordStop:
        if (snipScreen) snipScreen->stopRecording();
        break;
    case ShortcutType::TogglePins:
        PinWindow::toggleAll();
        break;
    }
}
