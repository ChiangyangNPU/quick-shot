#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#import "ScreenRecorder_mac_helper.h"

// ===== 全局状态 =====

static FrameCallback g_frameCallback = nullptr;
static bool g_isRecording = false;
static bool g_isPaused = false;
static bool g_audioEnabled = false;
static bool g_microphoneEnabled = false;

// ScreenCaptureKit
static SCStream* g_stream = nil;
static SCContentFilter* g_contentFilter = nil;
static SCStreamConfiguration* g_streamConfig = nil;

// AVAssetWriter (视频 + 音频输出)
static AVAssetWriter* g_assetWriter = nil;
static AVAssetWriterInput* g_videoInput = nil;
static AVAssetWriterInput* g_sysAudioInput = nil;   // 系统音频输入
static AVAssetWriterInput* g_micAudioInput = nil;    // 麦克风音频输入
static AVAssetWriterInputPixelBufferAdaptor* g_pixelBufferAdaptor = nil;
static dispatch_queue_t g_videoQueue = nil;
static dispatch_queue_t g_audioQueue = nil;

// 麦克风采集
static AVCaptureSession* g_micSession = nil;
static AVCaptureDeviceInput* g_micDeviceInput = nil;
static AVCaptureAudioDataOutput* g_micOutput = nil;
static dispatch_queue_t g_micQueue = nil;

static CMTime g_startTime = kCMTimeInvalid;
static int g_frameWidth = 0;
static int g_frameHeight = 0;
static int g_fps = 30;
static int g_frameCount = 0;

static bool g_initializationComplete = false;
static bool g_initializationSuccess = false;

// ===== Delegate =====

@interface ScreenRecorderDelegate : NSObject <SCStreamOutput, SCStreamDelegate, AVCaptureAudioDataOutputSampleBufferDelegate>
@end

@implementation ScreenRecorderDelegate

// SCStream 回调：视频帧 + 系统音频帧
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    if (!g_isRecording || !sampleBuffer || g_isPaused) {
        return;
    }

    if (type == SCStreamOutputTypeScreen) {
        [self processVideoFrame:sampleBuffer];
    } else if (type == SCStreamOutputTypeAudio) {
        [self processSystemAudioSample:sampleBuffer];
    }
}

- (void)processVideoFrame:(CMSampleBufferRef)sampleBuffer {
    if (!CMSampleBufferDataIsReady(sampleBuffer)) {
        return;
    }

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) {
        return;
    }

    g_frameCount++;
    if (g_frameCount % 30 == 0) {
        NSLog(@"Processing frame %d", g_frameCount);
    }

    if (!g_assetWriter || !g_videoInput || ![g_videoInput isReadyForMoreMediaData]) {
        return;
    }

    if (CMTIME_IS_INVALID(g_startTime)) {
        CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        [g_assetWriter startSessionAtSourceTime:presentationTime];
        g_startTime = presentationTime;
        NSLog(@"Started session at time: %f", CMTimeGetSeconds(presentationTime));
    }

    CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    BOOL appended = [g_pixelBufferAdaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentationTime];
    if (!appended) {
        NSLog(@"Failed to append frame at time: %f", CMTimeGetSeconds(presentationTime));
    } else if (g_frameCount % 30 == 0) {
        NSLog(@"Appended frame at time: %f", CMTimeGetSeconds(presentationTime));
    }
}

// SCStream 回调：系统音频
- (void)processSystemAudioSample:(CMSampleBufferRef)sampleBuffer {
    if (!g_audioEnabled || !g_sysAudioInput || ![g_sysAudioInput isReadyForMoreMediaData]) {
        return;
    }
    if (CMTIME_IS_INVALID(g_startTime)) {
        return; // 视频 session 尚未启动
    }
    [g_sysAudioInput appendSampleBuffer:sampleBuffer];
}

// AVCaptureAudioDataOutput 回调：麦克风音频
- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {

    if (!g_isRecording || !g_microphoneEnabled || g_isPaused) {
        return;
    }
    if (!g_micAudioInput || ![g_micAudioInput isReadyForMoreMediaData]) {
        return;
    }
    if (CMTIME_IS_INVALID(g_startTime)) {
        return; // 视频 session 尚未启动
    }
    [g_micAudioInput appendSampleBuffer:sampleBuffer];
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    NSLog(@"Stream stopped with error: %@", error);
    g_isRecording = false;
}

@end

static ScreenRecorderDelegate* g_delegate = nil;

// ===== 麦克风采集管理 =====

static bool initMicrophoneCapture() {
    AVCaptureDevice* micDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    if (!micDevice) {
        NSLog(@"No microphone device found");
        return false;
    }

    g_micSession = [[AVCaptureSession alloc] init];

    NSError* error = nil;
    g_micDeviceInput = [AVCaptureDeviceInput deviceInputWithDevice:micDevice error:&error];
    if (error || !g_micDeviceInput) {
        NSLog(@"Failed to create mic input: %@", error);
        g_micSession = nil;
        return false;
    }
    if (![g_micSession canAddInput:g_micDeviceInput]) {
        NSLog(@"Cannot add mic input to session");
        g_micSession = nil;
        g_micDeviceInput = nil;
        return false;
    }
    [g_micSession addInput:g_micDeviceInput];

    g_micOutput = [[AVCaptureAudioDataOutput alloc] init];
    g_micQueue = dispatch_queue_create("com.quickshot.miccapture", DISPATCH_QUEUE_SERIAL);
    [g_micOutput setSampleBufferDelegate:g_delegate queue:g_micQueue];

    if (![g_micSession canAddOutput:g_micOutput]) {
        NSLog(@"Cannot add mic output to session");
        g_micSession = nil;
        g_micDeviceInput = nil;
        g_micOutput = nil;
        return false;
    }
    [g_micSession addOutput:g_micOutput];

    [g_micSession startRunning];
    NSLog(@"Microphone capture started");
    return true;
}

static void cleanupMicrophoneCapture() {
    if (g_micSession) {
        [g_micSession stopRunning];
        if (g_micDeviceInput) {
            [g_micSession removeInput:g_micDeviceInput];
        }
        if (g_micOutput) {
            [g_micSession removeOutput:g_micOutput];
        }
    }
    g_micSession = nil;
    g_micDeviceInput = nil;
    g_micOutput = nil;
    g_micQueue = nil;
    NSLog(@"Microphone capture cleaned up");
}

// ===== 音频 AVAssetWriterInput 创建 =====

static AVAssetWriterInput* createAudioWriterInput(NSString* label) {
    AudioChannelLayout stereoLayout;
    memset(&stereoLayout, 0, sizeof(stereoLayout));
    stereoLayout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
    NSData* layoutData = [NSData dataWithBytes:&stereoLayout length:sizeof(stereoLayout)];

    NSDictionary* settings = @{
        AVFormatIDKey: @(kAudioFormatMPEG4AAC),
        AVSampleRateKey: @(44100),
        AVNumberOfChannelsKey: @(2),
        AVChannelLayoutKey: layoutData,
        AVEncoderBitRateKey: @(128000)
    };

    AVAssetWriterInput* input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                               outputSettings:settings];
    input.expectsMediaDataInRealTime = YES;
    NSLog(@"Created audio writer input: %@", label);
    return input;
}

// ===== 公共接口 =====

void screenRecorderSetFrameCallback(FrameCallback callback) {
    g_frameCallback = callback;
}

int screenRecorderGetAvailableWindows(ScreenCaptureWindowInfo** windows) {
    if (@available(macOS 12.3, *)) {
        __block NSArray<SCWindow*>* availableWindows = nil;
        dispatch_group_t group = dispatch_group_create();

        dispatch_group_enter(group);
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                   onScreenWindowsOnly:YES
                                                          completionHandler:^(SCShareableContent *content, NSError *error) {
            if (!error && content) {
                availableWindows = content.windows;
            }
            dispatch_group_leave(group);
        }];

        dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));

        if (!availableWindows) {
            return 0;
        }

        int count = (int)[availableWindows count];
        if (count == 0 || windows == NULL) {
            return count;
        }

        *windows = (ScreenCaptureWindowInfo*)malloc(sizeof(ScreenCaptureWindowInfo) * count);

        for (int i = 0; i < count; i++) {
            SCWindow* window = availableWindows[i];
            CGRect frame = window.frame;

            (*windows)[i].windowId = (int)window.windowID;
            (*windows)[i].x = frame.origin.x;
            (*windows)[i].y = frame.origin.y;
            (*windows)[i].width = frame.size.width;
            (*windows)[i].height = frame.size.height;

            NSString* title = window.title;
            if (title && [title length] > 0) {
                (*windows)[i].title = strdup([title UTF8String]);
            } else {
                (*windows)[i].title = strdup("");
            }

            SCRunningApplication* owner = window.owningApplication;
            NSString* ownerName = owner.applicationName ?: @"Unknown";
            if (ownerName) {
                (*windows)[i].ownerName = strdup([ownerName UTF8String]);
            } else {
                (*windows)[i].ownerName = strdup("Unknown");
            }
        }

        return count;
    }
    return 0;
}

void screenRecorderFreeWindowInfo(ScreenCaptureWindowInfo* windows, int count) {
    if (!windows) return;
    for (int i = 0; i < count; i++) {
        if (windows[i].title) free(windows[i].title);
        if (windows[i].ownerName) free(windows[i].ownerName);
    }
    free(windows);
}

// ===== 核心录制启动 =====

static bool startRecordingOnMainThread(NSString* outputPath,
                                        int sourceX, int sourceY,
                                        int sourceWidth, int sourceHeight,
                                        int outputWidth, int outputHeight,
                                        int fps, bool captureWindow, int windowId,
                                        float dpr) {
    NSLog(@"Starting recording: %@, source=(%d,%d %dx%d), output=%dx%d, fps=%d, captureWindow=%d, dpr=%.2f",
          outputPath, sourceX, sourceY, sourceWidth, sourceHeight, outputWidth, outputHeight, fps, captureWindow, dpr);

    // 重置状态
    g_startTime = kCMTimeInvalid;
    g_frameCount = 0;
    // 计算输出像素尺寸：macOS 上 Qt 坐标为逻辑点，乘以 DPR 得到实际像素数
    CGFloat scale = (dpr > 0) ? dpr : 1.0;
    int outputWidthPx = (int)(outputWidth * scale);
    int outputHeightPx = (int)(outputHeight * scale);
    g_frameWidth = outputWidthPx;
    g_frameHeight = outputHeightPx;
    g_fps = fps;

    // 清理旧文件
    NSFileManager* fileManager = [NSFileManager defaultManager];
    if ([fileManager fileExistsAtPath:outputPath]) {
        [fileManager removeItemAtPath:outputPath error:nil];
    }

    // 创建 AVAssetWriter
    NSURL* outputURL = [NSURL fileURLWithPath:outputPath];
    NSError* error = nil;
    g_assetWriter = [[AVAssetWriter alloc] initWithURL:outputURL fileType:AVFileTypeMPEG4 error:&error];
    if (error) {
        NSLog(@"Failed to create asset writer: %@", error);
        return false;
    }

    // ---- 视频输入 ----
    NSDictionary* videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(outputWidthPx),
        AVVideoHeightKey: @(outputHeightPx),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(outputWidthPx * outputHeightPx * 3),
            AVVideoExpectedSourceFrameRateKey: @(fps),
            AVVideoMaxKeyFrameIntervalKey: @(fps)
        }
    };

    g_videoInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
    g_videoInput.expectsMediaDataInRealTime = YES;

    NSDictionary* pixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(outputWidthPx),
        (id)kCVPixelBufferHeightKey: @(outputHeightPx)
    };

    g_pixelBufferAdaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput:g_videoInput
                                                                        sourcePixelBufferAttributes:pixelBufferAttributes];

    if ([g_assetWriter canAddInput:g_videoInput]) {
        [g_assetWriter addInput:g_videoInput];
    } else {
        NSLog(@"Cannot add video input");
        return false;
    }

    // ---- 系统音频输入 ----
    g_sysAudioInput = nil;
    if (g_audioEnabled) {
        g_sysAudioInput = createAudioWriterInput(@"system-audio");
        if ([g_assetWriter canAddInput:g_sysAudioInput]) {
            [g_assetWriter addInput:g_sysAudioInput];
        } else {
            NSLog(@"Cannot add system audio input");
            g_sysAudioInput = nil;
        }
    }

    // ---- 麦克风音频输入 ----
    g_micAudioInput = nil;
    if (g_microphoneEnabled) {
        g_micAudioInput = createAudioWriterInput(@"microphone");
        if ([g_assetWriter canAddInput:g_micAudioInput]) {
            [g_assetWriter addInput:g_micAudioInput];
        } else {
            NSLog(@"Cannot add microphone audio input");
            g_micAudioInput = nil;
        }
    }

    // 创建视频和音频处理队列
    g_videoQueue = dispatch_queue_create("com.quickshot.videocapture", DISPATCH_QUEUE_SERIAL);
    if (g_audioEnabled && g_sysAudioInput) {
        g_audioQueue = dispatch_queue_create("com.quickshot.audiocapture", DISPATCH_QUEUE_SERIAL);
    }

    // 开始写入
    if (![g_assetWriter startWriting]) {
        NSLog(@"Failed to start writing: %@", g_assetWriter.error);
        return false;
    }

    // ---- 创建 SCStream ----
    if (@available(macOS 12.3, *)) {
        dispatch_group_t group = dispatch_group_create();
        dispatch_group_enter(group);

        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                   onScreenWindowsOnly:YES
                                                          completionHandler:^(SCShareableContent *content, NSError *scError) {
            if (scError || !content) {
                NSLog(@"Failed to get shareable content: %@", scError);
                dispatch_group_leave(group);
                return;
            }

            SCContentFilter* filter = nil;
            SCDisplay* targetDisplay = nil;

            if (captureWindow) {
                // 窗口录制模式
                SCWindow* targetWindow = nil;
                for (SCWindow* window in content.windows) {
                    if ((int)window.windowID == windowId) {
                        targetWindow = window;
                        break;
                    }
                }
                if (targetWindow) {
                    filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
                }
            } else {
                // 区域/全屏录制模式：找到包含选区中心的显示器
                // macOS 上 Qt 坐标为逻辑点，SCDisplay.frame 也是逻辑点，直接比较即可
                if (content.displays.count > 0) {
                    CGFloat centerX = sourceX + sourceWidth / 2.0;
                    CGFloat centerY = sourceY + sourceHeight / 2.0;

                    for (SCDisplay* display in content.displays) {
                        if (CGRectContainsPoint(display.frame, CGPointMake(centerX, centerY))) {
                            targetDisplay = display;
                            break;
                        }
                    }
                    // 回退：如果没找到匹配的显示器，使用第一个
                    if (!targetDisplay) {
                        targetDisplay = content.displays.firstObject;
                    }
                }
                if (targetDisplay) {
                    filter = [[SCContentFilter alloc] initWithDisplay:targetDisplay
                                                  excludingApplications:@[]
                                                           exceptingWindows:@[]];
                }
            }

            if (!filter) {
                NSLog(@"Failed to create content filter");
                dispatch_group_leave(group);
                return;
            }

            g_contentFilter = filter;

            // ---- 配置 SCStream ----
            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];

            if (captureWindow) {
                // 窗口录制：不设置 sourceRect，全窗口捕获
                config.width = outputWidthPx;
                config.height = outputHeightPx;
            } else {
                // 区域录制：设置 sourceRect 裁剪到指定区域
                // macOS 上 Qt 坐标已是逻辑点（points），与 ScreenCaptureKit 的
                // sourceRect 坐标系一致，无需 DPR 转换。
                // config.width/height 使用预计算的像素尺寸 outputWidthPx/outputHeightPx。
                CGFloat rectX = sourceX - (targetDisplay ? targetDisplay.frame.origin.x : 0);
                CGFloat rectY = sourceY - (targetDisplay ? targetDisplay.frame.origin.y : 0);
                CGFloat rectW = sourceWidth;
                CGFloat rectH = sourceHeight;
                config.sourceRect = CGRectMake(rectX, rectY, rectW, rectH);
                config.width = outputWidthPx;
                config.height = outputHeightPx;
                NSLog(@"sourceRect (points): (%.1f, %.1f, %.1f, %.1f) display origin: (%.0f,%.0f), output (px): %ldx%ld, dpr: %.2f, input: (%d,%d,%d,%d)",
                      rectX, rectY, rectW, rectH,
                      targetDisplay ? targetDisplay.frame.origin.x : 0.0,
                      targetDisplay ? targetDisplay.frame.origin.y : 0.0,
                      (long)config.width, (long)config.height,
                      scale, sourceX, sourceY, sourceWidth, sourceHeight);
            }

            config.minimumFrameInterval = CMTimeMake(1, fps);
            config.queueDepth = 3;
            config.showsCursor = YES;
            config.pixelFormat = kCVPixelFormatType_32BGRA;

            // 音频配置
            config.capturesAudio = (g_audioEnabled && g_sysAudioInput != nil);
            if (@available(macOS 13.0, *)) {
                config.excludesCurrentProcessAudio = YES;
            }
            if (config.capturesAudio) {
                config.sampleRate = 44100;
                config.channelCount = 2;
                NSLog(@"System audio capture enabled");
            }

            g_streamConfig = config;

            // 创建 SCStream
            g_delegate = [[ScreenRecorderDelegate alloc] init];

            NSError* streamError = nil;
            g_stream = [[SCStream alloc] initWithFilter:filter
                                          configuration:config
                                               delegate:g_delegate];

            // 添加视频输出
            if (![g_stream addStreamOutput:g_delegate
                                      type:SCStreamOutputTypeScreen
                           sampleHandlerQueue:g_videoQueue
                                       error:&streamError]) {
                NSLog(@"Failed to add video stream output: %@", streamError);
            }

            // 添加系统音频输出
            if (config.capturesAudio && g_audioQueue) {
                NSError* audioError = nil;
                if (![g_stream addStreamOutput:g_delegate
                                         type:SCStreamOutputTypeAudio
                              sampleHandlerQueue:g_audioQueue
                                          error:&audioError]) {
                    NSLog(@"Failed to add audio stream output: %@", audioError);
                } else {
                    NSLog(@"Audio stream output added");
                }
            }

            // 启动 SCStream
            [g_stream startCaptureWithCompletionHandler:^(NSError *err) {
                if (err) {
                    NSLog(@"Failed to start capture: %@", err);
                } else {
                    NSLog(@"Capture started successfully");
                    g_isRecording = true;
                }
                dispatch_group_leave(group);
            }];
        }];

        dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
    }

    if (!g_isRecording) {
        NSLog(@"Recording failed to start");
        return false;
    }

    // ---- 启动麦克风采集 ----
    if (g_microphoneEnabled && g_micAudioInput) {
        if (!initMicrophoneCapture()) {
            NSLog(@"Warning: microphone capture failed to start, continuing without mic");
            // 移除麦克风 writer input
            if (g_micAudioInput) {
                [g_micAudioInput markAsFinished];
                g_micAudioInput = nil;
            }
        }
    }

    NSLog(@"Recording initialized successfully");
    return true;
}

// ===== 平台录制入口 =====

bool screenRecorderStartWindowRecording(int windowId, const char* outputPath, int width, int height, int fps) {
    NSString* path = [NSString stringWithUTF8String:outputPath];
    return startRecordingOnMainThread(path, 0, 0, width, height, width, height, fps, true, windowId, 1.0f);
}

bool screenRecorderStartDisplayRecording(int displayId, int x, int y, int width, int height, const char* outputPath, int fps) {
    NSString* path = [NSString stringWithUTF8String:outputPath];
    return startRecordingOnMainThread(path, x, y, width, height, width, height, fps, false, 0, 1.0f);
}

bool screenRecorderStartAreaRecording(float x, float y, float width, float height, const char* outputPath, int fps, float dpr) {
    NSString* path = [NSString stringWithUTF8String:outputPath];
    return startRecordingOnMainThread(path, (int)x, (int)y, (int)width, (int)height, (int)width, (int)height, fps, false, 0, dpr);
}

// ===== 暂停/恢复/停止 =====

bool screenRecorderPause() {
    if (!g_isRecording) return false;
    g_isPaused = true;
    return true;
}

bool screenRecorderResume() {
    if (!g_isRecording) return false;
    g_isPaused = false;
    return true;
}

void screenRecorderStop() {
    if (!g_isRecording && !g_stream && !g_assetWriter) {
        NSLog(@"Not recording, nothing to stop");
        return;
    }

    NSLog(@"Stopping recording...");

    g_isRecording = false;
    g_isPaused = false;

    // 1. 停止麦克风采集
    cleanupMicrophoneCapture();

    // 2. 停止 SCStream
    if (g_stream) {
        dispatch_group_t group = dispatch_group_create();
        dispatch_group_enter(group);

        [g_stream stopCaptureWithCompletionHandler:^(NSError *error) {
            if (error) {
                NSLog(@"Stream stop error: %@", error);
            } else {
                NSLog(@"Stream stopped");
            }
            dispatch_group_leave(group);
        }];

        dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        [g_stream release];
        g_stream = nil;
    }

    if (g_delegate) {
        [g_delegate release];
        g_delegate = nil;
    }

    // 3. 等待音频队列排空
    if (g_audioQueue) {
        dispatch_sync(g_audioQueue, ^{
            NSLog(@"Audio queue drained");
        });
    }

    // 4. 标记所有输入完成
    if (g_videoInput) {
        [g_videoInput markAsFinished];
    }
    if (g_sysAudioInput) {
        [g_sysAudioInput markAsFinished];
    }
    if (g_micAudioInput) {
        [g_micAudioInput markAsFinished];
    }

    // 5. 完成写入
    NSLog(@"Finalizing AVAssetWriter...");
    if (g_assetWriter) {
        [g_assetWriter finishWritingWithCompletionHandler:^{
            NSLog(@"AVAssetWriter finished, status: %ld, error: %@",
                  (long)g_assetWriter.status, g_assetWriter.error);
        }];

        // 等待完成（最多 5 秒）
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
        while (g_assetWriter.status == AVAssetWriterStatusWriting &&
               [[NSDate date] compare:deadline] == NSOrderedAscending) {
            [NSThread sleepForTimeInterval:0.05];
        }

        [g_assetWriter release];
        g_assetWriter = nil;
    }

    // 6. 清理引用
    g_videoInput = nil;
    g_sysAudioInput = nil;
    g_micAudioInput = nil;
    g_pixelBufferAdaptor = nil;
    g_contentFilter = nil;
    g_streamConfig = nil;

    if (g_videoQueue) {
        dispatch_release(g_videoQueue);
        g_videoQueue = nil;
    }
    if (g_audioQueue) {
        dispatch_release(g_audioQueue);
        g_audioQueue = nil;
    }

    g_startTime = kCMTimeInvalid;
    g_frameCount = 0;

    NSLog(@"Recording stopped and cleaned up");
}

// ===== 状态查询 =====

bool screenRecorderIsRecording() {
    return g_isRecording;
}

bool screenRecorderIsPaused() {
    return g_isPaused;
}

// ===== 音频控制 =====

void screenRecorderSetAudioEnabled(bool enabled) {
    g_audioEnabled = enabled;
    NSLog(@"System audio recording %s", enabled ? "enabled" : "disabled");
}

void screenRecorderSetMicrophoneEnabled(bool enabled) {
    g_microphoneEnabled = enabled;
    NSLog(@"Microphone recording %s", enabled ? "enabled" : "disabled");
}

// ===== 权限检测 =====

bool screenRecorderCheckPermission() {
    if (@available(macOS 14.0, *)) {
        return CGPreflightScreenCaptureAccess();
    }

    // macOS 13: 尝试获取 SCShareableContent 来检测权限
    __block bool hasAccess = false;
    dispatch_group_t group = dispatch_group_create();
    dispatch_group_enter(group);
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                               onScreenWindowsOnly:YES
                                                      completionHandler:^(SCShareableContent *content, NSError *error) {
        hasAccess = (content != nil && error == nil);
        dispatch_group_leave(group);
    }];
    dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
    return hasAccess;
}

// ===== 音频设备 =====

int screenRecorderGetAvailableAudioDevices(char*** deviceNames) {
    // 返回默认设备列表（简化实现）
    return 0;
}

void screenRecorderFreeAudioDevices(char** devices, int count) {
    if (!devices) return;
    for (int i = 0; i < count; i++) {
        if (devices[i]) free(devices[i]);
    }
    free(devices);
}
