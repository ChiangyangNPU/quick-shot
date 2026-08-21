# QuickShot Screen Capture Tool

QuickShot is a lightweight, powerful screen capture and recording tool developed with Qt 6, designed to provide an efficient and smooth screenshot experience.

## ✨ Key Features

### 1. Smart Capture Mode
*   **Global Shortcuts**: Defaults are `Alt + Q` capture, `Alt + S` record, `Alt + H` history, `Alt + P` pin clipboard (with history-cycle pagination), `Alt + Shift + F` fullscreen capture, `Alt + Shift + W` active window capture, `Alt + Shift + S` record pause/resume, `Alt + Shift + Q` record stop, `Alt + Shift + P` toggle all pins — all centrally managed via ShortcutManager and customizable in Settings. 9 global hotkeys are data-table driven; adding a new global hotkey requires only 3 changes.
*   **Auto Window Detection**: Automatically detects and highlights window areas when hovering over software windows; click to select quickly.
*   **Display/Fullscreen Switching**: Supports rectangular area, window, display, and desktop capture modes. Use the mouse wheel to quickly switch snapping levels.
*   **Free Rectangular Selection**: Supports drag-and-drop to create rectangular screenshot areas of any size.
*   **Smart Right-Click Interaction**:
    *   **No Selection**: Right-click automatically snaps to the window under the mouse; if on desktop background, it captures full screen.
    *   **Selection Active**: Right-click cancels the current screenshot (exit).
    *   **Drawing**: Right-click cancels the current drawing operation to prevent accidental touches.

### 2. Powerful Annotation Tools
Once a screenshot area is selected, the bottom toolbar provides rich editing functions with **secondary menu** customization:
*   **🔲 Shape Tools**:
    *   **Rectangle**: Quickly frame important areas.
    *   **Ellipse**: Circle and highlight important regions.
    *   **Triangle**: Mark important areas, suitable for directional annotations.
    *   **Line**: Connect two points with a line annotation.
    *   **Arrow**: Point to key content (acute triangle arrow with moderate size).
    *   **Custom Colors**: Supports Red, Blue, Black, Yellow, Green, White, and custom color picker.
*   **✏️ Pen Tool**:
    *   Freehand drawing and writing.
    *   Supports **stroke width adjustment** and color selection.
*   **📝 Text Tool**:
    *   Input text anywhere on the screenshot.
    *   Supports **real-time font size adjustment** and **color changing**.
    *   Text box position can be adjusted by dragging.
*   **▒ ▓ Mosaic Tool**: Pixelate sensitive information (like accounts, passwords).
*   **🧹 Eraser Tool**: Click or drag over annotations to delete them.
*   **🖱️ Drag to Move**: Hover the mouse over the most recent annotation (except mosaic/eraser), the cursor changes to movable state, then drag to reposition the annotation. Movement is constrained within the selection area, with undo/redo support.
*   **Undo/Redo**: Supports keyboard shortcuts Ctrl+Z to undo, Ctrl+Y to redo, and can clear all annotations. Undo priority: move operations are undone before annotation creation.
*   **📐 Annotation Constraints**: When drawing rectangle/ellipse/arrow/line, hold `Shift` for proportional constraint (square/circle/45° line), hold `Alt` to draw symmetrically from center (center constraint), `Shift+Alt` can be combined. Constraints are automatically clamped within the selection.
*   **🎯 Adaptive Control-Point Cursors**: Different cursors are set per control-point position for clear directional feedback: Rectangle TL/BR uses ↘ SizeFDiagCursor, TR/BL uses ↙ SizeBDiagCursor, top/bottom midpoints use ↕ SizeVerCursor, left/right midpoints use ↔ SizeHorCursor; Ellipse left/right ↔, top/bottom ↕; Line/Arrow/Triangle endpoints use ✥ SizeAllCursor. Cursor stays unchanged while the mouse button is held down during drawing.
*   **▒ Global Mosaic Algorithm + Eraser/Pen-Width Coupling**: Mosaic uses a "global preprocess + QRegion clipping" algorithm (the entire background is down-sampled once then up-sampled; all stroke rects are merged into a clip region for one-shot drawing) which eliminates per-block seam/color artifacts; mosaic stroke radius = eraser radius = penWidth × 2, all three linked via `m_currentPenWidth`.
*   **🔁 Unified Annotation Interaction via AnnotationInteractionHandler**: Annotation interaction logic for capture/recording/pin-window three places is shared via AnnotationInteractionHandler + Host-callback strategy (unified mouse-event priority, control-point cursors, recording overlay sync, consistent Shift/Alt constraints). New annotation features need only one change to take effect in all three scenarios.

### 3. Flexible Area Adjustment
*   **Fine-tuning**: Drag any of the 8 edges or corners to resize the screenshot area.
*   **Move Selection**: Click and drag inside the selection area to move the entire screenshot area.
*   **Locked Annotation Mode**: Selecting an annotation tool locks the selection area for focused annotation.

### 4. 📝 OCR Text Recognition
*   **One-click Recognition**: Click the "OCR" button on the toolbar to recognize text in the screenshot area.
*   **Multi-language Support**: Supports recognizing Chinese + English, English, Japanese, Korean, and multilingual mixed text.
*   **GPU Acceleration**: Enable GPU acceleration in Settings to improve recognition speed.
*   **Result Display**: Recognition results are displayed in a dialog, with one-click copy to clipboard, and support for dragging to move and resizing from edges/corners.
*   **Recording Integration**: OCR functionality is also available during screen recording.
*   **Pin Window Recognition**: OCR can be performed directly on pinned windows via the right-click menu.
*   **Automatic Resource Management**: Models are automatically released after recognition to save memory.

### 5. 🌐 Text Translation Feature
QuickShot provides two translation forms based on OCR results, translating recognized text into the target language with support for 4 translation engines:

#### Form 1: In-dialog Comparison Translation
*   Click the "Translate" button at the bottom of the OCR result dialog to switch between Original / Translation / Side-by-side views.
*   Uses whole-segment translation for fewer requests and lower quota consumption.

#### Form 2: Translation Overlay Display (Position-aware)
*   **Three entry points**: Translate button on screenshot toolbar, translate button on recording toolbar, "Translate" item in pin window right-click menu.
*   **Position Overlay**: Translated text is overlaid at the original text positions detected by OCR, preserving the original layout.
*   **Standalone Window**: A separate `TranslateOverlayWindow` is displayed, supporting:
    *   View mode switching: Original only / Translation only / Side-by-side (translation appended below original).
    *   Window dragging, bottom-right resize, wheel zoom, ESC to close, double-click to close.
    *   After translation succeeds and the Overlay is shown, the snip frame exits automatically (same as pin destroying the snip frame); PinWindow keeps the pinned window after translation.
    *   Right-click menu: view modes, text selection mode, copy original, copy translation, save as image, close.
    *   **Text Selection Mode**: Check "Text Selection Mode" to freely drag-select translation text across lines and segments; inside selection mode, right-click keeps Copy/Select All (localized) and appends a "Text Selection Mode" toggle (uncheck to exit) and a "Cancel" item (exit and close the Overlay); Ctrl+C to copy, ESC to exit selection mode.
*   **Batch Translation**: Translates segment by segment in order; single-segment failures fall back to original text without interrupting the overall flow.

#### Translation Engines
| Engine | Type | Description |
|---|---|---|
| MyMemory | Default, no registration | Direct access from China, 5000 words/day without email, 50000 words/day with email |
| Baidu Translate | User-provided AppId + Key | Stable in China, 2 million characters/month free |
| DeepL | User-provided Key | Highest translation quality |
| LibreTranslate | User-provided URL | Can be self-hosted for fully offline use |

> **Privacy Protection**: A dialog appears on first translation use explaining that text will be sent to third-party services, with a "Don't ask again" option. All Keys / URLs are entered by the user in Settings; the software does not embed any credentials. See [Translation Feature Technical Doc](docs/translation-design.md) for details.

### 6. 📌 Pin to Desktop
*   **Pin**: Click the "Pin" button on the toolbar to pin the current screenshot (including annotations) to the desktop.
*   **Free Interaction**:
    *   The pinned window stays on top of other windows by default.
    *   Drag the pinned window to move it anywhere.
    *   Drag edges to resize the pinned window (perfect High DPI support, adaptive SmoothPixmapTransform: smooth enabled on non-1:1 scale, disabled on 1:1 to preserve sharpness).
    *   **Double-click** the pinned window to close it.
    *   Supports mouse wheel zoom on the pinned window; annotations and mosaic strokes scale synchronously via AnnotationManager::scaleAll.
    *   Supports right-click menu (Copy → OCR → Translate → Save → Close, order strictly per project convention).
    *   Right-click to enter **Annotation Mode** with a standalone toolbar for 8 annotation tools (rectangle/ellipse/arrow/pen/line/text/mosaic/eraser, switch with number keys 1-8), supporting Shift/Alt constraints, undo/redo, Tab color cycling, `[`/`]` pen width, Delete to clear.
    *   **Unified Annotation Shortcuts**: Both SnipScreen and PinWindow register annotation shortcuts uniformly through AnnotationShortcutController + IShortcutHandler strategy interface + QShortcut (Qt::WindowShortcut). When OverlayTextEdit gains focus, setBareKeysEnabled(false) is called automatically to disable bare keys and avoid input conflicts.
    *   **Pin Shortcuts**: `Ctrl+C` copy, `Ctrl+S` save, `Ctrl+Z`/`Ctrl+Y` undo/redo, arrow keys move window (`Ctrl+Arrow` 10px fast move), `ESC` exit annotation mode or close window.
    *   Screenshots are also written to the system clipboard when pinned, and HistoryManager::addScreenshotPixmap() is called so the entry is recorded for Alt+P history-cycle pagination.
    *   **Alt+P Clipboard History Pagination**: First Alt+P displays the latest screenshot centered on the current mouse screen; subsequent presses cycle older screenshots in reverse chronological order (position offset +(24,24) each time, clamped to screen bounds); multiple pinned windows may stay visible simultaneously; wraps back to the newest after reaching the oldest.
    *   Press `Alt + Shift + P` to toggle visibility of all pinned windows (ShortcutManager::TogglePins, backed by PinWindow::toggleAll static method).

### 7. 📹 Screen Recording Feature
*   **Global Shortcut**: Default is `Alt + S` to quickly start recording (customizable in Settings).
*   **Area Selection**: Supports selecting any rectangular area for recording.
*   **Window Recording**: Supports recording a specific window directly.
*   **Audio Recording**: Supports simultaneous recording of **system audio** and **microphone**, each independently toggleable.
*   **Real-time Annotation (zero-ghost + process-visible)**: All annotation tools (arrow/rectangle/ellipse/triangle/line/pen/text/mosaic/eraser) are available during recording with no duplicated ghost annotations in the final video:
    *   Before recording starts, `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` is called to exclude SnipScreen (a WA_TranslucentBackground layered window) from BitBlt capture, preventing CAPTUREBLT from also capturing the annotations already drawn on the transparent window which would double-layer a ghost; affinity is restored when recording stops.
    *   The pixel-capture base cap inside `renderAnnotationOverlay()` is strictly aligned with ScreenRecorder::captureRect (= selection minus border), eliminating annotation position offset.
    *   Every operation branch in `AnnotationInteractionHandler::handleMouseMove` (control-point drag, annotation drag, eraser, mosaic, geometry/pen drawing) calls `syncOverlay()` to push the recording overlay, ensuring the drag-intermediate frames of the annotation process are fully visible in the recorded video, not just the final result.
*   **Recording Control**: Supports start, pause, resume, and stop recording.
*   **Cancel Recording**: Supports canceling recording, automatically deleting the generated video file upon cancellation.
*   **Snapshot**: Capture the current recording frame to the clipboard without stopping the recording; the snapshot is also written to history automatically for Alt+P pagination.
*   **Original Resolution Recording**: The recording output resolution equals the physical pixel size of the user-selected region, preserving original quality without scaling loss.
*   **Save Directory**: Customizable save directory for recording files.
*   **Recording Time Display**: Real-time display of recording duration during recording, making it easy to track recording progress.
*   **Anti-Flicker Toolbar/Sub-toolbar**: Sub-toolbars of the capture/recording main toolbars and the PinWindow standalone annotation toolbar transition smoothly with no flicker when switching tools: `BaseToolBar::clearSubToolbarLayout` uses synchronous `delete` to free layout space immediately; `showSubTools` wraps creation in `setUpdatesEnabled(false/true)` to suppress intermediate paints; additionally, PinWindow's top-level sub-toolbar window sets WA_NoSystemBackground to prevent the OS from clearing its background on resize.
*   **macOS Support**: Built on ScreenCaptureKit, requires macOS 13+ (Ventura), supports Retina display high-resolution recording with automatic DPR scaling.

### 8. 📋 History Feature
*   **Global Shortcut**: Default is `Alt + H` to quickly open the history window (customizable in Settings).
*   **Screenshot History**: Automatically records all screenshots (including saved and copied-to-clipboard screenshots), generating thumbnails for quick browsing.
*   **Clipboard History**: Automatically records copied/cut text content, including source application info.
*   **Categorized View**: Supports viewing history by "All / Screenshots / Texts" categories.
*   **Search & Filter**: Supports keyword content search and time range filtering (All / Today / Last 7 days / Last 30 days).
*   **Multi-select & Batch Operations**:
    *   Supports mouse drag-box selection, Ctrl+click to append, Shift+click for range selection.
    *   Supports batch deletion of multiple records.
    *   Button states intelligently linked to selection count and type.
*   **Actions**: Copy text/screenshot to clipboard, save screenshot to file (save path consistent with main program), delete single/batch records, clear all history.
*   **Context Menu**: Provides copy, save screenshot, delete shortcuts; menu style unified with pin window context menu.
*   **Auto Cleanup**: Automatically cleans expired records (default 7 days retention) and excess records (default max 1000 items) on app startup; manual cleanup also supported.
*   **Real-time Refresh**: When the history window is open, new records auto-refresh (300ms debounce).
*   **Local Storage**: All history is stored only in a local SQLite database, never uploaded to any server, protecting privacy.

### 9. ⚙️ Settings Center
Right-click the system tray icon to access "Settings". **Six tabs in order: General → Shortcuts → Style → Translate → History → About**:
*   **General**: Switch between 6 languages (Simplified Chinese, English, Traditional Chinese HK/TW, Japanese, Korean), enable Auto-start on boot, customize screenshot and recording save directories, log management, config file management, **OCR recognition language configuration**, **GPU acceleration**.
*   **Shortcuts**: Organized into 4 categories—**Global Hotkeys (Configurable)**: 9 customizable (capture/record/history/pin/fullscreen/active window/record pause/record stop/toggle pins). ShortcutManager drives the Settings UI rows from the ShortcutConfigItem data table; **Annotation Tools (Fixed)**: 1-8 switch tools; **Annotation Operations (Fixed)**: copy/undo/redo/save/pen width/cycle color/clear/Shift constraint/Alt center; **Pin Shortcuts (Fixed)**: copy/save/undo/redo/switch tool/move/exit. Changes take effect immediately and the tray menu is refreshed incrementally via the `shortcutChanged(ShortcutType, QKeySequence)` enum signal.
*   **Style**: Customize 20+ UI color properties (border colors, toolbar background/button/text/hover/disabled colors, sub-toolbar colors, tab colors, handle colors, etc.). 19 colors are centrally managed via the `colorSettingTable()` metadata table (17 colors with UI buttons + 2 button colors for save/load only); table-looping replaces the 34 scattered member variables. Button display mode (text/icon), sub-toolbar/control bar/tab color customization.
*   **Translate (between Style and History tabs)**: Select translation engine (MyMemory no-registration / Baidu / DeepL / LibreTranslate), configure target language (English by default, dropdown selection), fill in the API Key / URL for the chosen engine, toggle translation feature, toggle first-use privacy prompt. On first use a dialog appears (custom MessageBox centered on the current selection) explaining that text will be sent to third-party services.
*   **History**: Toggle screenshot history and clipboard history recording, set retention period (7/30/90/180/365 days) and max record count (500/1000/2000/5000 items), clean expired records (HistoryManager calls cleanupExpired() automatically on app startup), clear all history, view storage usage statistics.
*   **About**: View version information and website link.
*   **Manual DPI Adaptation**: Because the project disables Qt's automatic High-DPI scaling (`QT_ENABLE_HIGHDPI_SCALING=0`), when DPI changes SettingsWindow and HistoryWindow automatically call StyleManager::reapplyGlobalStyleSheet() to reload the global QSS. SettingsWindow then recalculates width/control sizes and re-adapts height depending on tab type (scrollable vs. non-scrollable); HistoryWindow recalculates its initial size and control sizes.
*   **Unified MessageBox Styling**: All confirmation/prompt dialogs use the custom `MessageBox` class (extends QMessageBox) which auto-applies the project MessageBox QSS style + loadAppIcon title-bar icon, auto-translates buttons with tm->get("ok"/"yes"/"no"), supports Yes/No questioning with No as default, centerOn(rect) positioning over the selection/parent window, and exposes one-liner static convenience methods (information/warning/critical/question). Translation errors also expose a secondary "show details" MessageBox.

## 📦 Installation & Deployment

### Developer Build
1.  Ensure Qt 6.10.2 and CMake are installed.
2.  Clone the project and build:
    ```bash
    mkdir build && cd build
    cmake ..
    cmake --build .
    ```

> **Version Management**: The version is managed centrally by [CMakeLists.txt](file:///e:/develop/Code/github_new/quick-shot/CMakeLists.txt#L2) via `project(QuickShot VERSION x.y.z)` and injected into source code through the `QUICKSHOT_VERSION` macro. After changing the version, use **Reset Cache and Reload CMake Project** in CLion.

### Quick Packaging
The project provides automated packaging scripts, no need to manually copy DLLs. The script reads the version number automatically from CMakeLists.txt.

#### Windows
1.  Run the PowerShell script in the `deploy/win/` directory:
    ```powershell
    # Package both Debug and Release (default)
    .\deploy.ps1
    # Package Release only
    .\deploy.ps1 -r
    # Package Debug only
    .\deploy.ps1 -d
    ```
2.  The script will automatically generate a `QuickShot-Release-v{version}-Windows-x64` folder in `deploy/win/` (containing the directly runnable `QuickShot.exe` and all its dependencies) and a corresponding zip package.

#### macOS
1.  Navigate to the `deploy/mac/` directory and run the Shell script:
    ```bash
    cd deploy/mac
    # Build both Debug and Release by default
    ./deploy_mac.sh
    # Build Release only
    ./deploy_mac.sh -r
    # Build Debug only
    ./deploy_mac.sh -d
    # Disable GPU acceleration (CoreML), CPU-only inference
    ./deploy_mac.sh -r --no-gpu-acceleration
    ```
2.  The script will automatically generate `QuickShot-Release-v{version}.dmg` and/or `QuickShot-Debug-v{version}.dmg` installers in `deploy/mac/` (including Qt dependencies, ONNX Runtime, language files, with automatic code signing). See [Mac Deployment Guide](deploy/mac/README_DEPLOY_MAC.md) for details.

## 🛠️ Technical Features
*   **High DPI Support (Manual Adaptation)**: The project disables Qt auto high-DPI scaling (`QT_ENABLE_HIGHDPI_SCALING=0`); UI uses `pt`/`em` units for manual adaptation. On DPI change, SettingsWindow & HistoryWindow call StyleManager::reapplyGlobalStyleSheet() to reload global QSS and recalculate control sizes. Screenshots and pinned windows keep 1:1 pixel mapping; PinWindow paintEvent dynamically enables/disables SmoothPixmapTransform based on scale ratio to balance sharpness and smoothness.
*   **Active Window Anti-Black-Screen**: Uses screen DC (GetDC(NULL)) instead of window DC to capture active windows, avoiding black screens with hardware-accelerated apps (DirectX/Chromium/UWP) on Windows 8+.
*   **Multi-Monitor Support**: Windows/Linux uses virtual desktop global coordinate system for cross-screen capture and recording without coordinate conversion; macOS uses single-screen window mode (for Sidecar/AirPlay compatibility), with a 30ms timer polling the cursor position to auto-switch windows, re-grab the background, and call raiseWindowAboveMenuBar() to cover the menu bar when the cursor crosses screens.
*   **System Integration**: Features a system tray icon. TrayMenuBuilder builds menu items data-driven from the ShortcutConfigItem table (auto-concatenates "label + (shortcut)") and the shortcutChanged signal triggers incremental refresh. Supports Windows auto-start on boot.
*   **Internationalization**: Supports 6 languages (简体中文, English, 繁體中文（香港）, 繁體中文（台灣）, 日本語, 한국어) via TranslationManager + JSON files with runtime switching; the TranslationManager::languageChanged signal uniformly drives SettingsWindow / TrayMenuBuilder / ShortcutManager retranslate.
*   **OCR Engine**: Based on ONNX Runtime + PP-OCRv4 models, supports Chinese+English, English, Japanese, Korean recognition with GPU acceleration; OcrResultDialog uses custom styling and app icon.
*   **Translation Engine**: Multi-engine abstraction over Qt6::Network (TranslateEngine interface + 4 implementations: MyMemory/Baidu/DeepL/LibreTranslate), defaults to MyMemory with no registration required. 8 TranslateError codes are localized via tm->get(); batch-translation state machine falls segment failures back to original text without aborting; first-use privacy prompt uses a custom MessageBox centered on the current selection.
*   **Dynamic Toolbar Positioning**: Automatically calculates the position hierarchy (main → sub → control bar three-level stacking) based on selection area and screen space; preferred below the selection, flipped above if space is insufficient; arranged from bottom up for full-screen mode; multi-monitor aware.
*   **Real-time Recording Annotation (Zero Ghost + Process Visible)**: Recording start calls SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) to exclude SnipScreen from capture; overlay cap is strictly aligned to ScreenRecorder::captureRect (selection minus border); every branch of AnnotationInteractionHandler handleMouseMove invokes syncOverlay() so drag-intermediate frames are composited into the video, not just final results.
*   **Cross-Platform Recording**: Windows uses Media Foundation + WASAPI; macOS uses ScreenCaptureKit + AVAssetWriter (H.264) + AVCaptureSession microphone; Linux uses X11 + FFmpeg.
*   **Unified Annotation Interaction (AnnotationInteractionHandler)**: Capture / recording / pin-window share the same annotation logic through Host-callback strategy injection of per-window differences (position clamping, recording sync, selection constraint, toolbar hit testing, etc.). Shared features: mouse-event priority (toolbar → text-finalize → eraser → mosaic → control point → text → drag → create), control-point cursors, Shift/Alt constraints, eraser/mosaic radius linked to penWidth × 2, global mosaic algorithm, undo/redo with move operations first.
*   **Shortcut System (ShortcutManager)**: The 9 global hotkeys use a ShortcutType enum + ShortcutConfigItem data table (single source of truth); ShortcutManager (Facade) provides unified APIs; ShortcutRegistry (Registry pattern) manages GlobalShortcut lifetimes; AnnotationShortcutController + IShortcutHandler + QShortcut(Qt::WindowShortcut) uniformly manages SnipScreen / PinWindow annotation shortcuts; setBareKeysEnabled(false/true) handles text-edit conflicts.
*   **Anti-Flicker Sub-toolbars**: `BaseToolBar::clearSubToolbarLayout` uses synchronous `delete` to free layout space immediately; `showSubTools` wraps creation in `setUpdatesEnabled(false/true)` to suppress intermediate paints; PinWindow top-level sub-toolbar window sets WA_NoSystemBackground to prevent the OS from clearing its background.
*   **History**: Screenshots and clipboard text stored in SQLite database. Selection modes: drag-box multi-select / Ctrl append / Shift range select. Batch deletion supported. Auto cleanup: cleanupExpired() runs on startup (7 day retention + max 1000 items cap). Real-time refresh (300ms debounce). HistoryManager::addScreenshotPixmap() is called uniformly from all screenshot producers (copy/save/pin/grabFullscreen/grabActiveWindow/snapshotRequested) for Alt+P history-cycle pagination.
*   **Alt+P Pin Clipboard History Pagination**: SnipScreen::pinClipboard() reads the time-reversed screenshot list from HistoryManager. First press shows the latest entry centered on the current mouse screen (QCursor::pos - QPoint(w/2, h/2)); subsequent presses increment m_pinHistoryIndex modulo count; each next window's position inherits the previous position + (24,24), clamped to screen bounds via qBound. QPointer<PinWindow> provides safe tracking; PinWindow uses WA_DeleteOnClose so the pointer auto-nulls on close.
*   **Custom Unified MessageBox**: All dialogs (prompts/confirmations/privacy-warning/translation-error-detail) use src/widgets/MessageBox.h extending QMessageBox with automatic applyProjectStyle() (getMessageBoxStyle QSS + loadAppIcon icon), addOkButton()/addYesNoButtons() (tm->get("yes"/"no") translated, No default), centerOn(rect) for selection-relative positioning, one-liner statics (information/warning/critical/question), and a secondary "show details" MessageBox for translation errors.
*   **Configuration Persistence**: QSettings-based persistence. ConfigManager Singleton exposes saveConfigAsync()/loadConfigAsync() for async read/write with config file switch/reset/open-location support.
*   **Code Commenting Standards**: Standard Doxygen-style comments (@brief, @param, @return, @note, @author) are used. All public methods have Chinese method comments (and Doxygen-formatted English tags) ensuring readability and maintainability.
*   **Logging System**: Logger Singleton with LOG_INFO / LOG_WARN / LOG_ERROR / LOG_DEBUG macros. Per project convention, only LOG_INFO-level logs are emitted throughout codebase (DEBUG level is disabled as a hard constraint); all log messages are in English. Logs are written to the `logs/` folder under the installation directory (initialized after QApplication creation).
*   **Auto Update**: UpdateManager multi-channel fallback check (GitHub → Gitee → Official), SHA256 verification of download package, PowerShell Expand-Archive extraction, robocopy with backup-and-rollback file replacement, auto-launch new version after install; supports `QUICKSHOT_UPDATE_URL` environment variable to override update URL for local testing (see [Update Testing Guide](update-test/README.md)).
*   **Performance Optimization**:
    *   **Lazy Initialization**: Core modules initialize on demand (OCR loads models on first use, translation engines initialize on first translate) for faster shortcut-response time
    *   **Asynchronous Processing**: Screen capture and OCR recognition run in background threads (QtConcurrent) to avoid blocking the UI
    *   **Smart Detection**: Hunter window detection implements throttling + movement thresholds to minimize unnecessary scans
    *   **Model Reuse**: OCR models are released after recognition and auto-loaded when needed again
    *   **Recording Frame-Buffer Reuse**: ScreenRecorder worker thread reuses frameBuffer to minimize memory allocations; the annotation overlay is guarded with QMutex for thread-safe concurrent access.

## 🚀 Development Environment
*   **Language**: C++ 17
*   **Framework**: Qt 6.10.2
*   **Build System**: CMake
*   **Compiler**: MinGW 64-bit (Windows) / Clang (macOS)
*   **OCR**: ONNX Runtime

## 📄 License

This project is licensed under the **CC BY-NC-SA 4.0** (Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International) license.

### You are free to:
- ✅ Use, copy, and modify freely
- ✅ Use for personal learning and research
- ✅ Distribute for non-commercial purposes

### You are NOT allowed to:
- ❌ Use for commercial purposes
- ❌ Sell modified versions commercially
- ❌ Remove the original author's attribution

### You MUST:
- ⚠️ Credit the original author **chiangyang**
- ⚠️ License derivative works under the same license

See the [LICENSE](LICENSE) file for full terms.

## 📦 Third-Party Resources

This project uses the following open-source resources:

### OCR Algorithm and Models
- **OCR Core Algorithm**: Ported from the [PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR) open-source project
  - Implementation: DB detection post-processing, image preprocessing, CTC recognition decoding
  - License: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
  - Usage: Algorithm logic ported to pure C++/Qt implementation, no OpenCV dependency
- **PP-OCRv4 Models**: Pre-trained models based on the PaddleOCR open-source project
  - License: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
  - Usage: Pre-trained ONNX model weight files are used

### Inference Engine
- **ONNX Runtime**: Microsoft's open-source machine learning inference engine
  - License: [MIT License](https://opensource.org/licenses/MIT)
  - Project: https://github.com/microsoft/onnxruntime

### Qt Framework
- **Qt 6**: Cross-platform C++ application development framework
  - License: LGPL 3.0 / GPL 3.0 (open source edition)
  - Project: https://www.qt.io/

Thanks to the above open-source projects for their contributions!
