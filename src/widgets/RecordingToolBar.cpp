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

    setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(StyleManager::getToolbarBackgroundStyle());
    setCursor(Qt::ArrowCursor);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_primaryLayout = new QHBoxLayout();
    m_primaryLayout->setContentsMargins(8, 4, 8, 4);
    m_primaryLayout->setSpacing(4);

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

    // 撤销按钮
    m_undoBtn = new QPushButton(tm->get("undo"), this);
    StyleManager::applyActionButtonStyle(m_undoBtn);
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Undo requested");
        emit undoRequested();
    });
    m_primaryLayout->addWidget(m_undoBtn);

    // 重做按钮
    m_redoBtn = new QPushButton(tm->get("redo"), this);
    StyleManager::applyActionButtonStyle(m_redoBtn);
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Redo requested");
        emit redoRequested();
    });
    m_primaryLayout->addWidget(m_redoBtn);

    // 清除按钮
    m_clearBtn = new QPushButton(tm->get("clear"), this);
    StyleManager::applyActionButtonStyle(m_clearBtn);
    m_clearBtn->setEnabled(false);
    connect(m_clearBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Clear requested");
        emit clearRequested();
    });
    m_primaryLayout->addWidget(m_clearBtn);

    // OCR 按钮
    m_ocrBtn = new QPushButton(tm->get("ocr.button"), this);
    StyleManager::applyActionButtonStyle(m_ocrBtn);
    connect(m_ocrBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: OCR requested");
        emit ocrRequested();
    });
    m_primaryLayout->addWidget(m_ocrBtn);

    // 翻译按钮
    m_translateBtn = new QPushButton(tm->get("translate.button"), this);
    StyleManager::applyActionButtonStyle(m_translateBtn);
    connect(m_translateBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Translate requested");
        emit translateRequested();
    });
    m_primaryLayout->addWidget(m_translateBtn);

    addSeparator(m_primaryLayout);

    // 快照按钮
    m_snapshotBtn = new QPushButton(tm->get("snapshot"), this);
    StyleManager::applyActionButtonStyle(m_snapshotBtn);
    connect(m_snapshotBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("RecordingToolBar: Snapshot requested");
        emit snapshotRequested();
    });
    m_primaryLayout->addWidget(m_snapshotBtn);

    addSeparator(m_primaryLayout);

    // 取消按钮
    m_cancelBtn = new QPushButton(tm->get("cancel"), this);
    m_cancelBtn->setObjectName("cancelButton");
    StyleManager::applyCloseButtonStyle(m_cancelBtn);
    connect(m_cancelBtn, &QPushButton::clicked, [this]() {
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
    LOG_INFO(QString("RecordingToolBar: Positioning near selection: %1,%2 %3x%4")
        .arg(selectionRect.x()).arg(selectionRect.y())
        .arg(selectionRect.width()).arg(selectionRect.height()));

    // 使用虚拟桌面几何作为边界，避免跨屏选区时被单一屏幕限制
    QRect screenGeometry = QApplication::primaryScreen()->virtualGeometry();

    int toolbarWidth = width();
    int toolbarHeight = height();

    int spaceBelow = screenGeometry.bottom() - selectionRect.bottom();
    int spaceAbove = selectionRect.top() - screenGeometry.top();

    // 左对齐选区左边界
    int x = selectionRect.left();
    int y;

    if (spaceBelow >= toolbarHeight + 10) {
        y = selectionRect.bottom() + 5;
    } else if (spaceAbove >= toolbarHeight + 10) {
        y = selectionRect.top() - toolbarHeight - 5;
    } else {
        y = selectionRect.bottom() - toolbarHeight - 5;
    }

    // 转换为父控件（SnipScreen）相对坐标
    QPoint parentPos = parentWidget() ? parentWidget()->mapToGlobal(QPoint(0, 0)) : QPoint(0, 0);
    move(x - parentPos.x(), y - parentPos.y());
    LOG_INFO(QString("RecordingToolBar: Positioned at (%1, %2)").arg(x).arg(y));
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

    applyButtonStyle(m_undoBtn,    ":/icons/undo.svg",   tm->get("undo"),     isIcon);
    applyButtonStyle(m_redoBtn,    ":/icons/redo.svg",   tm->get("redo"),     isIcon);
    applyButtonStyle(m_clearBtn,   ":/icons/clear.svg",  tm->get("clear"),    isIcon);
    applyButtonStyle(m_ocrBtn,     ":/icons/ocr.svg",  tm->get("ocr.button"), isIcon);
    applyButtonStyle(m_translateBtn, ":/icons/translate.svg", tm->get("translate.button"), isIcon);
    applyButtonStyle(m_snapshotBtn,":/icons/capture.svg", tm->get("snapshot"), isIcon);
    applyButtonStyle(m_cancelBtn,  ":/icons/close.svg",  tm->get("cancel"),   isIcon);

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

    m_undoBtn->setText(tm->get("undo"));
    m_redoBtn->setText(tm->get("redo"));
    m_clearBtn->setText(tm->get("clear"));
    m_ocrBtn->setText(tm->get("ocr.button"));
    m_translateBtn->setText(tm->get("translate.button"));
    m_snapshotBtn->setText(tm->get("snapshot"));
    m_cancelBtn->setText(tm->get("cancel"));
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
