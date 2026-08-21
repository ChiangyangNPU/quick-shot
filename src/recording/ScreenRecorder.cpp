#include "ScreenRecorder.h"

#include <QFileInfo>
#include "../log/Logger.h"
#include "../core/TranslationManager.h"

/**
 * @brief 将尺寸调整为偶数
 * 
 * 视频编码器通常要求宽度和高度为偶数。此函数将尺寸向下取整为最接近的偶数。
 * @param s 输入/输出尺寸，会被修改为偶数尺寸
 * @return 是否成功调整（尺寸有效且非零）
 * @author chiangyang
 */
static inline bool evenizeSize(QSize &s) {
    int w = s.width();
    int h = s.height();
    int ew = (w / 2) * 2;
    int eh = (h / 2) * 2;
    if (ew <= 0 || eh <= 0) return false;
    if (ew != w || eh != h) s = QSize(ew, eh);
    return true;
}

void screenRecorderImplStart(ScreenRecorder::Impl *impl);
void screenRecorderImplStop(ScreenRecorder::Impl *impl);
void screenRecorderImplPause(ScreenRecorder::Impl *impl);
void screenRecorderImplResume(ScreenRecorder::Impl *impl);
bool screenRecorderImplIsAvailable();
QList<QString> screenRecorderImplAvailableAudioDevices();
void screenRecorderImplSetAudioDevice(int index);

ScreenRecorder::ScreenRecorder(QObject *parent) : QObject(parent), m_impl(new Impl()) {
    m_impl->recorder = this;
}

ScreenRecorder::~ScreenRecorder() {
    stop();
    // 等待 worker 线程结束，确保资源正确释放
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
    delete m_impl;
}

bool ScreenRecorder::isRecording() const {
    return m_impl->running.load();
}

bool ScreenRecorder::isPaused() const {
    return m_impl->paused.load();
}

bool ScreenRecorder::start(const QRect &captureRectPx, const QSize &outputSizePx, const QString &outputFilePath, int fps) {
    if (isRecording()) {
        LOG_WARNING("Already recording, cannot start new recording");
        return false;
    }

    // 等待上一次录制的 worker 线程结束（异步 stop 后可能还在 Finalize）
    if (m_impl->worker.joinable()) {
        LOG_INFO("Waiting for previous recording thread to finish...");
        m_impl->worker.join();
    }

    if (captureRectPx.width() <= 0 || captureRectPx.height() <= 0) {
        LOG_ERROR("Invalid recording area");
        emit errorOccurred(TranslationManager::instance()->get("record.error.invalidArea", "Invalid recording area"));
        return false;
    }
    if (outputSizePx.width() <= 0 || outputSizePx.height() <= 0) {
        LOG_ERROR("Invalid output resolution");
        emit errorOccurred(TranslationManager::instance()->get("record.error.invalidResolution", "Invalid output resolution"));
        return false;
    }
    if (outputFilePath.isEmpty()) {
        LOG_ERROR("Invalid save path");
        emit errorOccurred(TranslationManager::instance()->get("record.error.invalidSavePath", "Invalid save path"));
        return false;
    }

    QSize outSize = outputSizePx;
    if (!evenizeSize(outSize)) {
        LOG_ERROR("Invalid output resolution");
        emit errorOccurred(TranslationManager::instance()->get("record.error.invalidResolution", "Invalid output resolution"));
        return false;
    }

    m_impl->captureRectPx = captureRectPx;
    m_impl->outputSizePx = outSize;
    m_impl->outputFilePath = outputFilePath;
    m_impl->fps = fps <= 0 ? 30 : fps;

    LOG_INFO(QString("Start recording: area=%1x%2+%3+%4, output=%5x%6, fps=%7, save path=%8")
             .arg(captureRectPx.width()).arg(captureRectPx.height())
             .arg(captureRectPx.x()).arg(captureRectPx.y())
             .arg(outSize.width()).arg(outSize.height())
             .arg(m_impl->fps).arg(outputFilePath));

    m_impl->running = true;
    m_impl->paused = false;

    screenRecorderImplStart(m_impl);

    return true;
}

void ScreenRecorder::pause() {
    if (!isRecording()) {
        LOG_WARNING("Not recording, cannot pause");
        return;
    }
    m_impl->paused = true;
    screenRecorderImplPause(m_impl);
    LOG_INFO("Recording paused");
}

/**
 * @brief 恢复录制
 * 
 * 从暂停状态恢复录制。
 * @author chiangyang
 */
void ScreenRecorder::resume() {
    if (!isRecording()) {
        LOG_WARNING("Not recording, cannot resume");
        return;
    }
    m_impl->paused = false;
    screenRecorderImplResume(m_impl);
    LOG_INFO("Recording resumed");
}

/**
 * @brief 停止录制
 * 
 * 停止当前录制并等待工作线程结束。
 * @author chiangyang
 */
void ScreenRecorder::stop() {
    if (!isRecording()) {
        LOG_WARNING("Not recording, cannot stop");
        return;
    }
    LOG_INFO("Stopping recording");
    m_impl->running = false;
    m_impl->paused = false;
    m_impl->canceled = false;
    screenRecorderImplStop(m_impl);
    // 不阻塞等待 worker 线程 —— worker 完成 Finalize 后会 emit stopped()
    LOG_INFO("Recording stop signaled");
}

/**
 * @brief 取消录制
 * 
 * 取消录制并删除已生成的视频文件。与 stop() 不同，cancel() 会在录制结束后删除视频文件。
 * @author chiangyang
 */
void ScreenRecorder::cancel() {
    if (!isRecording()) {
        LOG_WARNING("Not recording, cannot cancel");
        return;
    }
    LOG_INFO("Canceling recording, will delete video file");
    m_impl->running = false;
    m_impl->paused = false;
    m_impl->canceled = true;
    screenRecorderImplStop(m_impl);
    LOG_INFO("Recording cancel signaled");
}

/**
 * @brief 获取可用的音频设备列表
 * @return 音频设备名称列表
 * @author chiangyang
 */
QList<QString> ScreenRecorder::availableAudioDevices() const {
    return screenRecorderImplAvailableAudioDevices();
}

/**
 * @brief 设置音频设备
 * @param index 设备索引
 * @author chiangyang
 */
void ScreenRecorder::setAudioDevice(int index) {
    screenRecorderImplSetAudioDevice(index);
}

/**
 * @brief 检查录屏功能是否可用
 * @return 是否可用
 * @author chiangyang
 */
bool ScreenRecorder::isAvailable() const {
    return screenRecorderImplIsAvailable();
}

void ScreenRecorder::setAnnotationOverlay(const QImage &overlay) {
    QMutexLocker lock(&m_impl->annotationMutex);
    m_impl->annotationOverlay = overlay.isNull() ? QImage() : overlay.copy();
}

QSize ScreenRecorder::outputSize() const {
    return m_impl->outputSizePx;
}
