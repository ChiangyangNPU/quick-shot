#ifndef SCREEN_RECORDER_MAC_HELPER_H
#define SCREEN_RECORDER_MAC_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ScreenCaptureKit 窗口信息结构体
 * @author chiangyang
 */
typedef struct {
    int windowId;
    char* title;
    char* ownerName;
    float x;
    float y;
    float width;
    float height;
} ScreenCaptureWindowInfo;

/** @brief 帧回调函数类型 */
typedef void (*FrameCallback)(const uint8_t* pixelData, int width, int height, int64_t timestamp);

/** @brief 设置帧回调函数
 *  @author chiangyang
 */
void screenRecorderSetFrameCallback(FrameCallback callback);

/** @brief 获取可录制的窗口列表
 *  @author chiangyang
 */
int screenRecorderGetAvailableWindows(ScreenCaptureWindowInfo** windows);

/** @brief 释放窗口信息内存
 *  @author chiangyang
 */
void screenRecorderFreeWindowInfo(ScreenCaptureWindowInfo* windows, int count);

/** @brief 启动窗口录制
 *  @author chiangyang
 */
bool screenRecorderStartWindowRecording(int windowId, const char* outputPath, int width, int height, int fps);

/** @brief 启动显示器录制
 *  @author chiangyang
 */
bool screenRecorderStartDisplayRecording(int displayId, int x, int y, int width, int height, const char* outputPath, int fps);

/** @brief 启动区域录制 */
bool screenRecorderStartAreaRecording(float x, float y, float width, float height, const char* outputPath, int fps, float dpr);

/** @brief 暂停录制
 *  @author chiangyang
 */
bool screenRecorderPause();

/** @brief 恢复录制 */
bool screenRecorderResume();

/** @brief 停止录制
 *  @author chiangyang
 */
void screenRecorderStop();

/** @brief 是否正在录制 */
bool screenRecorderIsRecording();

/** @brief 是否暂停
 *  @author chiangyang
 */
bool screenRecorderIsPaused();

/** @brief 设置系统音频录制开关 */
void screenRecorderSetAudioEnabled(bool enabled);

/** @brief 设置麦克风录制开关
 *  @author chiangyang
 */
void screenRecorderSetMicrophoneEnabled(bool enabled);

/** @brief 获取可用音频设备列表 */
int screenRecorderGetAvailableAudioDevices(char*** deviceNames);

/** @brief 释放音频设备内存
 *  @author chiangyang
 */
void screenRecorderFreeAudioDevices(char** devices, int count);

/**
 * @brief 检测屏幕录制权限
 * @return 是否有屏幕录制权限
 * @author chiangyang
 */
bool screenRecorderCheckPermission();

#ifdef __cplusplus
}
#endif

#endif
