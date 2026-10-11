#include "../../src/capture/SnipScreen.h"
#include "../../src/widgets/PinWindow.h"

// 测试桩：ShortcutTypes.cpp 的 dispatchShortcutAction 引用的 GUI 类方法。
// 单元测试闭包不编译 GUI 类实现（SnipScreen.cpp / PinWindow.cpp），
// 此处提供空实现满足链接。这些方法在测试中不应被调用（不触发 dispatch）。

void SnipScreen::start() {}
void SnipScreen::startRecording() {}
void SnipScreen::stopRecording() {}
void SnipScreen::togglePauseRecording() {}
void SnipScreen::grabFullscreen() {}
void SnipScreen::grabActiveWindow() {}
void SnipScreen::pinClipboard() {}
void PinWindow::toggleAll() {}
