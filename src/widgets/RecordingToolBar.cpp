#include "RecordingToolBar.h"
#include "StyleManager.h"
#include "Logger.h"
#include "TranslationManager.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
RecordingToolBar::RecordingToolBar(QWidget *parent)
    : BaseToolBar(parent)
{
    setupUi();
}

/**
 * @brief 设置 UI 布局
 * @author chiangyang
 */
void RecordingToolBar::setupUi() {
    LOG_INFO("RecordingToolBar: Setting up UI");

    // 窗口属性与两级布局（三个工具栏子类共用的外壳）
    initShell(Qt::Widget | Qt::FramelessWindowHint);

    // 录屏按钮（最左边，可切换高亮）
    m_recordBtn = new QPushButton(TranslationManager::instance()->get("record"), this);
    m_recordBtn->setCheckable(true);
    StyleManager::applyToolButtonStyle(m_recordBtn);
    connect(m_recordBtn, &QPushButton::toggled, [this](bool checked) {
        if (checked) {
            m_recordBtn->setStyleSheet(StyleManager::getButtonCheckedStyle());
        } else {
            m_recordBtn->setStyleSheet(StyleManager::getToolButtonStyle());
        }
        LOG_INFO(QString("RecordingToolBar: Record button toggled: %1").arg(checked));
        emit showControlRequested(checked);
    });
    m_primaryLayout->addWidget(m_recordBtn);

    // 计时器
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, [this]() {
        m_elapsedSeconds++;
        int min = m_elapsedSeconds / 60;
        int sec = m_elapsedSeconds % 60;
        emit timerUpdated(QString("%1:%2")
            .arg(min, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0')));
    });

    // 录屏按钮与截图按钮之间的分隔符
    addSeparator(m_primaryLayout);

    // 截图按钮
    m_screenshotBtn = new QPushButton(TranslationManager::instance()->get("screenshot"), this);
    StyleManager::applyToolButtonStyle(m_screenshotBtn);
    connect(m_screenshotBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Screenshot requested");
        emit screenshotRequested();
    });
    m_primaryLayout->addWidget(m_screenshotBtn);

    addSeparator(m_primaryLayout);

    // 标注工具（基类统一创建）
    createAnnotationTools();

    addSeparator(m_primaryLayout);

    // 操作按钮
    createActionButtons();

    m_mainLayout->addLayout(m_primaryLayout);

    updateButtonStyles();
    adjustSize();
    LOG_INFO(QString("RecordingToolBar: UI setup completed, size: %1x%2")
        .arg(width()).arg(height()));
}

/**
 * @brief 创建操作按钮（撤销、重做、清除、OCR、快照、取消）
 * @author chiangyang
 */
void RecordingToolBar::createActionButtons() {
    LOG_INFO("RecordingToolBar: Creating action buttons");
    auto *tm = TranslationManager::instance();

    // 通用操作按钮：创建/样式/翻译走基类注册表（spec 表），信号在此显式发射
    m_undoBtn = makeActionButton({&m_undoBtn, "undo", ":/icons/undo.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: Undo requested");
        emit undoRequested();
    });
    m_undoBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_undoBtn);

    m_redoBtn = makeActionButton({&m_redoBtn, "redo", ":/icons/redo.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: Redo requested");
        emit redoRequested();
    });
    m_redoBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_redoBtn);

    m_clearBtn = makeActionButton({&m_clearBtn, "clear", ":/icons/clear.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: Clear requested");
        emit clearRequested();
    });
    m_clearBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_clearBtn);

    m_ocrBtn = makeActionButton({&m_ocrBtn, "ocr.button", ":/icons/ocr.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: OCR requested");
        emit ocrRequested();
    });
    m_primaryLayout->addWidget(m_ocrBtn);

    m_translateBtn = makeActionButton({&m_translateBtn, "translate.button", ":/icons/translate.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: Translate requested");
        emit translateRequested();
    });
    m_primaryLayout->addWidget(m_translateBtn);

    addSeparator(m_primaryLayout);

    m_snapshotBtn = makeActionButton({&m_snapshotBtn, "snapshot", ":/icons/capture.svg"}, [this]() {
        LOG_INFO("RecordingToolBar: Snapshot requested");
        emit snapshotRequested();
    });
    m_primaryLayout->addWidget(m_snapshotBtn);

    addSeparator(m_primaryLayout);

    m_cancelBtn = makeActionButton({&m_cancelBtn, "cancel", ":/icons/close.svg", /*closeStyle=*/true}, [this]() {
        LOG_INFO("RecordingToolBar: Cancel requested");
        emit cancelRecordRequested();
    });
    m_primaryLayout->addWidget(m_cancelBtn);

    LOG_INFO("RecordingToolBar: Action buttons created");
}

/**
 * @brief 将工具栏定位到选区附近
 * @param selectionRect 选区矩形（全局坐标）
 * @author chiangyang
 */
void RecordingToolBar::positionNearSelection(const QRect &selectionRect) {
    // 定位规则统一在基类（左对齐选区左边界）
    move(positionNearRect(selectionRect, /*alignRight=*/false));
}


/**
 * @brief 更新按钮样式（文字/图标模式切换）
 * @author chiangyang
 */
void RecordingToolBar::updateButtonStyles() {
    const bool isIcon = StyleManager::getToolbarButtonStyle() == "icon";
    auto *tm = TranslationManager::instance();

    applyButtonStyle(m_recordBtn,     ":/icons/record.svg",  tm->get("record"),     isIcon);
    applyButtonStyle(m_screenshotBtn, ":/icons/capture.svg", tm->get("screenshot"), isIcon);

    updateAnnotationButtonStyles(isIcon);

    // 操作按钮样式统一遍历基类注册表
    updateActionButtons(isIcon);

    layout()->activate();
    adjustSize();
}

/**
 * @brief 重新翻译用户界面
 * @author chiangyang
 */
void RecordingToolBar::retranslateUi() {
    auto *tm = TranslationManager::instance();
    m_recordBtn->setText(tm->get("record"));
    m_screenshotBtn->setText(tm->get("screenshot"));

    retranslateAnnotationButtons();

    // 操作按钮文案统一遍历基类注册表
    retranslateActionButtons();

    updateButtonStyles();
    adjustSize();
}

/**
 * @brief 重置录屏按钮为未选中状态
 * @author chiangyang
 */
void RecordingToolBar::resetRecordBtn() {
    if (m_recordBtn && m_recordBtn->isChecked()) {
        m_recordBtn->blockSignals(true);
        m_recordBtn->setChecked(false);
        m_recordBtn->blockSignals(false);
        m_recordBtn->setStyleSheet(StyleManager::getToolButtonStyle());
    }
}

/**
 * @brief 设置截图按钮的启用状态
 * @param enabled true 启用，false 禁用（置灰）
 * @author chiangyang
 */
void RecordingToolBar::setScreenshotButtonEnabled(bool enabled) {
    if (m_screenshotBtn) {
        m_screenshotBtn->setEnabled(enabled);
        LOG_INFO(QString("RecordingToolBar: Screenshot button %1").arg(enabled ? "enabled" : "disabled"));
    }
}

/**
 * @brief 启动录制计时器
 * @author chiangyang
 */
void RecordingToolBar::startTimer() {
    m_elapsedSeconds = 0;
    m_timer->start();
}

/**
 * @brief 暂停录制计时器
 * @author chiangyang
 */
void RecordingToolBar::pauseTimer() {
    m_timer->stop();
}

/**
 * @brief 恢复录制计时器
 * @author chiangyang
 */
void RecordingToolBar::resumeTimer() {
    m_timer->start();
}

/**
 * @brief 停止录制计时器并重置计时
 * @author chiangyang
 */
void RecordingToolBar::stopTimer() {
    m_timer->stop();
    m_elapsedSeconds = 0;
}
