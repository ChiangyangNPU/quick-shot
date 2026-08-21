# QuickShot Windows 录屏技术文档

## 1. 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 视频捕获 | Win32 GDI (`BitBlt`/`StretchBlt`) | 屏幕像素抓取 |
| 视频编码 | Media Foundation (H.264) | 硬件加速视频压缩 |
| 音频捕获 | WASAPI（Loopback + 麦克风） | 系统音频回环 + 麦克风输入 |
| 音频编码 | Media Foundation (AAC) | 双声道音频压缩 |
| 容器格式 | MP4 / WMV（降级） | 视频封装 |
| 线程模型 | std::thread + std::atomic | 视频/音频独立线程 |
| 互斥同步 | QMutex | 标注叠加与帧缓冲并发保护 |

## 2. 架构设计

### 2.1 分层架构

```mermaid
flowchart TD
    subgraph L1["UI 控制层"]
        A1["ScreenRecorder.h<br/>跨平台接口"]
        A2["RecordingControlWindow<br/>暂停/恢复/停止"]
    end
    subgraph L2["业务协调层"]
        B1["ScreenRecorder_Impl<br/>状态管理、录制模式"]
    end
    subgraph L3["平台实现层 Q_OS_WIN"]
        C1["PlatformImpl<br/>COM/DC/音频资源"]
        C2["Media Foundation Sink Writer<br/>视频+音频编码"]
        C3["GDI BitBlt<br/>视频捕获"]
        C4["WASAPI<br/>音频双源捕获与混合"]
    end
    subgraph L4["Windows API 层"]
        D1["GDI32.dll / Media Foundation / WASAPI"]
    end
    L1 --> L2 --> L3 --> L4
```

### 2.2 核心数据结构

```cpp
struct PlatformImpl {
    // Media Foundation 编码链
    IMFSinkWriter *writer = nullptr;       // Sink Writer（视频+音频编码）
    DWORD videoStreamIndex = 0;            // 视频流索引
    DWORD audioStreamIndex = 0;             // 音频流索引
    LONGLONG frameDuration100ns = 0;        // 单帧时长（100ns 单位）

    // GDI 绘图资源
    HDC screenDc;                          // 屏幕 DC
    HDC srcDc;                             // 源 DC（存储捕获的原始画面）
    HBITMAP srcBmp;                        // 源位图
    HDC dstDc;                             // 目标 DC（存储缩放后的画面）
    HBITMAP dstBmp;                        // 目标位图
    BITMAPINFO bmi;                        // 位图信息头

    // WASAPI 系统音频（Loopback 回环）
    IAudioClient *systemAudioClient;       // 音频客户端
    IAudioCaptureClient *systemCaptureClient; // 捕获客户端
    HANDLE systemAudioEvent;               // 事件句柄
    WAVEFORMATEX systemAudioFmt;           // 音频格式

    // WASAPI 麦克风音频
    IAudioClient *micAudioClient;
    IAudioCaptureClient *micCaptureClient;
    HANDLE micAudioEvent;
    WAVEFORMATEX micAudioFmt;

    // 音频捕获线程
    std::thread audioThread;
    std::atomic<bool> audioRunning{false};

    // 录制起始时间（音频时间戳同步）
    LONGLONG recordingStartTime100ns = 0;
};
```

## 3. 视频捕获流程

### 3.1 GDI 捕获管线

```mermaid
flowchart LR
    A["屏幕"] --> B["BitBlt<br/>源区域捕获"]
    B --> C["srcDc/srcBmp<br/>物理像素坐标"]
    C --> D["StretchBlt<br/>尺寸缩放"]
    D --> E["dstDc/dstBmp<br/>输出分辨率"]
    E --> F["GetDIBits<br/>像素格式转换"]
    F --> G["frameBuffer<br/>RGB32 帧缓冲"]
```

**详细步骤：**

1. **获取屏幕 DC**：`GetDC(nullptr)` 获取整个虚拟桌面的设备上下文
2. **创建兼容 DC 和位图**：`CreateCompatibleDC`/`CreateCompatibleBitmap` 创建源和目标缓冲区
3. **源区域捕获**：`BitBlt(srcDc, 0, 0, cap.width, cap.height, screenDc, cap.x, cap.y, SRCCOPY | CAPTUREBLT)`
   - `CAPTUREBLT`：包含分层窗口（如透明窗口）
   - 捕获区域由 `ScreenRecorder_Impl::captureRectPx` 指定（虚拟桌面坐标，物理像素）
4. **尺寸缩放**：`StretchBlt(dstDc, 0, 0, out.w, out.h, srcDc, 0, 0, cap.w, cap.h, SRCCOPY)`
   - 当捕获区域与输出分辨率不同时进行缩放
   - 使用 `HALFTONE` 模式实现高质量缩放
5. **像素读取**：`GetDIBits(dstDc, dstBmp, 0, out.h, frameBuffer, &bmi, DIB_RGB_COLORS)`
   - 输出为 32-bit BGRA 格式帧缓冲

### 3.2 标注叠加合成

```cpp
// 在视频帧捕获后、写入编码前合成标注
QMutexLocker lock(&impl->annotationMutex);
if (!impl->annotationOverlay.isNull()) {
    QImage frameImage(reinterpret_cast<uchar*>(impl->frameBuffer.data()),
                      out.width(), out.height(), QImage::Format_RGB32);
    QPainter fp(&frameImage);
    fp.setCompositionMode(QPainter::CompositionMode_SourceOver);
    fp.drawImage(0, 0, impl->annotationOverlay);
    fp.end();
}
```

- UI 线程在标注变化时调用 `ScreenRecorder::setAnnotationOverlay()` 更新叠加图像
- 录制线程每帧在 `annotationMutex` 保护下合成
- 使用 `QPainter::CompositionMode_SourceOver` 实现 Alpha 混合

### 3.3 编码写入流程

```mermaid
flowchart LR
    A["frameBuffer"] --> B["MFCreateMemoryBuffer<br/>填充像素数据"]
    B --> C["MFCreateSample<br/>设置时间戳/时长"]
    C --> D["writer->WriteSample<br/>H.264 编码 → MP4 封装"]
```

**关键参数：**
- 编码格式：H.264（`MFVideoFormat_H264`），6Mbps 比特率
- 输入格式：RGB32，固定步长 `width * 4`
- 帧率：可配置（默认 30fps），帧时长 = `10000000 / fps`（100ns 单位）

## 4. 音频捕获流程

### 4.1 WASAPI 双源捕获

```mermaid
flowchart TD
    subgraph S1["系统音频 Loopback"]
        A1["IMMDeviceEnumerator<br/>eRender 端点"] --> A2["IAudioClient<br/>LOOPBACK flag"]
        A2 --> A3["IAudioCaptureClient"]
    end
    subgraph S2["麦克风 Capture"]
        B1["IMMDeviceEnumerator<br/>eCapture 端点"] --> B2["IAudioClient<br/>normal capture"]
        B2 --> B3["IAudioCaptureClient"]
    end
    A3 --> C["PCM 采样读取<br/>浮点→PCM16 转换"]
    B3 --> D["PCM 采样读取<br/>浮点→PCM16 转换"]
    C --> E["系统音频缓冲区"]
    D --> F["麦克风音频缓冲区"]
    E --> G["音频混合线程<br/>audioCaptureMixedThread"]
    F --> G
    G --> H["音频帧混合<br/>双源取平均 (s1+s2)/2"]
    H --> I["writeAudioToSinkWriter<br/>AAC 编码 → 写入 MP4"]
```

**初始化关键步骤（`initAudioCapture`）：**

1. `CoCreateInstance(IMMDeviceEnumerator)` 创建设备枚举器
2. 根据 `isLoopback` 选择端点：
   - 系统音频：`eRender` + `eConsole`（Loopback 回环）
   - 麦克风：`eCapture` + `eConsole`
3. `IAudioClient::Initialize`：`AUDCLNT_SHAREMODE_SHARED` + `AUDCLNT_STREAMFLAGS_LOOPBACK`（系统音频）+ `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`
4. 获取 `IAudioCaptureClient` 用于读取音频包

**音频混合策略：**
- 单音频源：直接写入 Sink Writer
- 双音频源（系统+麦克风）：逐帧线性混合 `(s1 + s2) / 2`
- 使用 `WaitForMultipleObjects` 等待双源事件，同步采样时间
- 缓冲区溢出保护：超过 2 秒数据时丢弃最旧数据

### 4.2 音频编码

- 编码格式：AAC（`MFAudioFormat_AAC`），128kbps
- 采样率：跟随系统音频混合格式（通常 48000Hz）
- 声道数：跟随源格式（通常 2 声道立体声）
- 位深度：16-bit PCM 输入 → AAC 输出

## 5. 完整录制流程

### 5.1 启动流程

```mermaid
flowchart TD
    A["screenRecorderImplStart(impl)"] --> B["COM 初始化 + Media Foundation 启动"]
    B --> C["音频捕获初始化"]
    C --> C1["initAudioCapture 系统音频 isLoopback=true"]
    C --> C2["initAudioCapture 麦克风 isLoopback=false"]
    C1 --> D["创建输出目录"]
    C2 --> D
    D --> E["Sink Writer 创建 带降级策略"]
    E --> E1["尝试 H.264 + MP4"]
    E1 --> E2["失败则降级 WMV + .wmv"]
    E2 --> F["启动音频捕获线程"]
    F --> F1["audioCaptureMixedThread 双源混合"]
    F1 --> G["初始化 GDI 资源"]
    G --> G1["GetDC nullptr"]
    G1 --> G2["CreateCompatibleDC / CreateCompatibleBitmap"]
    G2 --> G3["BITMAPINFO 配置"]
    G3 --> H["进入视频捕获主循环"]
```

### 5.2 主循环流程

```mermaid
flowchart TD
    A["while running"] --> B{"paused?"}
    B -- "是" --> B1["sleep 30ms"]
    B1 --> A
    B -- "否" --> C["视频捕获"]
    C --> C1["BitBlt 源区域 → srcDc"]
    C1 --> C2["StretchBlt srcDc → dstDc 尺寸适配"]
    C2 --> C3["GetDIBits dstDc → frameBuffer"]
    C3 --> D["标注合成"]
    D --> D1["QPainter drawImage annotationOverlay"]
    D1 --> E["编码写入"]
    E --> E1["MFCreateMemoryBuffer frameBuffer"]
    E1 --> E2["MFCreateSample + SetSampleTime ts"]
    E2 --> E3["writer->WriteSample videoStreamIndex"]
    E3 --> F["时间推进"]
    F --> F1["ts += frameDuration100ns"]
    F1 --> F2["sleep_until nextTick"]
    F2 --> G["frameCount++"]
    G --> A
```

### 5.3 停止流程

```mermaid
flowchart TD
    A["screenRecorderImplStop impl"] --> B["impl->running = false 原子标志"]
    B --> C["视频循环退出"]
    C --> D["音频线程等待退出"]
    D --> E["writer->Finalize 完成 MP4 封装"]
    E --> F["GDI 资源释放"]
    F --> F1["DeleteObject dstBmp/srcBmp"]
    F1 --> F2["DeleteDC dstDc/srcDc"]
    F2 --> F3["ReleaseDC nullptr, screenDc"]
    F3 --> G["音频资源释放"]
    G --> G1["audioClient->Stop"]
    G1 --> G2["safeRelease capture/client"]
    G2 --> G3["CloseHandle event"]
    G3 --> H["Sink Writer 释放"]
    H --> I["MFShutdown + CoUninitialize"]
    I --> J{"取消?"}
    J -- "是" --> K["删除视频文件"]
    J -- "否" --> L["emit stopped outputPath"]
    K --> L
```

## 6. 降级策略

### 6.1 视频编码降级

```mermaid
flowchart TD
    A["H.264 + MP4 创建失败"] --> B["WMV3 + .wmv 降级格式"]
    B --> C{"成功?"}
    C -- "是" --> D["切换输出路径为 .wmv"]
    C -- "否" --> E["报告错误<br/>提示系统缺少 Media Feature Pack"]
```

降级原因：Windows N/KN 版本（欧洲市场）默认不含 H.264 编码组件。

### 6.2 音频降级

- 系统音频初始化失败 → 继续录制，仅使用麦克风
- 麦克风初始化失败 → 继续录制，仅使用系统音频
- 双源均失败 → 纯视频录制（无音频）

## 7. 关键时序

```mermaid
sequenceDiagram
    participant UI as UI 线程
    participant SR as ScreenRecorder
    participant Worker as 录制线程
    participant Audio as 音频线程
    participant GDI as GDI/Media Foundation

    UI->>SR: start(rect, size, path, fps)
    SR->>Worker: 启动录制线程
    
    Worker->>GDI: CoInitializeEx + MFStartup
    Worker->>Audio: initAudioCapture(系统+麦克风)
    Worker->>GDI: createSinkWriter(H.264)
    GDI-->>Worker: writer ready
    Worker->>Audio: 启动音频混合线程
    Worker->>GDI: GetDC + CreateCompatibleDC
    
    loop 每帧 (1000/fps ms)
        Worker->>GDI: BitBlt 屏幕捕获
        GDI-->>Worker: srcBmp 原始画面
        Worker->>GDI: StretchBlt 尺寸缩放
        GDI-->>Worker: dstBmp 输出画面
        Worker->>Worker: 合成标注 overlay
        Worker->>GDI: WriteSample 写入视频
        Audio->>GDI: WriteSample 写入音频
    end

    UI->>SR: stop()
    SR->>Worker: running = false
    Worker->>Audio: 等待音频线程退出
    Worker->>GDI: writer->Finalize()
    Worker->>GDI: 释放所有 GDI/COM 资源
    Worker-->>UI: stopped(path)
```

## 8. 错误处理

| 错误场景 | 处理方式 |
|----------|----------|
| Media Foundation 初始化失败 | 报告错误，终止录制 |
| H.264 Sink Writer 创建失败 | 降级 WMV → 再失败则报告 |
| GDI 屏幕捕获失败 | 报告错误，终止录制 |
| 编码写入失败 | 报告错误，终止录制 |
| 系统音频初始化失败 | 警告，继续（仅麦克风） |
| 麦克风初始化失败 | 警告，继续（仅系统音频） |
| 音频写入失败 | 日志错误，继续视频录制 |
| 取消录制 | 正常退出并删除文件 |

## 9. 文件索引

| 文件 | 说明 |
|------|------|
| `src/recording/platform/ScreenRecorder_win.cpp` | Windows 平台录屏完整实现 |
| `src/recording/ScreenRecorder.h` | 跨平台录屏器接口定义 |
| `src/recording/ScreenRecorder.cpp` | 跨平台公共逻辑 |
| `src/recording/RecordingControlWindow.h` | 录制控制栏 UI |