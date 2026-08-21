#include "ScreenRecorder.h"

#include <QDir>
#include <QFileInfo>
#include "../log/Logger.h"
#include "../core/TranslationManager.h"

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <chrono>
#include <thread>

/**
 * @brief 启动 Linux 平台录屏（X11 + XGetImage）
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStart(ScreenRecorder::Impl *impl) {
    impl->worker = std::thread([impl]() {
        LOG_INFO("Initialize Linux screen recording");
        const QRect cap = impl->captureRectPx;
        const QSize out = impl->outputSizePx;
        const int fps = impl->fps;

        QString absPath = QDir::toNativeSeparators(QFileInfo(impl->outputFilePath).absoluteFilePath());
        LOG_INFO("Output file path: " + absPath);

        Display *display = XOpenDisplay(nullptr);
        if (!display) {
            LOG_ERROR("Failed to open X display");
            emit impl->recorder->errorOccurred(TranslationManager::instance()->get("record.error.initFailed", "Screen recording initialization failed"));
            impl->running = false;
            return;
        }

        Window rootWindow = DefaultRootWindow(display);

        const int stride = out.width() * 4;
        impl->frameBuffer.resize(stride * out.height());
        LOG_INFO("Initialize frame buffer, size: " + QString::number(impl->frameBuffer.size()));

        LOG_INFO("Start recording loop");
        int frameCount = 0;
        auto startTime = std::chrono::steady_clock::now();
        auto nextTick = std::chrono::steady_clock::now();

        while (impl->running.load()) {
            if (impl->paused.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                nextTick = std::chrono::steady_clock::now();
                continue;
            }

            // 捕获屏幕指定区域
            XImage *image = XGetImage(display, rootWindow, cap.x(), cap.y(), cap.width(), cap.height(), AllPlanes, ZPixmap);
            if (image) {
                for (int y = 0; y < cap.height(); y++) {
                    for (int x = 0; x < cap.width(); x++) {
                        unsigned long pixel = XGetPixel(image, x, y);
                        int r = (pixel >> 16) & 0xff;
                        int g = (pixel >> 8) & 0xff;
                        int b = pixel & 0xff;
                        int a = 0xff;
                        int index = (y * cap.width() + x) * 4;
                        if (index + 3 < impl->frameBuffer.size()) {
                            impl->frameBuffer[index] = b;
                            impl->frameBuffer[index + 1] = g;
                            impl->frameBuffer[index + 2] = r;
                            impl->frameBuffer[index + 3] = a;
                        }
                    }
                }
                XDestroyImage(image);
            }

            frameCount++;

            if (frameCount % 100 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
                LOG_INFO(QString("Recording progress: %1 frames, recorded %2 seconds")
                         .arg(frameCount).arg(elapsed));
            }

            nextTick += std::chrono::milliseconds(1000 / fps);
            std::this_thread::sleep_until(nextTick);
        }

        if (display) {
            XCloseDisplay(display);
        }

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
 * @brief 停止 Linux 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplStop(ScreenRecorder::Impl *impl) {
    impl->running = false;
}

/**
 * @brief 暂停 Linux 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplPause(ScreenRecorder::Impl *impl) {
    impl->paused = true;
}

/**
 * @brief 恢复 Linux 平台录屏
 * @param impl 录屏器实现
 * @author chiangyang
 */
void screenRecorderImplResume(ScreenRecorder::Impl *impl) {
    impl->paused = false;
}

/**
 * @brief 检查 Linux 平台录屏是否可用
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

#endif // Q_OS_LINUX
