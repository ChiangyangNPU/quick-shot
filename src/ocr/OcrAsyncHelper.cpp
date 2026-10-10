#include "OcrAsyncHelper.h"
#include "OcrEngine.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QTimer>
#include <QtConcurrent>

namespace {

/**
 * @brief 将控件在 parent 的指定区域内居中（区域无效时居中于整个 parent）
 * @author chiangyang
 */
void centerIn(QWidget *w, QWidget *parent, const QRect &rect) {
    w->adjustSize();
    const QRect r = rect.isValid() ? rect : parent->rect();
    w->move(r.x() + (r.width() - w->width()) / 2,
            r.y() + (r.height() - w->height()) / 2);
}

} // namespace

/**
 * @brief 异步执行 OCR 识别（加载提示 + QtConcurrent 后台识别 + 统一结果处理）
 * @author chiangyang
 */
void OcrAsync::run(QWidget *parent,
                   const QImage &image,
                   const QRect &centerRect,
                   bool releaseAfter,
                   const std::function<void(const OcrEngine::OcrResult &)> &onResult) {
    TranslationManager *tm = TranslationManager::instance();

    // 显示"识别中"加载提示
    QLabel *loadingLabel = new QLabel(tm->get("ocr.recognizing"), parent);
    loadingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    loadingLabel->setAlignment(Qt::AlignCenter);
    centerIn(loadingLabel, parent, centerRect);
    loadingLabel->show();

    // 异步执行 OCR
    auto *watcher = new QFutureWatcher<OcrEngine::OcrResult>(parent);
    QObject::connect(watcher, &QFutureWatcher<OcrEngine::OcrResult>::finished, parent,
                     [parent, watcher, loadingLabel, centerRect, releaseAfter, onResult]() {
        loadingLabel->hide();
        loadingLabel->deleteLater();

        OcrEngine::OcrResult result = watcher->result();
        watcher->deleteLater();

        if (releaseAfter) {
            // 释放 OCR 模型资源，下次识别时重新初始化
            OcrEngine::instance()->release();
        }

        if (result.texts.isEmpty()) {
            // 无识别文本：提示 2 秒后自动消失
            QLabel *noText = new QLabel(TranslationManager::instance()->get("ocr.noText"), parent);
            noText->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
            noText->setAlignment(Qt::AlignCenter);
            centerIn(noText, parent, centerRect);
            noText->show();
            QTimer::singleShot(2000, noText, &QWidget::deleteLater);
            return;
        }

        if (onResult) {
            onResult(result);
        }
    });

    QFuture<OcrEngine::OcrResult> future = QtConcurrent::run([image]() {
        return OcrEngine::instance()->recognize(image);
    });
    watcher->setFuture(future);
}
