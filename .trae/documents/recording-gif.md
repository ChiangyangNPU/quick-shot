# 录制 GIF 功能

## Context

录屏原先只输出视频（Windows 走 Media Foundation H.264）。需求：**截屏模式和录屏模式都能录制 GIF**，默认 10 fps。

两种模式共用同一套录制编排：截屏工具栏的「录屏」经 `SnipScreen::switchToRecordingMode()` 切到录屏工具栏，最终都由 `RecordingSession` 创建 `RecordingToolBar` + `RecordingControlWindow` + `ScreenRecorder`。因此 GIF 能力由同一套录制管线提供，两种模式通过各自的 GIF 快捷按钮进入。

## 实现

### 1. GIF 编码器（无第三方依赖）

新增 [GifEncoder.h](file:///e:/develop/Code/github_new/quick-shot/src/recording/GifEncoder.h) / [GifEncoder.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/GifEncoder.cpp)，纯 Qt 实现 GIF89a 动画编码：

- `bool open(const QString &filePath, const QSize &size)`：写文件头、逻辑屏幕描述符（无全局调色板）、Netscape 无限循环扩展。
- `bool addFrame(const QImage &frame, int delayCentiseconds)`：帧尺寸不一致时用 `FastTransformation` 缩放到目标尺寸；与上一待写帧完全相同则只累加延迟（静态画面不重复写数据），否则先刷新上一帧再挂起当前帧。
- `bool close()` 写出最后一帧与 Trailer；`void abort()` 关闭并删除未完成文件；析构时若仍打开自动 abort。
- 调色板：统计 5-5-5 量化后的颜色直方图（32768 个桶），按出现次数取前 256 色生成每帧局部色表；未入选的颜色按欧氏距离映射到最近调色板项。
- 每帧写：图形控制扩展（延迟、Disposal=1 保留整帧、无透明）+ 图像描述符（局部色表）+ LZW 压缩数据（按 255 字节子块封装，最小码长 2~8）。
- LZW 关键点：字典上限 4095，满时发 Clear Code 并重置；**码宽必须在写出当前码之后再提升**（与 giflib/compress 一致），否则解码器错位会造成花屏。
- 全部新增方法带中文 Doxygen 注释（@brief/@param/@return/@author）。

### 2. 录屏器支持输出格式

**[ScreenRecorder.h](file:///e:/develop/Code/github_new/quick-shot/src/recording/ScreenRecorder.h)**
- 新增 `enum class OutputFormat { Video, Gif };`
- `start(captureRectPx, outputSizePx, outputFilePath, fps, format = OutputFormat::Video)` 增加格式参数；`Impl` 增加 `bool gifMode` 与 `std::unique_ptr<GifEncoder> gifEncoder`。
- 新增 `static bool isGifSupported()`：Windows/Linux 为 true，macOS 为 false。

**[ScreenRecorder_win.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/platform/ScreenRecorder_win.cpp)**
- `gifMode` 时跳过 COM/Media Foundation 初始化、音频采集与 SinkWriter 创建。
- 复用 BitBlt + StretchBlt 截帧与标注 overlay 合成；合成后把帧交给 `GifEncoder::addFrame`（延迟 `qMax(1, 100 / fps)`，10 fps → 10cs），不再走 MF 写样本分支。
- 循环结束：取消则 `gifEncoder->abort()` 删除文件，否则 `close()` 完成编码；随后照常 `emit stopped(path)`。非 GIF 分支才执行 `MFShutdown` / `CoUninitialize`。

**[ScreenRecorder_linux.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/platform/ScreenRecorder_linux.cpp)**
- 同样按 `gifMode` 分支：X11 截帧并合成 overlay 后写入 GIF，结束时 close/abort 并 `emit stopped()`。

**[ScreenRecorder_mac.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/platform/ScreenRecorder_mac.cpp)**
- `gifMode` 时直接 `errorOccurred(record.error.gifUnsupported)` 并结束（系统 ScreenCaptureKit 管线不产出可编码的帧缓冲）。UI 侧在 macOS 上不显示 GIF 入口，此为兜底。

### 3. 工具栏 GIF 入口（两种模式各一个）

**[BaseToolBar.h](file:///e:/develop/Code/github_new/quick-shot/src/widgets/BaseToolBar.h)** 新增信号 `void gifRecordRequested();`。

- [ScreenshotToolBar.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/ScreenshotToolBar.cpp)：GIF 按钮放在右侧操作区（OCR/翻译之后、复制之前），通过 `makeActionButton({&m_gifBtn, "record.gif", ":/icons/gif.svg"}, ...)` 创建；`!ScreenRecorder::isGifSupported()` 时隐藏。
- [RecordingToolBar.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingToolBar.cpp)：GIF 按钮放在操作区（快照之后、取消之前），创建方式相同。
- 两个按钮都注册进操作按钮表，图标/文字模式切换与翻译统一由 `updateActionButtons` / `retranslateActionButtons` 处理；新增图标 [gif.svg](file:///e:/develop/Code/github_new/quick-shot/icons/gif.svg) 并在 [resources.qrc](file:///e:/develop/Code/github_new/quick-shot/src/resources.qrc) 注册为 `:/icons/gif.svg`。
- 接线：[SnipScreen.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/SnipScreen.cpp) 中截图工具栏的 `gifRecordRequested → switchToRecordingMode() + showControlForGif()`；[RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp) 中录屏工具栏的 `gifRecordRequested → showControlForGif()`。
- `RecordingSession::showControlForGif()`：控制栏格式置为 GIF、录屏按钮置为选中、显示控制栏并更新输入遮罩；用户再点「开始」即开始录制。录制期间 `setGifButtonEnabled(false)` 禁用 GIF 按钮，结束/出错后恢复。

### 4. 控制栏格式选择（两种模式共用）

**[RecordingControlWindow.h](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingControlWindow.h) / [.cpp](file:///e:/develop/Code/github_new/quick-shot/src/widgets/RecordingControlWindow.cpp)**
- 在「开始/暂停/继续/结束」之后、音频选项之前新增 `QComboBox *m_formatCombo`，条目 MP4 / GIF；`ScreenRecorder::isGifSupported()` 为 false 时不添加 GIF 条目。
- 初始值读 `ConfigManager` 的 `record/format`，切换时写回实现持久化。
- 新增 `selectedFormat()` / `setSelectedFormat()`；后者会把格式写回配置，供 GIF 快捷入口预选。
- 选择 GIF 时取消勾选并禁用「系统声音/麦克风」复选框；切回 MP4 时恢复可用。
- `updateButtonStates()` 在录制中禁用格式下拉框；`retranslateUi()` 刷新条目文案。
- [RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp) 的 `retranslateUi()` 同时刷新控制栏（原先只刷新工具栏）。

### 5. 录制启动与持久化

**[RecordingSession.cpp](file:///e:/develop/Code/github_new/quick-shot/src/capture/RecordingSession.cpp)**
- `startRequested` 中读取 `m_control->selectedFormat()`：
  - GIF：扩展名 `.gif`，fps = 10，`OutputFormat::Gif`，启动前强制 `setAudioEnabled(false)` / `setMicrophoneEnabled(false)`。
  - MP4：保持 `.mp4` + 30 fps + `OutputFormat::Video`。
- 实际启动日志位于 [ScreenRecorder.cpp](file:///e:/develop/Code/github_new/quick-shot/src/recording/ScreenRecorder.cpp) 的 `start()`，形如 `Start recording: area=... fps=10 format=gif save path=...`；GIF 编码器初始化与完成日志在平台实现内。

**[ConfigManager.cpp](file:///e:/develop/Code/github_new/quick-shot/src/core/ConfigManager.cpp)**
- 默认配置新增 `record/format` = `"mp4"`（老配置启动时自动补默认值）。

### 6. 多语言与构建

- 6 个语言文件新增：`record.gif`（GIF 按钮）、`record.format.mp4`、`record.format.gif`、`record.error.gifUnsupported`。
- [CMakeLists.txt](file:///e:/develop/Code/github_new/quick-shot/CMakeLists.txt) 源文件列表加入 `src/recording/GifEncoder.cpp` / `.h`。
- [resources.qrc](file:///e:/develop/Code/github_new/quick-shot/src/resources.qrc) 加入 `icons/gif.svg`。

## 设计要点

- GIF 编码在录屏工作线程内完成，不阻塞 UI；Windows/Linux 复用各自已有的截帧与标注合成逻辑。
- 每帧局部调色板 + 相同帧延迟合并，兼顾画质与体积；默认 10 fps。
- 格式选择全局持久化（截屏/录屏共用同一偏好），重启后保持。
- 不改动 MP4 既有行为；macOS 隐藏 GIF 入口。

## 验证

（用户自行编译）
1. 截屏模式：框选 → 点右侧操作区的 GIF 按钮 → 自动切到录屏工具栏并打开控制栏（格式已选 GIF）→ 开始录制 → 录制中画矩形/箭头 → 结束，在「视频」目录生成 `.gif`，可循环播放且标注已烧录。
2. 录屏模式（托盘/快捷键进入）：框选 → 点操作区 GIF 按钮（快照右侧）→ 控制栏预选 GIF → 开始录制，结果同上。
3. 控制栏手动切换 MP4/GIF：选 GIF 时音频复选框被取消并禁用，切回 MP4 恢复；MP4 录制仍正常。
4. 重启软件，控制栏格式保持上次选择。
5. 录制过程中 GIF 按钮置灰，结束或出错后恢复可用；取消录制后未完成的 `.gif` 被删除。
6. 静态画面录制时 GIF 体积明显小于逐帧写入（相同帧已合并）。
7. 画面不应出现花屏（LZW 码宽时序已按 giflib 修正）。
