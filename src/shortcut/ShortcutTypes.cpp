#include "ShortcutTypes.h"

#include "../capture/SnipScreen.h"
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
            historyWindow->show();
            historyWindow->raise();
            historyWindow->activateWindow();
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
