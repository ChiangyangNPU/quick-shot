#include "ScreenRecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QScreen>
#include <QGuiApplication>
#include "../log/Logger.h"
#include "../core/TranslationManager.h"

#ifdef Q_OS_MACOS

#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>

#include <chrono>
#include <thread>
#include <vector>

#include "platform/ScreenRecorder_mac_helper.h"

/**
 * @brief 获取 macOS 版本号
 * @return 主版本号（如 13、14）
 * @author chiangyang
 */
static int getMacOSVersion() {
    int majorVersion = 0;
    int minorVersion = 0;
    size_t size = sizeof(int);
    
    if (sysctlbyname("kern.osproductversion", &minorVersion, &size, NULL, 0) == 0) {
        return minorVersion;
    }
    
    return 0;
}

/**
 * @brief macOS 窗口信息结构体
 * @author chiangyang
 */
struct WindowInfo {
    int windowId;
    QString title;
    QString ownerName;
    QRect rect;
};

static std::vector<WindowInfo> g_availableWindows;

/**
 * @brief 启动 macOS 平台录屏（ScreenCaptureKit）
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStart(ScreenRecorder::Impl *impl) {
    // 在主线程获取 DPR，避免在 worker 线程访问 GUI 对象
    QScreen *screen = QGuiApplication::screenAt(impl->captureRectPx.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const float dpr = screen ? static_cast<float>(screen->devicePixelRatio()) : 1.0f;
    LOG_INFO(QString("Screen DPR: %1").arg(dpr));

    impl->worker = std::thread([impl, dpr]() {
        LOG_INFO("Starting ScreenCaptureKit recording");

        const QRect cap = impl->captureRectPx;
        const QSize out = impl->outputSizePx;
        const int fps = impl->fps;

        QString absPath = QDir::toNativeSeparators(QFileInfo(impl->outputFilePath).absoluteFilePath());
        LOG_INFO("Output file path: " + absPath);

        QDir dir = QFileInfo(absPath).dir();
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                LOG_ERROR("Failed to create output directory: " + dir.absolutePath());
                impl->running = false;
                emit impl->recorder->errorOccurred("Failed to create output directory");
                emit impl->recorder->stopped(impl->outputFilePath);
                return;
            }
        }

        bool success = screenRecorderStartAreaRecording(
            cap.x(),
            cap.y(),
            cap.width(),
            cap.height(),
            absPath.toUtf8().constData(),
            fps,
            dpr
        );

        if (!success) {
            LOG_ERROR("Failed to start ScreenCaptureKit recording");
            impl->running = false;
            emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.createFileFailed", "Failed to start recording"));
            emit impl->recorder->stopped(impl->outputFilePath);
            return;
        }

        LOG_INFO("ScreenCaptureKit recording started successfully");

        while (impl->running.load()) {
            if (impl->paused.load()) {
                screenRecorderPause();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            } else {
                screenRecorderResume();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_INFO("Stopping ScreenCaptureKit recording");
        screenRecorderStop();

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
 * @brief 停止 macOS 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStop(ScreenRecorder::Impl *impl) {
    impl->running = false;
}

/**
 * @brief 暂停 macOS 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplPause(ScreenRecorder::Impl *impl) {
    impl->paused = true;
    screenRecorderPause();
}

/**
 * @brief 恢复 macOS 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplResume(ScreenRecorder::Impl *impl) {
    impl->paused = false;
    screenRecorderResume();
}

/**
 * @brief 检查 macOS 平台录屏是否可用（需要 macOS 13+ 且有屏幕录制权限）
 * @return 是否可用
 * @author chiangyang
 */
bool screenRecorderImplIsAvailable() {
    int version = getMacOSVersion();
    if (version < 13) return false;
    return screenRecorderCheckPermission();
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
 * @brief 获取可录制的窗口列表
 * @return 窗口信息列表
 * @author chiangyang
 */
QList<ScreenRecorder::WindowInfo> ScreenRecorder::getAvailableWindows() {
    QList<ScreenRecorder::WindowInfo> windows;

    ScreenCaptureWindowInfo* windowInfos = nullptr;
    int count = screenRecorderGetAvailableWindows(&windowInfos);

    if (count > 0 && windowInfos) {
        for (int i = 0; i < count; i++) {
            ScreenRecorder::WindowInfo info;
            info.windowId = windowInfos[i].windowId;
            info.title = QString::fromUtf8(windowInfos[i].title);
            info.ownerName = QString::fromUtf8(windowInfos[i].ownerName);
            info.rect = QRect(
                static_cast<int>(windowInfos[i].x),
                static_cast<int>(windowInfos[i].y),
                static_cast<int>(windowInfos[i].width),
                static_cast<int>(windowInfos[i].height)
            );
            windows.append(info);
        }
        screenRecorderFreeWindowInfo(windowInfos, count);
    }

    return windows;
}

/**
 * @brief 启动窗口录制
 * @param windowId 窗口 ID
 * @param outputFilePath 输出文件路径
 * @param width 输出宽度
 * @param height 输出高度
 * @param fps 帧率
 * @return 是否成功
 * @author chiangyang
 */
bool ScreenRecorder::startWindowRecording(int windowId, const QString &outputFilePath, int width, int height, int fps) {
    if (isRecording()) {
        LOG_WARNING("Already recording, cannot start new recording");
        return false;
    }

    if (windowId <= 0) {
        LOG_ERROR("Invalid window ID");
        return false;
    }

    QString absPath = QDir::toNativeSeparators(QFileInfo(outputFilePath).absoluteFilePath());

    m_impl->captureRectPx = QRect(0, 0, width, height);
    m_impl->outputSizePx = QSize(width, height);
    m_impl->outputFilePath = absPath;
    m_impl->fps = fps;

    LOG_INFO(QString("Start window recording: windowId=%1, output=%2x%3, fps=%4, save path=%5")
             .arg(windowId)
             .arg(width).arg(height)
             .arg(fps).arg(absPath));

    m_impl->running = true;
    m_impl->paused = false;

    m_impl->worker = std::thread([this, windowId, absPath, width, height, fps]() {
        bool success = screenRecorderStartWindowRecording(
            windowId,
            absPath.toUtf8().constData(),
            width,
            height,
            fps
        );

        if (!success) {
            LOG_ERROR("Failed to start window recording");
            m_impl->running = false;
            emit errorOccurred(TranslationManager::instance()->get("record.error.createFileFailed", "Failed to start recording"));
            emit stopped(absPath);
            return;
        }

        LOG_INFO("Window recording started successfully");

        while (m_impl->running.load()) {
            if (m_impl->paused.load()) {
                screenRecorderPause();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            } else {
                screenRecorderResume();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_INFO("Stopping window recording");
        screenRecorderStop();

        m_impl->running = false;

        // 如果是取消录制，删除视频文件
        if (m_impl->canceled.load()) {
            LOG_INFO("Window recording was canceled, deleting video file: " + absPath);
            QFile::remove(absPath);
            LOG_INFO("Video file deleted");
        } else {
            LOG_INFO("Window recording completed, output file: " + absPath);
        }

        emit stopped(absPath);
    });

    return true;
}

/**
 * @brief 启动区域录制
 * @param captureRect 录制区域
 * @param outputFilePath 输出文件路径
 * @param fps 帧率
 * @return 是否成功
 * @author chiangyang
 */
bool ScreenRecorder::startAreaRecording(const QRect &captureRect, const QString &outputFilePath, int fps) {
    if (isRecording()) {
        LOG_WARNING("Already recording, cannot start new recording");
        return false;
    }

    if (captureRect.width() <= 0 || captureRect.height() <= 0) {
        LOG_ERROR("Invalid recording area");
        emit errorOccurred(TranslationManager::instance()->get("record.error.invalidArea", "Invalid recording area"));
        return false;
    }

    QString absPath = QDir::toNativeSeparators(QFileInfo(outputFilePath).absoluteFilePath());

    m_impl->captureRectPx = captureRect;
    m_impl->outputSizePx = captureRect.size();
    m_impl->outputFilePath = absPath;
    m_impl->fps = fps <= 0 ? 30 : fps;

    LOG_INFO(QString("Start area recording: area=%1x%2+%3+%4, fps=%5, save path=%6")
             .arg(captureRect.width()).arg(captureRect.height())
             .arg(captureRect.x()).arg(captureRect.y())
             .arg(m_impl->fps).arg(absPath));

    m_impl->running = true;
    m_impl->paused = false;

    screenRecorderImplStart(m_impl);

    return true;
}

/**
 * @brief 设置系统音频录制开关
 * @param enabled 是否启用
 * @author chiangyang
 */
void ScreenRecorder::setAudioEnabled(bool enabled) {
    screenRecorderSetAudioEnabled(enabled);
}

/**
 * @brief 设置麦克风录制开关
 * @param enabled 是否启用
 * @author chiangyang
 */
void ScreenRecorder::setMicrophoneEnabled(bool enabled) {
    screenRecorderSetMicrophoneEnabled(enabled);
}

#endif
