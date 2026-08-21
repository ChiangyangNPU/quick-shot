#include "ScreenshotToolBar.h"
#include "StyleManager.h"
#include "Logger.h"
#include "TranslationManager.h"
#include "Annotation.h"
using namespace ToolIds;
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
ScreenshotToolBar::ScreenshotToolBar(QWidget *parent)
    : BaseToolBar(parent)
{
    setupUi();
}

/**
 * @brief 设置 UI 布局
 * @author chiangyang
 */
void ScreenshotToolBar::setupUi() {
    LOG_INFO("ScreenshotToolBar: Setting up UI");

    // 设置窗口属性
    setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(StyleManager::getToolbarBackgroundStyle());
    setCursor(Qt::ArrowCursor);

    // 创建主布局
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 创建一级工具栏布局
    m_primaryLayout = new QHBoxLayout();
    m_primaryLayout->setContentsMargins(8, 4, 8, 4);
    m_primaryLayout->setSpacing(4);

    // 录屏按钮（最左边）
    m_recordBtn = new QPushButton(TranslationManager::instance()->get("record"), this);
    StyleManager::applyToolButtonStyle(m_recordBtn);
    connect(m_recordBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Record requested");
        emit recordRequested();
    });
    m_primaryLayout->addWidget(m_recordBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 标注工具按钮（基类统一创建）
    createAnnotationTools();

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 操作按钮
    createActionButtons();

    m_mainLayout->addLayout(m_primaryLayout);

    updateButtonStyles();
    // 调整大小
    adjustSize();
    LOG_INFO(QString("ScreenshotToolBar: UI setup completed, size: %1x%2")
        .arg(width()).arg(height()));
}

/**
 * @brief 创建操作按钮（撤销、重做、清除、OCR、复制、保存、贴图、关闭）
 * @author chiangyang
 */
void ScreenshotToolBar::createActionButtons() {
    LOG_INFO("ScreenshotToolBar: Creating action buttons");
    auto *tm = TranslationManager::instance();

    // 撤销按钮
    m_undoBtn = new QPushButton(tm->get("undo"), this);
    StyleManager::applyActionButtonStyle(m_undoBtn);
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Undo requested");
        emit undoRequested();
    });
    m_primaryLayout->addWidget(m_undoBtn);

    // 重做按钮
    m_redoBtn = new QPushButton(tm->get("redo"), this);
    StyleManager::applyActionButtonStyle(m_redoBtn);
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Redo requested");
        emit redoRequested();
    });
    m_primaryLayout->addWidget(m_redoBtn);

    // 清除按钮
    m_clearBtn = new QPushButton(tm->get("clear"), this);
    StyleManager::applyActionButtonStyle(m_clearBtn);
    m_clearBtn->setEnabled(false);
    connect(m_clearBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Clear requested");
        emit clearRequested();
    });
    m_primaryLayout->addWidget(m_clearBtn);

    // OCR 按钮
    m_ocrBtn = new QPushButton(tm->get("ocr.button"), this);
    StyleManager::applyActionButtonStyle(m_ocrBtn);
    connect(m_ocrBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: OCR requested");
        emit ocrRequested();
    });
    m_primaryLayout->addWidget(m_ocrBtn);

    // 翻译按钮
    m_translateBtn = new QPushButton(tm->get("translate.button"), this);
    StyleManager::applyActionButtonStyle(m_translateBtn);
    connect(m_translateBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Translate requested");
        emit translateRequested();
    });
    m_primaryLayout->addWidget(m_translateBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 复制按钮
    m_copyBtn = new QPushButton(tm->get("copy"), this);
    StyleManager::applyActionButtonStyle(m_copyBtn);
    connect(m_copyBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Copy requested");
        emit copyRequested();
    });
    m_primaryLayout->addWidget(m_copyBtn);

    // 保存按钮
    m_saveBtn = new QPushButton(tm->get("save"), this);
    StyleManager::applyActionButtonStyle(m_saveBtn);
    connect(m_saveBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Save requested");
        emit saveRequested();
    });
    m_primaryLayout->addWidget(m_saveBtn);

    // 贴图按钮
    m_pinBtn = new QPushButton(tm->get("pin"), this);
    StyleManager::applyActionButtonStyle(m_pinBtn);
    connect(m_pinBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Pin requested");
        emit pinRequested();
    });
    m_primaryLayout->addWidget(m_pinBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 关闭按钮
    m_closeBtn = new QPushButton(tm->get("cancel"), this);
    m_closeBtn->setObjectName("cancelButton");
    StyleManager::applyCloseButtonStyle(m_closeBtn);
    connect(m_closeBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("ScreenshotToolBar: Close requested");
        emit closeRequested();
    });
    m_primaryLayout->addWidget(m_closeBtn);

    LOG_INFO("ScreenshotToolBar: Action buttons created");
}

/**
 * @brief 将工具栏定位到选区附近
 * @param selectionRect 选区矩形（全局坐标）
 * @author chiangyang
 */
void ScreenshotToolBar::positionNearSelection(const QRect &selectionRect) {
    LOG_INFO(QString("ScreenshotToolBar: Positioning near selection: %1,%2 %3x%4")
        .arg(selectionRect.x()).arg(selectionRect.y())
        .arg(selectionRect.width()).arg(selectionRect.height()));

    // 使用虚拟桌面几何作为边界，避免跨屏选区时被单一屏幕限制
    QRect screenGeometry = QApplication::primaryScreen()->virtualGeometry();

    int toolbarWidth = width();
    int toolbarHeight = height();

    // 选区下方的可用空间
    int spaceBelow = screenGeometry.bottom() - selectionRect.bottom();
    // 选区上方的可用空间
    int spaceAbove = selectionRect.top() - screenGeometry.top();

    // 右边界对齐选区右边界
    int x = selectionRect.right() - toolbarWidth;
    int y;

    // 优先放在选区下方
    if (spaceBelow >= toolbarHeight + 10) {
        y = selectionRect.bottom() + 5;
        LOG_INFO("ScreenshotToolBar: Positioning below selection");
    } else if (spaceAbove >= toolbarHeight + 10) {
        y = selectionRect.top() - toolbarHeight - 5;
        LOG_INFO("ScreenshotToolBar: Positioning above selection");
    } else {
        y = selectionRect.bottom() - toolbarHeight - 5;
        LOG_INFO("ScreenshotToolBar: Positioning inside selection bottom");
    }

    // 确保工具栏不超出屏幕左边界
    if (x < screenGeometry.left()) {
        x = screenGeometry.left() + 5;
    }
    // 确保工具栏不超出屏幕右边界
    if (x + toolbarWidth > screenGeometry.right()) {
        x = screenGeometry.right() - toolbarWidth - 5;
    }

    // 转换为父控件（SnipScreen）相对坐标
    QPoint parentPos = parentWidget() ? parentWidget()->mapToGlobal(QPoint(0, 0)) : QPoint(0, 0);
    move(x - parentPos.x(), y - parentPos.y());
    LOG_INFO(QString("ScreenshotToolBar: Positioned at (%1, %2)").arg(x).arg(y));
}

/**
 * @brief 更新按钮样式（文字/图标模式切换）
 * @author chiangyang
 */
void ScreenshotToolBar::updateButtonStyles() {
    const bool isIcon = StyleManager::getToolbarButtonStyle() == "icon";
    auto *tm = TranslationManager::instance();

    applyButtonStyle(m_recordBtn, ":/icons/record.svg", tm->get("record"), isIcon);

    updateAnnotationButtonStyles(isIcon);

    applyButtonStyle(m_undoBtn,  ":/icons/undo.svg",  tm->get("undo"),  isIcon);
    applyButtonStyle(m_redoBtn,  ":/icons/redo.svg",  tm->get("redo"),  isIcon);
    applyButtonStyle(m_clearBtn, ":/icons/clear.svg", tm->get("clear"), isIcon);
    applyButtonStyle(m_ocrBtn,   ":/icons/ocr.svg", tm->get("ocr.button"), isIcon);
    applyButtonStyle(m_translateBtn, ":/icons/translate.svg", tm->get("translate.button"), isIcon);
    applyButtonStyle(m_copyBtn,  ":/icons/copy.svg",  tm->get("copy"),  isIcon);
    applyButtonStyle(m_saveBtn,  ":/icons/save.svg",  tm->get("save"),  isIcon);
    applyButtonStyle(m_pinBtn,   ":/icons/pin.svg",   tm->get("pin"),   isIcon);
    applyButtonStyle(m_closeBtn, ":/icons/close.svg", tm->get("cancel"), isIcon);

    layout()->activate();
    adjustSize();
}

/**
 * @brief 重新翻译用户界面
 * @author chiangyang
 */
void ScreenshotToolBar::retranslateUi() {
    auto *tm = TranslationManager::instance();
    m_recordBtn->setText(tm->get("record"));

    retranslateAnnotationButtons();

    m_undoBtn->setText(tm->get("undo"));
    m_redoBtn->setText(tm->get("redo"));
    m_clearBtn->setText(tm->get("clear"));
    m_ocrBtn->setText(tm->get("ocr.button"));
    m_translateBtn->setText(tm->get("translate.button"));
    m_copyBtn->setText(tm->get("copy"));
    m_saveBtn->setText(tm->get("save"));
    m_pinBtn->setText(tm->get("pin"));
    m_closeBtn->setText(tm->get("cancel"));
    updateButtonStyles();
    adjustSize();
}
