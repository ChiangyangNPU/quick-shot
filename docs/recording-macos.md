# QuickShot macOS 录屏技术文档

## 1. 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 视频捕获 | ScreenCaptureKit (`SCStream`) | 系统级屏幕捕获（macOS 12.3+） |
| 视频编码 | AVAssetWriter (H.264) | 硬件加速视频压缩 |
| 系统音频 | ScreenCaptureKit (`SCStreamOutputTypeAudio`) | 系统音频流捕获 |
| 麦克风 | AVCaptureSession + AVCaptureAudioDataOutput | 麦克风实时采集 |
| 音频编码 | AVAssetWriter (AAC) | 双声道音频压缩 |
| 容器格式 | MP4 (AVFileTypeMPEG4) | 视频封装 |
| 线程模型 | dispatch_queue_t + std::thread | 视频/音频独立调度队列 |
| 权限检测 | CGPreflightScreenCaptureAccess / SCShareableContent | 屏幕录制权限验证 |

## 2. 架构设计

### 2.1 分层架构

```mermaid
flowchart TD
    subgraph L1["UI 控制层"]
        A1["ScreenRecorder.h<br/>跨平台接口"]
        A2["RecordingControlWindow<br/>暂停/恢复/停止"]
    end
    subgraph L2["业务协调层"]
        B1["ScreenRecorder_Impl<br/>状态管理、DPR 计算"]
    end
    subgraph L3["平台桥接层 Q_OS_MACOS"]
        C1["ScreenRecorder_mac.cpp<br/>C++ 入口"]
        C2["ScreenRecorder_mac_helper.h<br/>C 接口声明"]
    end
    subgraph L4["Objective-C 实现层"]
        D1["ScreenRecorderDelegate<br/>SCStream 委托"]
        D2["SCStream + SCContentFilter<br/>屏幕捕获"]
        D3["AVAssetWriter + AVAssetWriterInput<br/>编码"]
        D4["AVCaptureSession<br/>麦克风"]
        D5["dispatch_queue<br/>视频/音频队列"]
    end
    subgraph L5["macOS API 层"]
        E1["ScreenCaptureKit / AVFoundation / AVKit / CoreMedia"]
    end
    L1 --> L2 --> L3 --> L4 --> L5
```

### 2.2 全局状态变量

```objc
// ScreenCaptureKit
static SCStream* g_stream = nil;                    // 捕获流
static SCContentFilter* g_contentFilter = nil;       // 内容过滤器（窗口/显示器）
static SCStreamConfiguration* g_streamConfig = nil;  // 流配置

// AVAssetWriter
static AVAssetWriter* g_assetWriter = nil;           // 媒体写入器
static AVAssetWriterInput* g_videoInput = nil;       // 视频输入 (H.264)
static AVAssetWriterInput* g_sysAudioInput = nil;    // 系统音频输入 (AAC)
static AVAssetWriterInput* g_micAudioInput = nil;    // 麦克风音频输入 (AAC)
static AVAssetWriterInputPixelBufferAdaptor* g_pixelBufferAdaptor; // 像素缓冲适配器

// 麦克风采集
static AVCaptureSession* g_micSession = nil;        // 麦克风会话
static AVCaptureDeviceInput* g_micDeviceInput = nil; // 麦克风设备输入
static AVCaptureAudioDataOutput* g_micOutput = nil;  // 音频数据输出
static dispatch_queue_t g_micQueue = nil;            // 麦克风调度队列

// 录制状态
static bool g_isRecording = false;
static bool g_isPaused = false;
static CMTime g_startTime = kCMTimeInvalid;
static int g_frameWidth = 0;
static int g_frameHeight = 0;
static int g_fps = 30;
```

### 2.3 录制模式

```mermaid
flowchart TD
    subgraph RM["RecordingMode"]
        direction LR
        subgraph WM["Window 模式"]
            W1["SCContentFilter<br/>initWithDesktopIndependentWindow:window"]
            W2["捕获指定窗口<br/>全窗口输出"]
            W3["config.width/height<br/>= 窗口尺寸 × DPR"]
            W1 --> W2 --> W3
        end
        subgraph AM["Area 模式"]
            A1["SCContentFilter<br/>initWithDisplay:display"]
            A2["捕获显示器<br/>使用 sourceRect 裁剪区域"]
            A3["config.sourceRect<br/>= 区域坐标 逻辑点"]
            A4["config.width/height<br/>= 区域尺寸 × DPR"]
            A1 --> A2 --> A3 --> A4
        end
    end
```

## 3. 视频捕获流程

### 3.1 ScreenCaptureKit 管线

```mermaid
flowchart TD
    A["SCShareableContent<br/>枚举可捕获内容<br/>显示器/窗口"] --> B["SCContentFilter<br/>选择捕获目标<br/>窗口/区域"]
    B --> C["SCStream<br/>开始捕获<br/>配置帧率/格式"]
    C --> D["SCStreamDelegate<br/>接收视频帧<br/>系统音频帧"]
    D --> E["CMSampleBuffer"]
    E --> F["CVPixelBuffer 32BGRA"]
    F --> G["AVAssetWriterInputPixelBufferAdaptor"]
    G --> H["AVAssetWriter H.264"]
    H --> I["MP4 文件输出"]
```

### 3.2 初始化与配置

**1. 获取共享内容（`SCShareableContent`）**

```objc
[SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                              onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent *content, NSError *error) {
    // content.displays → 可用显示器列表
    // content.windows → 可用窗口列表
}];
```

**2. 创建内容过滤器（`SCContentFilter`）**

- **窗口模式**：`initWithDesktopIndependentWindow:window`
  - 指定 `SCWindow` 对象，捕获整个窗口
- **区域模式**：`initWithDisplay:display excludingApplications:nil exceptingWindows:nil`
  - 指定 `SCDisplay` 对象，通过 `sourceRect` 裁剪区域

**3. 配置流参数（`SCStreamConfiguration`）**

| 参数 | 值 | 说明 |
|------|------|
| `config.width/height` | `outputSizePx` | 输出像素尺寸（已乘 DPR） |
| `config.sourceRect` | `CGRectMake(x, y, w, h)` | 区域模式裁剪（逻辑点坐标） |
| `config.minimumFrameInterval` | `CMTimeMake(1, fps)` | 帧率控制 |
| `config.queueDepth` | 3 | 帧队列深度 |
| `config.showsCursor` | YES | 显示鼠标光标 |
| `config.pixelFormat` | `kCVPixelFormatType_32BGRA` | 像素格式 |
| `config.capturesAudio` | YES | 启用系统音频捕获 |
| `config.excludesCurrentProcessAudio` | YES | 排除当前进程（macOS 13+） |
| `config.sampleRate` | 44100 | 系统音频采样率 |
| `config.channelCount` | 2 | 系统音频声道数 |

**4. 添加流输出**

```objc
// 视频输出（必须）
[g_stream addStreamOutput:g_delegate
                    type:SCStreamOutputTypeScreen
         sampleHandlerQueue:g_videoQueue
                     error:&streamError];

// 系统音频输出（可选）
[g_stream addStreamOutput:g_delegate
                    type:SCStreamOutputTypeAudio
         sampleHandlerQueue:g_audioQueue
                     error:&audioError];
```

### 3.3 录制模式差异

#### 窗口录制模式

```mermaid
flowchart LR
    A["用户选择窗口"] --> B["窗口 ID 匹配"]
    B --> C["SCWindow 查找"]
    C --> D["SCContentFilter<br/>initWithDesktopIndependentWindow"]
    D --> E["config.width/height<br/>= windowBounds × DPR"]
    E --> F["全窗口捕获 不裁剪"]
```

#### 区域录制模式

```mermaid
flowchart LR
    A["用户框选区域"] --> B["获取选区中心坐标"]
    B --> C["匹配 SCDisplay<br/>CGRectContainsPoint"]
    C --> D["SCContentFilter<br/>initWithDisplay"]
    D --> E["config.sourceRect 裁剪到选区"]
    E --> F["config.width/height<br/>= regionSize × DPR"]
    F --> G["Qt 逻辑坐标 = SK 逻辑点坐标<br/>无需 DPR 转换"]
```

## 4. 音频捕获流程

### 4.1 双源音频架构

```mermaid
flowchart TD
    subgraph S1["系统音频 ScreenCaptureKit"]
        A1["SCStreamOutputTypeAudio"] --> A2["SCStreamDelegate<br/>didOutputSampleBuffer"]
        A2 --> A3["g_sysAudioInput.appendSampleBuffer"]
    end
    subgraph S2["麦克风 AVFoundation"]
        B1["AVCaptureSession"] --> B2["AVCaptureDeviceInput<br/>defaultDeviceWithMediaType"]
        B2 --> B3["AVCaptureAudioDataOutput<br/>setSampleBufferDelegate"]
        B3 --> B4["g_micAudioInput.appendSampleBuffer"]
    end
    A3 --> C["AVAssetWriter AAC 128kbps"]
    B4 --> C
    C --> D["MP4 音频流"]
```

### 4.2 系统音频

- 来源：`SCStream` 的 `SCStreamOutputTypeAudio` 回调
- 编码：通过 AVAssetWriterInput 直接接收 CMSampleBuffer
- 参数：44100Hz / 2声道 / AAC 编码
- 注意：需 `SCStreamConfiguration.capturesAudio = YES`

### 4.3 麦克风采集

```objc
// 初始化麦克风会话
AVCaptureDevice* micDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
g_micSession = [[AVCaptureSession alloc] init];
g_micDeviceInput = [AVCaptureDeviceInput deviceInputWithDevice:micDevice error:&error];
[g_micSession addInput:g_micDeviceInput];
g_micOutput = [[AVCaptureAudioDataOutput alloc] init];
[g_micOutput setSampleBufferDelegate:g_delegate queue:g_micQueue];
[g_micSession addOutput:g_micOutput];
[g_micSession startRunning];
```

- 设备：`AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio`
- 委托：`ScreenRecorderDelegate` 实现 `AVCaptureAudioDataOutputSampleBufferDelegate`
- 队列：独立 `dispatch_queue_t`（`com.quickshot.miccapture`）

### 4.4 音频编码配置

```objc
NSDictionary* settings = @{
    AVFormatIDKey: @(kAudioFormatMPEG4AAC),
    AVSampleRateKey: @(44100),
    AVNumberOfChannelsKey: @(2),
    AVChannelLayoutKey: stereoLayoutData,
    AVEncoderBitRateKey: @(128000)
};
```

- 编码：AAC (MPEG-4)
- 采样率：44100Hz
- 声道：2 声道立体声
- 比特率：128kbps

## 5. DPR 处理逻辑

### 5.1 坐标系统转换

```
Qt 逻辑坐标 (points) × DPR = 实际像素 (pixels)

示例（Retina 2x 屏）：
  选区 1920×1080 (逻辑点)
  DPR = 2.0
  输出 = 3840×2160 (物理像素)
```

**处理流程：**

```mermaid
flowchart TD
    A["ScreenRecorder::start<br/>captureRectPx, outputSizePx"] --> B["captureRectPx<br/>已为物理像素 Qt HiDPI 自动处理"]
    B --> C["outputSizePx<br/>已为物理像素"]
    C --> D["ScreenRecorder_mac.cpp"]
    D --> E["QScreen::devicePixelRatio → dpr"]
    E --> F["startRecordingOnMainThread<br/>mac_helper.mm"]
    F --> G["scale = dpr > 0 ? dpr : 1.0"]
    G --> H["outputWidthPx = outputWidth × scale"]
    H --> I["outputHeightPx = outputHeight × scale"]
    I --> J["config.width = outputWidthPx"]
    J --> K["config.height = outputHeightPx"]
    K --> L["config.sourceRect = CGRectMake 逻辑坐标<br/>Qt 坐标与 SK 坐标系一致 无需额外转换"]
```

**注意事项：**
- ScreenCaptureKit 的 `sourceRect` 使用逻辑点坐标（与 Qt 坐标系一致）
- 输出分辨率使用物理像素（乘以 DPR）
- 窗口录制：outputWidth × DPR、outputHeight × DPR
- 区域录制：region.width × DPR、region.height × DPR

## 6. 权限检测

### 6.1 Screen Recording 权限

```objc
bool screenRecorderCheckPermission() {
    if (@available(macOS 14.0, *)) {
        // macOS 14+ 专用 API
        return CGPreflightScreenCaptureAccess();
    }
    // macOS 13: 通过尝试获取内容间接检测
    [SCShareableContent getShareableContent... completionHandler:^(...) {
        hasAccess = (content != nil && error == nil);
    }];
}
```

| 系统版本 | 检测方式 |
|----------|----------|
| macOS 14+ (Sonoma) | `CGPreflightScreenCaptureAccess()` |
| macOS 13 (Ventura) | `SCShareableContent` 获取结果 |

### 6.2 可用性判断

```cpp
bool screenRecorderImplIsAvailable() {
    int version = getMacOSVersion();  // sysctl "kern.osproductversion"
    if (version < 13) return false;   // 要求 macOS 13+
    return screenRecorderCheckPermission();
}
```

## 7. 完整录制流程

### 7.1 启动流程

```mermaid
flowchart TD
    A["ScreenRecorder::start<br/>startWindowRecording / startAreaRecording"] --> B["计算 DPR<br/>QScreen::devicePixelRatio"]
    B --> C["配置 captureRectPx / outputSizePx / fps"]
    C --> D["创建输出目录"]
    D --> E["启动录制线程 std::thread"]
    E --> F["screenRecorderStartAreaRecording<br/>screenRecorderStartWindowRecording"]
    F --> G["startRecordingOnMainThread<br/>outputPath, x, y, w, h, outW, outH, fps..."]
    G --> H["重置状态变量"]
    H --> I["计算 DPR 缩放后的输出尺寸"]
    I --> J["删除旧文件"]
    J --> K["创建 AVAssetWriter<br/>MP4, H.264, 6Mbps"]
    K --> L["创建 AVAssetWriterInput<br/>视频 + 可选系统音频 + 可选麦克风"]
    L --> M["创建 AVAssetWriterInputPixelBufferAdaptor"]
    M --> N["AVAssetWriter.startWriting"]
    N --> O["SCShareableContent.getShareableContent"]
    O --> P["创建 SCContentFilter"]
    P --> P1["窗口模式: initWithDesktopIndependentWindow"]
    P --> P2["区域模式: initWithDisplay + sourceRect"]
    P1 --> Q["配置 SCStreamConfiguration"]
    P2 --> Q
    Q --> R["创建 SCStream + ScreenRecorderDelegate"]
    R --> S["添加视频输出 SCStreamOutputTypeScreen"]
    S --> T["添加系统音频输出 SCStreamOutputTypeAudio"]
    T --> U["SCStream.startCaptureWithCompletionHandler"]
    U --> V["启动麦克风采集 AVCaptureSession"]
    V --> W["等待初始化完成"]
```

### 7.2 主循环流程

```mermaid
flowchart TD
    A["ScreenRecorderDelegate 回调中心"] --> B["stream:didOutputSampleBuffer:ofType:<br/>SCStream 回调"]
    B --> C{"type == ?"}
    C -- "Screen" --> D["processVideoFrame"]
    D --> D1["pixelBufferAdaptor.appendPixelBuffer"]
    D1 --> D2["AVAssetWriter 内部完成 H.264 编码"]
    C -- "Audio" --> E["processSystemAudioSample"]
    E --> E1["sysAudioInput.appendSampleBuffer"]
    A --> F["captureOutput:didOutputSampleBuffer:<br/>AVCaptureAudioDataOutput 回调"]
    F --> G["micAudioInput.appendSampleBuffer"]
    D2 --> H["AVAssetWriter 统一管理写入"]
    E1 --> H
    G --> H
```

**与 Windows 的区别：**
- macOS 无需手动帧捕获循环，由 `SCStreamDelegate` 驱动
- `AVAssetWriter` 自行管理编码时间戳
- 暂停/恢复通过 `g_isPaused` 标志控制，回调中检查此标志

### 7.3 停止流程

```mermaid
flowchart TD
    A["screenRecorderStop"] --> B["停止麦克风采集"]
    B --> B1["g_micSession stopRunning"]
    B1 --> B2["释放 AVCaptureDeviceInput / AVCaptureAudioDataOutput"]
    B2 --> C["停止 SCStream"]
    C --> C1["g_stream stopCaptureWithCompletionHandler"]
    C1 --> C2["dispatch_group_wait 5s 超时"]
    C2 --> C3["g_stream release"]
    C3 --> D["等待音频队列排空"]
    D --> D1["dispatch_sync g_audioQueue"]
    D1 --> E["标记所有 AVAssetWriterInput 完成"]
    E --> E1["g_videoInput markAsFinished"]
    E1 --> E2["g_sysAudioInput markAsFinished"]
    E2 --> E3["g_micAudioInput markAsFinished"]
    E3 --> F["AVAssetWriter finishWritingWithCompletionHandler"]
    F --> F1["轮询等待 5s 超时"]
    F1 --> G["释放所有资源"]
    G --> H["重置全局状态变量"]
```

## 8. H.264 编码配置

```objc
NSDictionary* videoSettings = @{
    AVVideoCodecKey: AVVideoCodecTypeH264,
    AVVideoWidthKey: @(outputWidthPx),
    AVVideoHeightKey: @(outputHeightPx),
    AVVideoCompressionPropertiesKey: @{
        AVVideoAverageBitRateKey: @(outputWidthPx * outputHeightPx * 3), // 3Mbps/pixel
        AVVideoExpectedSourceFrameRateKey: @(fps),
        AVVideoMaxKeyFrameIntervalKey: @(fps)
    }
};
```

| 参数 | 值 | 说明 |
|------|------|------|
| 编码格式 | H.264 | `AVVideoCodecTypeH264` |
| 分辨率 | 输出像素尺寸 | 宽 × DPR, 高 × DPR |
| 平均比特率 | `width × height × 3` | ~3Mbps/像素，高质量 |
| 期望帧率 | 配置值 | 通常 30fps |
| 关键帧间隔 | 等于帧率 | 每秒一个关键帧 |
| 像素格式 | 32BGRA | `kCVPixelFormatType_32BGRA` |

## 9. 关键时序

```mermaid
sequenceDiagram
    participant UI as UI 线程
    participant SR as ScreenRecorder
    participant Impl as mac_helper.mm
    participant SC as ScreenCaptureKit
    participant AV as AVFoundation

    UI->>SR: startAreaRecording(rect, path, fps)
    SR->>SR: 计算 DPR (QScreen)
    SR->>Impl: startRecordingOnMainThread(...)
    
    Impl->>AV: 创建 AVAssetWriter + AVAssetWriterInput
    AV-->>Impl: 编码链就绪
    Impl->>SC: getShareableContent()
    SC-->>Impl: content (displays + windows)
    Impl->>SC: 创建 SCContentFilter + SCStream
    Impl->>SC: startCaptureWithCompletionHandler()
    SC-->>Impl: g_isRecording = true
    Impl->>AV: 启动麦克风 AVCaptureSession
    
    loop SCStream 视频帧回调
        SC->>Impl: didOutputSampleBuffer:ofType:Screen
        Impl->>AV: appendPixelBuffer (H.264 编码)
    end
    
    loop SCStream 音频帧回调
        SC->>Impl: didOutputSampleBuffer:ofType:Audio
        Impl->>AV: sysAudioInput.appendSampleBuffer
    end
    
    loop 麦克风音频回调
        AV->>Impl: didOutputSampleBuffer
        Impl->>AV: micAudioInput.appendSampleBuffer
    end
    
    UI->>SR: stop()
    SR->>Impl: screenRecorderStop()
    Impl->>AV: stop 麦克风会话
    Impl->>SC: stopCaptureWithCompletionHandler
    Impl->>AV: markAsFinished 所有输入
    Impl->>AV: finishWritingWithCompletionHandler
    AV-->>Impl: MP4 文件完成
    Impl-->>SR: stopped(path)
```

## 10. 错误处理

| 错误场景 | 处理方式 |
|----------|----------|
| macOS 版本 < 13 | `isAvailable()` 返回 false |
| ScreenCaptureKit 权限缺失 | `screenRecorderCheckPermission()` 返回 false |
| AVAssetWriter 创建失败 | 报告错误，终止录制 |
| SCStream startCapture 失败 | 日志错误，终止录制 |
| 麦克风初始化失败 | 警告，继续（仅系统音频） |
| 系统音频捕获失败 | 警告，继续（仅麦克风） |
| AVAssetWriter 写入失败 | 日志错误，继续尝试 |
| 初始化超时 | dispatch_group_wait 5s 超时保护 |
| 停止超时 | AVAssetWriter finishWriting 5s 轮询超时 |
| 取消录制 | 正常退出并删除文件 |

## 11. 文件索引

| 文件 | 说明 |
|------|------|
| `src/recording/platform/ScreenRecorder_mac.cpp` | C++ 平台桥接实现 |
| `src/recording/platform/ScreenRecorder_mac_helper.h` | C 接口声明（extern "C"） |
| `src/recording/platform/ScreenRecorder_mac_helper.mm` | Objective-C 核心实现 |
| `src/recording/ScreenRecorder.h` | 跨平台录屏器接口 |
| `src/recording/ScreenRecorder.cpp` | 跨平台公共逻辑 |