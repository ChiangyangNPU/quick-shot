#include "ScreenRecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include "../log/Logger.h"
#include "../core/TranslationManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <mmreg.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")

/**
 * @brief Windows 平台录屏实现的内部状态
 *
 * 包含 Media Foundation Sink Writer、DC/位图资源和 WASAPI 音频捕获客户端。
 * @author chiangyang
 */
struct PlatformImpl {
    IMFSinkWriter *writer = nullptr;
    DWORD videoStreamIndex = 0;
    DWORD audioStreamIndex = 0;
    LONGLONG frameDuration100ns = 0;

    HDC screenDc = nullptr;
    HDC srcDc = nullptr;
    HBITMAP srcBmp = nullptr;

    HDC dstDc = nullptr;
    HBITMAP dstBmp = nullptr;

    BITMAPINFO bmi{};

    // Audio
    IAudioClient *systemAudioClient = nullptr;
    IAudioCaptureClient *systemCaptureClient = nullptr;
    HANDLE systemAudioEvent = nullptr;
    WAVEFORMATEX systemAudioFmt{};
    IAudioClient *micAudioClient = nullptr;
    IAudioCaptureClient *micCaptureClient = nullptr;
    HANDLE micAudioEvent = nullptr;
    WAVEFORMATEX micAudioFmt{};

    // Single audio thread that handles both system audio and microphone
    std::thread audioThread;
    std::atomic<bool> audioRunning{false};

    LONGLONG recordingStartTime100ns = 0;
};

/**
 * @brief 安全释放 COM 对象
 * @param p COM 对象指针
 * @author chiangyang
 */
static inline void safeRelease(IUnknown *p) {
    if (p) p->Release();
}

/**
 * @brief 判断 HRESULT 是否成功
 * @param hr HRESULT 值
 * @return 是否成功
 * @author chiangyang
 */
static inline bool hrOk(HRESULT hr) {
    return SUCCEEDED(hr);
}

/**
 * @brief 将 HRESULT 转换为可读的错误信息字符串
 * @param hr HRESULT 值
 * @return 错误信息字符串
 * @author chiangyang
 */
static QString hresultToString( HRESULT hr) {
    wchar_t *buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    DWORD len = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr), lang, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString msg;
    if (len > 0 && buf) {
        msg = QString::fromWCharArray(buf).trimmed();
        LocalFree(buf);
    }
    if (msg.isEmpty()) {
        msg = QStringLiteral("HRESULT=0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
    } else {
        msg = QStringLiteral("0x%1 %2").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')).arg(msg);
    }
    return msg;
}

/**
 * @brief 获取当前时间（100 纳秒单位）
 * @return 当前时间戳
 * @author chiangyang
 */
static LONGLONG currentTime100ns() {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return static_cast<LONGLONG>(cnt.QuadPart * 10000000LL / freq.QuadPart);
}

/**
 * @brief 初始化 WASAPI 音频捕获客户端
 * @param outClient 输出音频客户端
 * @param outCapture 输出音频捕获客户端
 * @param outEvent 输出事件句柄
 * @param outFmt 输出音频格式
 * @param isLoopback true 为系统音频回环，false 为麦克风
 * @return 是否成功
 * @author chiangyang
 */
static bool initAudioCapture(IAudioClient **outClient, IAudioCaptureClient **outCapture,
                              HANDLE *outEvent, WAVEFORMATEX *outFmt, bool isLoopback) {
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioClient *client = nullptr;
    IAudioCaptureClient *capture = nullptr;
    WAVEFORMATEX *mixFmt = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (!hrOk(hr)) { LOG_ERROR("Failed to create device enumerator: " + hresultToString(hr)); return false; }

    if (isLoopback) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }
    safeRelease(enumerator);
    if (!hrOk(hr)) { LOG_ERROR("Failed to get default audio endpoint: " + hresultToString(hr)); return false; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client);
    safeRelease(device);
    if (!hrOk(hr)) { LOG_ERROR("Failed to activate audio client: " + hresultToString(hr)); return false; }

    hr = client->GetMixFormat(&mixFmt);
    if (!hrOk(hr)) { safeRelease(client); LOG_ERROR("Failed to get mix format"); return false; }

    LOG_INFO(QString("Audio mix format: %1Hz, %2ch, %3bit, tag=%4")
             .arg(mixFmt->nSamplesPerSec).arg(mixFmt->nChannels)
             .arg(mixFmt->wBitsPerSample).arg(mixFmt->wFormatTag));

    DWORD streamFlags = isLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    streamFlags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    REFERENCE_TIME bufferDuration = 10000000; // 1 second in 100-ns units
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, mixFmt, nullptr);
    if (!hrOk(hr)) {
        CoTaskMemFree(mixFmt);
        safeRelease(client);
        LOG_ERROR("Failed to initialize audio client: " + hresultToString(hr));
        return false;
    }

    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!event) { CoTaskMemFree(mixFmt); safeRelease(client); LOG_ERROR("Failed to create audio event"); return false; }

    hr = client->SetEventHandle(event);
    if (!hrOk(hr)) { CloseHandle(event); CoTaskMemFree(mixFmt); safeRelease(client); LOG_ERROR("Failed to set audio event handle"); return false; }

    hr = client->GetService(__uuidof(IAudioCaptureClient), (void **)&capture);
    if (!hrOk(hr)) { CloseHandle(event); CoTaskMemFree(mixFmt); safeRelease(client); LOG_ERROR("Failed to get capture client"); return false; }

    *outClient = client;
    *outCapture = capture;
    *outEvent = event;
    *outFmt = *mixFmt;
    CoTaskMemFree(mixFmt);
    return true;
}

/**
 * @brief 将浮点音频采样转换为 16 位 PCM
 * @param src 源浮点采样数据
 * @param dst 目标 PCM 数据
 * @param sampleCount 采样数
 * @author chiangyang
 */
static void convertFloatToPcm16(const float *src, int16_t *dst, UINT32 sampleCount) {
    for (UINT32 i = 0; i < sampleCount; i++) {
        float s = src[i];
        if (s < -1.0f) s = -1.0f;
        if (s > 1.0f) s = 1.0f;
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }
}

/**
 * @brief 检测 WASAPI 音频格式是否为 IEEE 浮点
 * @param fmt 音频格式
 * @return 是否为浮点格式
 * @author chiangyang
 */
// Detect if WASAPI audio format is IEEE float.
// Note: the WAVEFORMATEX may be truncated from WAVEFORMATEXTENSIBLE
// (only sizeof(WAVEFORMATEX) bytes copied), so avoid reading extension fields.
static bool isFloatFormat(const WAVEFORMATEX &fmt) {
    if (fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    // WASAPI loopback/capture mix format is always 32-bit float or 16-bit PCM.
    // For extensible format: wBitsPerSample==32 implies float, wBitsPerSample==16 implies PCM.
    // The SubFormat field can't be safely read due to potential truncation.
    return (fmt.wBitsPerSample == 32 && fmt.wFormatTag != WAVE_FORMAT_PCM);
}

/**
 * @brief 从 WASAPI 捕获客户端读取所有可用音频包到 PCM 缓冲区
 * @param capture 音频捕获客户端
 * @param fmt 音频格式
 * @param isFloat 是否为浮点格式
 * @param pcmBuf PCM 缓冲区
 * @return 添加的帧数
 * @author chiangyang
 */
static UINT32 pumpAudioPackets(IAudioCaptureClient *capture, const WAVEFORMATEX &fmt,
                                bool isFloat, std::vector<int16_t> &pcmBuf) {
    UINT32 totalFrames = 0;
    UINT32 packetCount = 0;
    HRESULT hr = capture->GetNextPacketSize(&packetCount);
    if (!hrOk(hr)) return 0;

    std::vector<int16_t> convertBuf;
    while (packetCount > 0) {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (!hrOk(hr)) break;

        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && frames > 0) {
            const UINT32 samples = frames * fmt.nChannels;
            const int16_t *src;
            if (isFloat) {
                convertBuf.resize(samples);
                convertFloatToPcm16(reinterpret_cast<const float *>(data), convertBuf.data(), samples);
                src = convertBuf.data();
            } else {
                src = reinterpret_cast<const int16_t *>(data);
            }
            pcmBuf.insert(pcmBuf.end(), src, src + samples);
            totalFrames += frames;
        }
        capture->ReleaseBuffer(frames);
        hr = capture->GetNextPacketSize(&packetCount);
        if (!hrOk(hr)) break;
    }
    return totalFrames;
}

/**
 * @brief 将混合 PCM 数据写入 Sink Writer
 * @param writer Media Foundation Sink Writer
 * @param streamIndex 音频流索引
 * @param data PCM 数据
 * @param frames 帧数
 * @param channels 声道数
 * @param sampleRate 采样率
 * @param timestamp 时间戳（会被更新）
 * @return 是否成功
 * @author chiangyang
 */
static bool writeAudioToSinkWriter(IMFSinkWriter *writer, DWORD streamIndex,
                                    const int16_t *data, UINT32 frames, UINT32 channels,
                                    UINT32 sampleRate, LONGLONG &timestamp) {
    const UINT32 dataSize = frames * channels * 2;
    const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(frames) * 10000000LL / sampleRate;

    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(dataSize, &buffer);
    if (!hrOk(hr)) return false;

    BYTE *dst = nullptr;
    hr = buffer->Lock(&dst, nullptr, nullptr);
    if (hrOk(hr)) {
        memcpy(dst, data, dataSize);
        buffer->Unlock();
        buffer->SetCurrentLength(dataSize);

        IMFSample *sample = nullptr;
        hr = MFCreateSample(&sample);
        if (hrOk(hr)) {
            sample->AddBuffer(buffer);
            sample->SetSampleTime(timestamp);
            sample->SetSampleDuration(duration);
            hr = writer->WriteSample(streamIndex, sample);
            safeRelease(sample);
            if (hrOk(hr)) {
                timestamp += duration;
                safeRelease(buffer);
                return true;
            }
        }
    }
    safeRelease(buffer);
    return false;
}

/**
 * @brief 混合音频捕获线程，同时处理系统音频和麦克风
 * @param impl 录屏器实现
 * @param pImpl 平台实现
 * @param hasSysAudio 是否有系统音频
 * @param hasMicAudio 是否有麦克风音频
 * @author chiangyang
 */
// Single audio capture thread that handles both system audio and microphone.
// Mixes PCM samples from both sources into a single AAC audio stream.
// Silent buffers are filled with zero samples to keep both sources time-aligned.
static void audioCaptureMixedThread(ScreenRecorder::Impl *impl, PlatformImpl *pImpl,
                                     bool hasSysAudio, bool hasMicAudio) {
    LOG_INFO(QString("Audio capture thread started (sys=%1, mic=%2)")
             .arg(hasSysAudio).arg(hasMicAudio));

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Start audio clients — mic first to avoid loopback blocking capture endpoint
    if (hasMicAudio) {
        HRESULT hr = pImpl->micAudioClient->Start();
        if (!hrOk(hr)) {
            LOG_ERROR("Microphone client start failed: " + hresultToString(hr));
            hasMicAudio = false;
        } else {
            // Give the capture endpoint a moment to begin streaming
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    if (hasSysAudio) {
        HRESULT hr = pImpl->systemAudioClient->Start();
        if (!hrOk(hr)) {
            LOG_ERROR("System audio client start failed: " + hresultToString(hr));
            hasSysAudio = false;
        }
    }

    if (!hasSysAudio && !hasMicAudio) {
        LOG_WARNING("No audio sources available, audio thread exiting");
        pImpl->audioRunning = false;
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return;
    }

    // Use system audio format as reference for output; fall back to mic format
    const WAVEFORMATEX &refFmt = hasSysAudio ? pImpl->systemAudioFmt : pImpl->micAudioFmt;
    const UINT32 outChannels = refFmt.nChannels;
    const UINT32 sampleRate = refFmt.nSamplesPerSec;

    // Log sample rate info for diagnostics
    if (hasSysAudio && hasMicAudio) {
        LOG_INFO(QString("Audio sources: sys=%1Hz/%2ch mic=%3Hz/%4ch output=%5Hz/%6ch")
                 .arg(pImpl->systemAudioFmt.nSamplesPerSec).arg(pImpl->systemAudioFmt.nChannels)
                 .arg(pImpl->micAudioFmt.nSamplesPerSec).arg(pImpl->micAudioFmt.nChannels)
                 .arg(sampleRate).arg(outChannels));
    }

    // Per-source state (use pointers, not references, for array storage)
    struct SrcState {
        IAudioCaptureClient *capture;
        HANDLE event;
        const WAVEFORMATEX *fmt;
        bool isFloat;
        std::vector<int16_t> *buf;
        UINT32 channels;
    };

    std::vector<int16_t> sysBuf, micBuf;

    // Build source list and event array
    SrcState srcs[2];
    HANDLE events[2];
    int srcCount = 0;

    if (hasSysAudio) {
        srcs[srcCount] = { pImpl->systemCaptureClient, pImpl->systemAudioEvent,
                           &pImpl->systemAudioFmt, isFloatFormat(pImpl->systemAudioFmt),
                           &sysBuf, pImpl->systemAudioFmt.nChannels };
        events[srcCount] = pImpl->systemAudioEvent;
        srcCount++;
    }
    if (hasMicAudio) {
        srcs[srcCount] = { pImpl->micCaptureClient, pImpl->micAudioEvent,
                           &pImpl->micAudioFmt, isFloatFormat(pImpl->micAudioFmt),
                           &micBuf, pImpl->micAudioFmt.nChannels };
        events[srcCount] = pImpl->micAudioEvent;
        srcCount++;
    }

    LOG_INFO(QString("Audio mixer: %1Hz %2ch, sources=%3")
             .arg(sampleRate).arg(outChannels).arg(srcCount));

    LONGLONG audioTimestamp = 0;
    const bool doMix = (srcCount == 2);
    std::vector<int16_t> convertBuf;
    int frameCount = 0;

    // Max buffer size to prevent unbounded growth (2 seconds worth)
    const size_t maxBufSamples = sampleRate * outChannels * 2;

    while (pImpl->audioRunning.load() && impl->running.load()) {
        if (impl->paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        DWORD waitResult = WaitForMultipleObjects(srcCount, events, FALSE, 200);
        if (waitResult < WAIT_OBJECT_0 || waitResult >= WAIT_OBJECT_0 + static_cast<DWORD>(srcCount))
            continue;

        // Read all available packets from ALL sources.
        // Silent packets are filled with zero samples to maintain time alignment.
        for (int i = 0; i < srcCount; i++) {
            SrcState &s = srcs[i];
            UINT32 packetCount = 0;
            HRESULT hr = s.capture->GetNextPacketSize(&packetCount);
            if (!hrOk(hr)) continue;

            while (packetCount > 0 && pImpl->audioRunning.load() && impl->running.load()) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = s.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (!hrOk(hr)) break;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // Fill with silence instead of skipping — keeps buffer time-aligned
                    s.buf->insert(s.buf->end(), frames * s.channels, int16_t(0));
                } else if (data && frames > 0) {
                    const UINT32 samples = frames * s.channels;
                    if (s.isFloat) {
                        convertBuf.resize(samples);
                        convertFloatToPcm16(reinterpret_cast<const float *>(data),
                                           convertBuf.data(), samples);
                        s.buf->insert(s.buf->end(), convertBuf.begin(), convertBuf.end());
                    } else {
                        const int16_t *src = reinterpret_cast<const int16_t *>(data);
                        s.buf->insert(s.buf->end(), src, src + samples);
                    }
                }
                s.capture->ReleaseBuffer(frames);
                hr = s.capture->GetNextPacketSize(&packetCount);
                if (!hrOk(hr)) break;
            }
        }

        // Mix and write
        if (doMix) {
            // Both sources have data (or silence) — mix when both have at least some data
            while (!sysBuf.empty() && !micBuf.empty()) {
                const size_t f1 = sysBuf.size() / srcs[0].channels;
                const size_t f2 = micBuf.size() / srcs[1].channels;
                const size_t mixFrames = f1 < f2 ? f1 : f2;

                std::vector<int16_t> mixed(mixFrames * outChannels);
                for (size_t f = 0; f < mixFrames; f++) {
                    for (UINT32 c = 0; c < outChannels; c++) {
                        int32_t s1 = sysBuf[f * srcs[0].channels + (c % srcs[0].channels)];
                        int32_t s2 = micBuf[f * srcs[1].channels + (c % srcs[1].channels)];
                        mixed[f * outChannels + c] = static_cast<int16_t>((s1 + s2) / 2);
                    }
                }

                if (!writeAudioToSinkWriter(pImpl->writer, pImpl->audioStreamIndex,
                                            mixed.data(), static_cast<UINT32>(mixFrames),
                                            outChannels, sampleRate, audioTimestamp)) {
                    LOG_ERROR("Failed to write mixed audio sample");
                }

                sysBuf.erase(sysBuf.begin(), sysBuf.begin() + mixFrames * srcs[0].channels);
                micBuf.erase(micBuf.begin(), micBuf.begin() + mixFrames * srcs[1].channels);
            }

            // Limit buffer growth: if one source is ahead, drop oldest data to stay under max
            for (int i = 0; i < 2; i++) {
                std::vector<int16_t> *b = srcs[i].buf;
                if (b->size() > maxBufSamples) {
                    const size_t dropped = b->size() - maxBufSamples;
                    LOG_WARNING(QString("Audio buffer %1 overflow, dropping %2 samples")
                                .arg(i).arg(dropped));
                    b->erase(b->begin(), b->begin() + dropped);
                }
            }
        } else {
            // Single source — write directly
            SrcState &s = srcs[0];
            if (!s.buf->empty()) {
                const size_t frames = s.buf->size() / s.channels;
                if (!writeAudioToSinkWriter(pImpl->writer, pImpl->audioStreamIndex,
                                            s.buf->data(), static_cast<UINT32>(frames),
                                            outChannels, sampleRate, audioTimestamp)) {
                    LOG_ERROR("Failed to write audio sample");
                }
                s.buf->clear();
            }
        }

        if (++frameCount % 100 == 0) {
            LOG_INFO(QString("Audio: %1 iterations, ts=%2s, sysBuf=%3 micBuf=%4")
                     .arg(frameCount)
                     .arg(static_cast<double>(audioTimestamp) / 10000000.0, 0, 'f', 1)
                     .arg(sysBuf.size()).arg(micBuf.size()));
        }
    }

    // Stop audio clients
    if (hasSysAudio) pImpl->systemAudioClient->Stop();
    if (hasMicAudio) pImpl->micAudioClient->Stop();

    LOG_INFO("Audio capture thread stopped");
    pImpl->audioRunning = false;
    if (SUCCEEDED(hrCo)) CoUninitialize();
}

/**
 * @brief 创建 Media Foundation Sink Writer（视频+音频）
 * @param outWriter 输出 Sink Writer
 * @param outVideoStreamIndex 输出视频流索引
 * @param outAudioStreamIndex 输出音频流索引
 * @param path 输出文件路径
 * @param outSize 输出分辨率
 * @param fps 帧率
 * @param outSubType 视频编码格式
 * @param bitrate 视频比特率
 * @param addAudio 是否添加音频流
 * @param audioFmt 音频格式
 * @param outHr 输出 HRESULT
 * @return 是否成功
 * @author chiangyang
 */
static bool createSinkWriter(IMFSinkWriter **outWriter, DWORD *outVideoStreamIndex,
                              DWORD *outAudioStreamIndex,
                              const wchar_t *path, const QSize &outSize, int fps,
                              const GUID &outSubType, UINT32 bitrate,
                              bool addAudio, const WAVEFORMATEX *audioFmt,
                              HRESULT *outHr) {
    if (!outWriter || !outVideoStreamIndex) return false;
    *outWriter = nullptr;
    *outVideoStreamIndex = 0;
    if (outAudioStreamIndex) *outAudioStreamIndex = 0;
    if (outHr) *outHr = S_OK;

    IMFAttributes *attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 2);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; return false; }
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

    IMFSinkWriter *writer = nullptr;
    hr = MFCreateSinkWriterFromURL(path, nullptr, attrs, &writer);
    safeRelease(attrs);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; return false; }

    // Video output type
    IMFMediaType *outType = nullptr;
    hr = MFCreateMediaType(&outType);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(writer); return false; }

    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, outSubType);
    outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, static_cast<UINT32>(outSize.width()), static_cast<UINT32>(outSize.height()));
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(outType); safeRelease(writer); return false; }
    hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(outType); safeRelease(writer); return false; }
    hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(outType); safeRelease(writer); return false; }

    DWORD videoStreamIndex = 0;
    hr = writer->AddStream(outType, &videoStreamIndex);
    safeRelease(outType);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(writer); return false; }

    // Video input type
    IMFMediaType *inType = nullptr;
    hr = MFCreateMediaType(&inType);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(writer); return false; }

    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, static_cast<UINT32>(outSize.width()), static_cast<UINT32>(outSize.height()));
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(inType); safeRelease(writer); return false; }
    hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(inType); safeRelease(writer); return false; }
    hr = MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(inType); safeRelease(writer); return false; }
    const LONG stride = static_cast<LONG>(outSize.width()) * 4;
    inType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(stride));
    inType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    inType->SetUINT32(MF_MT_SAMPLE_SIZE, static_cast<UINT32>(stride) * static_cast<UINT32>(outSize.height()));

    hr = writer->SetInputMediaType(videoStreamIndex, inType, nullptr);
    safeRelease(inType);
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(writer); return false; }

    // Audio stream (mixed system + microphone, AAC encoded)
    DWORD audioIdx = 0;
    if (addAudio && audioFmt) {
        IMFMediaType *audioOutType = nullptr;
        hr = MFCreateMediaType(&audioOutType);
        if (hrOk(hr)) {
            audioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            audioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            audioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioFmt->nChannels);
            audioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioFmt->nSamplesPerSec);
            audioOutType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            hr = writer->AddStream(audioOutType, &audioIdx);
            safeRelease(audioOutType);
            if (hrOk(hr)) {
                IMFMediaType *audioInType = nullptr;
                hr = MFCreateMediaType(&audioInType);
                if (hrOk(hr)) {
                    audioInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                    audioInType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                    audioInType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioFmt->nChannels);
                    audioInType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioFmt->nSamplesPerSec);
                    audioInType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                    audioInType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audioFmt->nChannels * 2);
                    audioInType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audioFmt->nSamplesPerSec * audioFmt->nChannels * 2);
                    hr = writer->SetInputMediaType(audioIdx, audioInType, nullptr);
                    safeRelease(audioInType);
                    if (!hrOk(hr)) {
                        LOG_WARNING("Failed to set audio input type: " + hresultToString(hr));
                        audioIdx = 0;
                    } else {
                        LOG_INFO("Audio stream added to sink writer");
                    }
                }
            }
        }
    }

    hr = writer->BeginWriting();
    if (!hrOk(hr)) { if (outHr) *outHr = hr; safeRelease(writer); return false; }

    *outWriter = writer;
    *outVideoStreamIndex = videoStreamIndex;
    if (outAudioStreamIndex) *outAudioStreamIndex = audioIdx;
    return true;
}

/**
 * @brief 清理音频捕获资源
 * @param client 音频客户端
 * @param capture 音频捕获客户端
 * @param event 事件句柄
 * @param thread 音频线程
 * @param running 运行标志
 * @author chiangyang
 */
static void cleanupAudioCapture(IAudioClient **client, IAudioCaptureClient **capture,
                                 HANDLE *event, std::thread *thread, std::atomic<bool> *running) {
    if (running) running->store(false);
    if (thread && thread->joinable()) thread->join();
    if (capture && *capture) { safeRelease(*capture); *capture = nullptr; }
    if (client && *client) { safeRelease(*client); *client = nullptr; }
    if (event && *event) { CloseHandle(*event); *event = nullptr; }
}

/**
 * @brief 获取平台实现单例
 * @return 平台实现指针
 * @author chiangyang
 */
static PlatformImpl* getPlatformImpl() {
    static PlatformImpl platformImpl;
    return &platformImpl;
}

/**
 * @brief 启动 Windows 平台录屏（Media Foundation + WASAPI）
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStart(ScreenRecorder::Impl *impl) {
    PlatformImpl *pImpl = getPlatformImpl();

    impl->worker = std::thread([impl, pImpl]() {
        LOG_INFO("Initialize COM and Media Foundation");
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hrMf = MFStartup(MF_VERSION);
        if (!hrOk(hrMf)) {
            LOG_ERROR("Screen recording initialization failed: " + hresultToString(hrMf));
            emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.initFailed", "Screen recording initialization failed"));
            impl->running = false;
            if (SUCCEEDED(hrCo)) CoUninitialize();
            return;
        }

        const QRect cap = impl->captureRectPx;
        const QSize out = impl->outputSizePx;
        const int fps = impl->fps;
        pImpl->frameDuration100ns = 10000000LL / fps;

        const bool wantSysAudio = impl->systemAudioEnabled;
        const bool wantMicAudio = impl->microphoneEnabled;

        // Initialize audio capture before creating sink writer (need format info)
        bool hasSysAudio = false;
        bool hasMicAudio = false;

        if (wantSysAudio) {
            hasSysAudio = initAudioCapture(&pImpl->systemAudioClient, &pImpl->systemCaptureClient,
                                            &pImpl->systemAudioEvent, &pImpl->systemAudioFmt, true);
            if (hasSysAudio) {
                LOG_INFO(QString("System audio: %1Hz, %2ch, %3bit")
                         .arg(pImpl->systemAudioFmt.nSamplesPerSec)
                         .arg(pImpl->systemAudioFmt.nChannels)
                         .arg(pImpl->systemAudioFmt.wBitsPerSample));
            } else {
                LOG_WARNING("System audio capture initialization failed, continuing without system audio");
            }
        }

        if (wantMicAudio) {
            hasMicAudio = initAudioCapture(&pImpl->micAudioClient, &pImpl->micCaptureClient,
                                            &pImpl->micAudioEvent, &pImpl->micAudioFmt, false);
            if (hasMicAudio) {
                LOG_INFO(QString("Microphone: %1Hz, %2ch, %3bit")
                         .arg(pImpl->micAudioFmt.nSamplesPerSec)
                         .arg(pImpl->micAudioFmt.nChannels)
                         .arg(pImpl->micAudioFmt.wBitsPerSample));
            } else {
                LOG_WARNING("Microphone capture initialization failed, continuing without microphone");
            }
        }

        QString absPath = QDir::toNativeSeparators(QFileInfo(impl->outputFilePath).absoluteFilePath());
        std::wstring outPath = absPath.toStdWString();
        DeleteFileW(outPath.c_str());
        LOG_INFO("Delete existing output file if it exists");

        HRESULT hrWriter = S_OK;
        LOG_INFO("Create H.264 media sink");
        const bool hasAudio = (hasSysAudio || hasMicAudio);
        const WAVEFORMATEX *audioFmt = hasSysAudio ? &pImpl->systemAudioFmt : (hasMicAudio ? &pImpl->micAudioFmt : nullptr);
        bool ok = createSinkWriter(&pImpl->writer, &pImpl->videoStreamIndex,
                                    &pImpl->audioStreamIndex,
                                    outPath.c_str(), out, fps, MFVideoFormat_H264, 6000000,
                                    hasAudio, audioFmt, &hrWriter);
        if (!ok) {
            LOG_WARNING("H.264 media sink creation failed, trying WMV format: " + hresultToString(hrWriter));
            QString fallbackPathQt = absPath;
            if (fallbackPathQt.endsWith(".mp4", Qt::CaseInsensitive)) {
                fallbackPathQt.chop(4);
                fallbackPathQt += ".wmv";
            } else {
                fallbackPathQt += ".wmv";
            }

            std::wstring fallbackPath = fallbackPathQt.toStdWString();
            DeleteFileW(fallbackPath.c_str());

            HRESULT hrWriter2 = S_OK;
            LOG_INFO("Create WMV media sink");
            ok = createSinkWriter(&pImpl->writer, &pImpl->videoStreamIndex,
                                   &pImpl->audioStreamIndex,
                                   fallbackPath.c_str(), out, fps, MFVideoFormat_WMV3, 4000000,
                                   hasAudio, audioFmt, &hrWriter2);
            if (ok) {
                impl->outputFilePath = fallbackPathQt;
                LOG_INFO("WMV media sink creation successful, using WMV format");
            } else {
                const QString detail1 = hresultToString(hrWriter);
                const QString detail2 = hresultToString(hrWriter2);
                QString hint = QStringLiteral("Failed to create video file: \n- MP4/H.264: %1\n- WMV fallback: %2").arg(detail1, detail2);
                hint += QStringLiteral("\nPossible reasons: System lacks H.264/MP4 encoding components (e.g., Windows N/KN without Media Feature Pack), or no permission to save path.");
                LOG_ERROR("Media sink creation failed: " + hint);
                emit impl->recorder->errorOccurred(hint);
                cleanupAudioCapture(&pImpl->systemAudioClient, &pImpl->systemCaptureClient,
                                     &pImpl->systemAudioEvent, nullptr, nullptr);
                cleanupAudioCapture(&pImpl->micAudioClient, &pImpl->micCaptureClient,
                                     &pImpl->micAudioEvent, nullptr, nullptr);
                MFShutdown();
                impl->running = false;
                if (SUCCEEDED(hrCo)) CoUninitialize();
                return;
            }
        } else {
            LOG_INFO("H.264 media sink creation successful");
        }

        if (!pImpl->writer) {
            LOG_ERROR("Failed to create video file");
            emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.createFileFailed", "Failed to create video file"));
            cleanupAudioCapture(&pImpl->systemAudioClient, &pImpl->systemCaptureClient,
                                 &pImpl->systemAudioEvent, nullptr, nullptr);
            cleanupAudioCapture(&pImpl->micAudioClient, &pImpl->micCaptureClient,
                                 &pImpl->micAudioEvent, nullptr, nullptr);
            MFShutdown();
            impl->running = false;
            if (SUCCEEDED(hrCo)) CoUninitialize();
            return;
        }

        // Record start time for audio timestamp synchronization
        pImpl->recordingStartTime100ns = currentTime100ns();

        // Start mixed audio capture thread (handles both system and mic)
        if (hasAudio && pImpl->audioStreamIndex > 0) {
            pImpl->audioRunning = true;
            pImpl->audioThread = std::thread([impl, pImpl, hasSysAudio, hasMicAudio]() {
                audioCaptureMixedThread(impl, pImpl, hasSysAudio, hasMicAudio);
            });
        }

        LOG_INFO("Initialize screen DC and bitmaps");
        pImpl->screenDc = GetDC(nullptr);
        pImpl->srcDc = CreateCompatibleDC(pImpl->screenDc);
        pImpl->srcBmp = CreateCompatibleBitmap(pImpl->screenDc, cap.width(), cap.height());
        SelectObject(pImpl->srcDc, pImpl->srcBmp);

        pImpl->dstDc = CreateCompatibleDC(pImpl->screenDc);
        pImpl->dstBmp = CreateCompatibleBitmap(pImpl->screenDc, out.width(), out.height());
        SelectObject(pImpl->dstDc, pImpl->dstBmp);

        ZeroMemory(&pImpl->bmi, sizeof(pImpl->bmi));
        pImpl->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        pImpl->bmi.bmiHeader.biWidth = out.width();
        pImpl->bmi.bmiHeader.biHeight = -out.height();
        pImpl->bmi.bmiHeader.biPlanes = 1;
        pImpl->bmi.bmiHeader.biBitCount = 32;
        pImpl->bmi.bmiHeader.biCompression = BI_RGB;

        const int stride = out.width() * 4;
        impl->frameBuffer.resize(stride * out.height());
        LOG_INFO("Initialize frame buffer, size: " + QString::number(impl->frameBuffer.size()));

        LONGLONG ts = 0;
        auto nextTick = std::chrono::steady_clock::now();
        int frameCount = 0;
        auto startTime = std::chrono::steady_clock::now();

        LOG_INFO("Start recording loop");
        while (impl->running.load()) {
            if (impl->paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                nextTick = std::chrono::steady_clock::now();
                continue;
            }

            // 使用 BitBlt 捕获指定区域
            BitBlt(pImpl->srcDc, 0, 0, cap.width(), cap.height(), pImpl->screenDc, cap.x(), cap.y(), SRCCOPY | CAPTUREBLT);
            SetStretchBltMode(pImpl->dstDc, HALFTONE);
            StretchBlt(pImpl->dstDc, 0, 0, out.width(), out.height(), pImpl->srcDc, 0, 0, cap.width(), cap.height(), SRCCOPY);

            int lines = GetDIBits(pImpl->dstDc, pImpl->dstBmp, 0, out.height(), impl->frameBuffer.data(), &pImpl->bmi, DIB_RGB_COLORS);
            if (lines != out.height()) {
                LOG_ERROR("Screen capture failed");
                emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.captureFailed", "Screen capture failed"));
                break;
            }

            // 合成标注叠加到帧缓冲
            {
                QMutexLocker lock(&impl->annotationMutex);
                if (!impl->annotationOverlay.isNull()) {
                    QImage frameImage(reinterpret_cast<uchar*>(impl->frameBuffer.data()),
                                      out.width(), out.height(),
                                      QImage::Format_RGB32);
                    QPainter fp(&frameImage);
                    fp.setCompositionMode(QPainter::CompositionMode_SourceOver);
                    fp.drawImage(0, 0, impl->annotationOverlay);
                    fp.end();
                }
            }

            IMFMediaBuffer *buffer = nullptr;
            HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(impl->frameBuffer.size()), &buffer);
            if (!hrOk(hr)) {
                LOG_ERROR("Failed to create media buffer: " + hresultToString(hr));
                break;
            }

            BYTE *dst = nullptr;
            DWORD maxLen = 0;
            DWORD curLen = 0;
            hr = buffer->Lock(&dst, &maxLen, &curLen);
            if (!hrOk(hr)) {
                LOG_ERROR("Failed to lock buffer: " + hresultToString(hr));
                safeRelease(buffer);
                break;
            }

            memcpy(dst, impl->frameBuffer.constData(), impl->frameBuffer.size());
            buffer->Unlock();
            buffer->SetCurrentLength(static_cast<DWORD>(impl->frameBuffer.size()));

            IMFSample *sample = nullptr;
            hr = MFCreateSample(&sample);
            if (!hrOk(hr)) {
                LOG_ERROR("Failed to create media sample: " + hresultToString(hr));
                safeRelease(buffer);
                break;
            }

            sample->AddBuffer(buffer);
            safeRelease(buffer);

            sample->SetSampleTime(ts);
            sample->SetSampleDuration(pImpl->frameDuration100ns);

            hr = pImpl->writer->WriteSample(pImpl->videoStreamIndex, sample);
            safeRelease(sample);
            if (!hrOk(hr)) {
                LOG_ERROR("Failed to write video: " + hresultToString(hr));
                emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.writeFailed", "Failed to write video"));
                break;
            }

            ts += pImpl->frameDuration100ns;
            frameCount++;

            if (frameCount % 100 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
                LOG_INFO(QString("Recording progress: %1 frames, recorded %2 seconds")
                         .arg(frameCount).arg(elapsed));
            }

            nextTick += std::chrono::milliseconds(1000 / fps);
            std::this_thread::sleep_until(nextTick);
        }

        LOG_INFO("Recording loop ended, total frames: " + QString::number(frameCount));

        // Stop audio thread first
        pImpl->audioRunning = false;
        if (pImpl->audioThread.joinable()) pImpl->audioThread.join();

        if (pImpl->writer) {
            LOG_INFO("Finalizing writer");
            pImpl->writer->Finalize();
        }

        LOG_INFO("Clean up DC and bitmap resources");
        if (pImpl->dstBmp) DeleteObject(pImpl->dstBmp);
        if (pImpl->dstDc) DeleteDC(pImpl->dstDc);
        if (pImpl->srcBmp) DeleteObject(pImpl->srcBmp);
        if (pImpl->srcDc) DeleteDC(pImpl->srcDc);
        if (pImpl->screenDc) ReleaseDC(nullptr, pImpl->screenDc);

        cleanupAudioCapture(&pImpl->systemAudioClient, &pImpl->systemCaptureClient,
                             &pImpl->systemAudioEvent, nullptr, nullptr);
        cleanupAudioCapture(&pImpl->micAudioClient, &pImpl->micCaptureClient,
                             &pImpl->micAudioEvent, nullptr, nullptr);

        safeRelease(pImpl->writer);
        pImpl->writer = nullptr;

        LOG_INFO("Shutdown Media Foundation and COM");
        MFShutdown();
        if (SUCCEEDED(hrCo)) CoUninitialize();

        const QString outFile = impl->outputFilePath;
        impl->running = false;

        // 如果是取消录制，删除视频文件
        if (impl->canceled.load()) {
            LOG_INFO("Recording was canceled, deleting video file: " + outFile);
            QFile::remove(outFile);
            LOG_INFO("Video file deleted");
        } else {
            LOG_INFO("Recording completed, output file: " + outFile);
        }

        emit impl->recorder->stopped(outFile);
    });
}

/**
 * @brief 停止 Windows 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStop(ScreenRecorder::Impl *impl) {
    impl->running = false;
}

/**
 * @brief 暂停 Windows 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplPause(ScreenRecorder::Impl *impl) {
    impl->paused = true;
}

/**
 * @brief 恢复 Windows 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplResume(ScreenRecorder::Impl *impl) {
    impl->paused = false;
}

/**
 * @brief 检查 Windows 平台录屏是否可用
 * @return 是否可用
 * @author chiangyang
 */
bool screenRecorderImplIsAvailable() {
    return true;
}

/**
 * @brief 获取可用音频设备列表
 * @return 设备名称列表
 * @author chiangyang
 */
QList<QString> screenRecorderImplAvailableAudioDevices() {
    QList<QString> deviceNames;
    return deviceNames;
}

/**
 * @brief 设置音频设备
 * @param index 设备索引
 * @author chiangyang
 */
void screenRecorderImplSetAudioDevice(int index) {
    Q_UNUSED(index);
}

/**
 * @brief 设置系统音频录制开关
 * @param enabled 是否启用
 * @author chiangyang
 */
void ScreenRecorder::setAudioEnabled(bool enabled) {
    m_impl->systemAudioEnabled = enabled;
    LOG_INFO(QString("System audio recording %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 设置麦克风录制开关
 * @param enabled 是否启用
 * @author chiangyang
 */
void ScreenRecorder::setMicrophoneEnabled(bool enabled) {
    m_impl->microphoneEnabled = enabled;
    LOG_INFO(QString("Microphone recording %1").arg(enabled ? "enabled" : "disabled"));
}

#endif // Q_OS_WIN
