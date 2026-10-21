# 录制 GIF 功能

## Context

现有录屏只输出 MP4（Windows 走 Media Foundation H.264）。用户希望在**截屏模式和录屏模式下都能录制 GIF**，GIF 帧率 10 fps。

两种模式实际共用同一套录制编排：截屏工具栏点「录屏」经 `SnipScreen::switchToRecordingMode()` 切到录屏工具栏，两者最终都由 `RecordingSession` 创建 `RecordingToolBar` + `RecordingControlWindow` + `ScreenRecorder`，并由控制栏的「开始」触发 `ScreenRecorder::start()`。因此**只需在录制控制栏加格式选择**，两种模式即可同时具备 GIF 能力，无需两套入口。

## 实现方案

### 1. 新增 GIF 编码器（无第三方依赖）

新增 `src/recording/GifEncoder.h/.cpp`，纯 Qt 实现 GIF89a 动画编码：

- `bool open(const QString &path, const QSize &size)`：写文件头、逻辑屏幕描述符（无全局调色板）、Netscape 无限循环扩展。
- `bool addFrame(const QImage &frame, int delayCentiseconds)`：用 `QImage::convertToFormat(QImage::Format_Indexed8, Qt::DiffuseDither)` 生成每帧局部调色板（≤256 色，屏幕内容足够）；与上一帧完全相同则只累加延迟（静态画面大幅减小体积），否则刷新上一帧。
- `bool close()` 写 Trailer；`void abort()` 关闭并删除文件。
- 每帧写：图形控制扩展（延迟、Disposal=1 整帧覆盖）+ 图像描述符（局部色表）+ LZW 压缩数据（最小码长 2~8，字典满 4096 时发 Clear Code，输出按 255 字节子块封装）。
- 新增方法均写中文 Doxygen 注释（@brief/@param/@return/@author）。

### 2. 录屏器支持输出格式

**[ScreenRecorder.h](file:///e:/develop/Code/github_new/quick-shot/src/recording/ScreenRecorder.h)**
- 新增 `enum class OutputFormat { Video, Gif };`
- `start(...)` 增加参数 `OutputFormat format = OutputFormat::Video`，`Impl` 增加 `bool gifMode` 与 `std::unique_ptr<GifEncoder> gifEncoder`。
- 新增 `static bool isGifSupported()`：Windows/Linux 为 true，macOS 为 false（其 ScreenCaptureKit 管线不产出 C++ 帧缓冲）。

**[ScreenRecorder_win.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/platform/ScreenRecorder_win.cpp#L696-L990)**
- `gifMode` 时跳过 COM/Media Foundation 初始化、音频采集与 SinkWriter 创建。
- 复用现有 BitBlt + StretchBlt 截帧与标注 overlay 合成代码；合成后若 `gifMode`，将帧交给 `GifEncoder::addFrame(frame, 10)`（10 fps→10cs），否则走原 MF 写样本分支。
- 循环结束后 `gifMode` 分支调用 `gifEncoder->close()`；取消录制沿用现有 `QFile::remove()` 删除逻辑，最后照常 `emit stopped(path)`。

**[ScreenRecorder_linux.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/platform/ScreenRecorder_linux.cpp#L21-L90)**
- 同样按 `gifMode` 分支：现有 X11 帧缓冲（ARGB32）合成 overlay 后写入 GIF；该平台原本没有编码落盘，GIF 分支需在结束时 close/删除并 `emit stopped()`。

**ScreenRecorder_mac.cpp**
- `gifMode` 时直接 `errorOccurred(record.error.gifUnsupported)` + `stopped()`（UI 侧在 macOS 上不显示 GIF 选项，属兜底）。

### 3. 控制栏格式选择（两种模式共用）

**[RecordingControlWindow.h](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingControlWindow.h)/[.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingControlWindow.cpp)**
- 在「开始/暂停/继续/结束」之后、音频选项之前新增 `QComboBox *m_formatCombo`，条目 MP4 / GIF，样式复用 `StyleManager::getComboBoxStyle()`（非 macOS 才添加 GIF 条目）。
- 初始值读 `ConfigManager` 的 `record/format`（`"mp4"`/`"gif"`），切换时写回实现持久化。
- 选择 GIF 时取消勾选并禁用「系统声音/麦克风」复选框（GIF 无音轨）；切回 MP4 时重新启用。
- 录制中（`updateButtonStates`）禁用格式下拉框。
- 新增 `QString selectedFormat() const`；`retranslateUi()` 刷新条目文本。
- [RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp#L268-L270) 的 `retranslateUi()` 补充 `m_control->retranslateUi()`（目前遗漏，格式下拉需要语言切换刷新）。

### 4. 录制启动与持久化

**[RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp#L58-L94)**
- `startRequested` 中读取 `m_control->selectedFormat()`：
  - GIF：扩展名 `.gif`，fps = 10，`OutputFormat::Gif`，启动前强制 `setAudioEnabled(false)` / `setMicrophoneEnabled(false)`。
  - MP4：保持 `.mp4` + 30 fps + `OutputFormat::Video`。
- 关键分支打英文日志（如 `Start GIF recording: fps=10, path=...`）。

**[ConfigManager.cpp](file:///e:/develop/Code/github_new/quick-shot/src/core/ConfigManager.cpp)**
- 默认配置新增 `record/format` = `"mp4"`（老配置自动补默认值）。

### 5. 多语言与构建

- 6 个语言文件新增：`record.format.mp4`（MP4）、`record.format.gif`（GIF）、`record.error.gifUnsupported`（当前平台不支持 GIF 录制）。
- [CMakeLists.txt](file:///e:/develop/Code/github_new/quick-shot/CMakeLists.txt#L255-L256)：源文件列表加入 `src/recording/GifEncoder.cpp` / `.h`。

## 设计要点

- GIF 编码在录屏工作线程内完成，不阻塞 UI；`recording/` 层新增跨平台编码器，Windows/Linux 复用同一份帧合成逻辑。
- 每帧局部调色板 + 相同帧延迟合并，兼顾画质与体积；默认 10 fps。
- 格式选择全局持久化（截图/录屏两种模式共用同一偏好），重启后保持。
- 不改动 MP4 现有行为；macOS 隐藏 GIF 选项。

## 验证

（用户自行编译）
1. 截图模式：框选后点「录屏」→ 控制栏出现格式下拉，选 GIF → 开始录制 → 选区出现计时 → 录制中画矩形/箭头 → 结束，在「视频」目录生成 `.gif`，浏览器/看图工具中可循环播放，标注已烧录进帧。
2. 录屏模式（托盘/快捷键进入）重复上述步骤，确认同样可选 GIF。
3. 选 GIF 时音频复选框被取消并禁用；切回 MP4 后恢复，MP4 录制仍正常。
4. 重启软件，控制栏格式保持上次选择。
5. 静态画面录制时检查 GIF 体积明显小于逐帧写入（相同帧已合并）。
6. 暂停/继续、取消录制（取消后文件被删除）行为正常。
