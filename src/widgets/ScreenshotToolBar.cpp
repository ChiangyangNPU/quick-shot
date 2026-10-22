#include "ScreenshotToolBar.h"
#include "StyleManager.h"
#include "Logger.h"
#include "TranslationManager.h"
#include "Annotation.h"
#include "../recording/ScreenRecorder.h"
using namespace ToolIds;
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
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

    // 窗口属性与两级布局（三个工具栏子类共用的外壳）
    initShell(Qt::Widget | Qt::FramelessWindowHint);

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

    // 通用操作按钮：创建/样式/翻译走基类注册表（spec 表），信号在此显式发射
    m_redoBtn = makeActionButton({&m_redoBtn, "redo", ":/icons/redo.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Redo requested");
        emit redoRequested();
    });
    m_redoBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_redoBtn);

    m_clearBtn = makeActionButton({&m_clearBtn, "clear", ":/icons/clear.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Clear requested");
        emit clearRequested();
    });
    m_clearBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_clearBtn);

    m_ocrBtn = makeActionButton({&m_ocrBtn, "ocr.button", ":/icons/ocr.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: OCR requested");
        emit ocrRequested();
    });
    m_primaryLayout->addWidget(m_ocrBtn);

    m_translateBtn = makeActionButton({&m_translateBtn, "translate.button", ":/icons/translate.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Translate requested");
        emit translateRequested();
    });
    m_primaryLayout->addWidget(m_translateBtn);

    // GIF 录制按钮（操作区）：切到录屏模式并预选 GIF 格式
    m_gifBtn = makeActionButton({&m_gifBtn, "record.gif", ":/icons/gif.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: GIF recording requested");
        emit gifRecordRequested();
    });
    m_gifBtn->setVisible(ScreenRecorder::isGifSupported());
    m_primaryLayout->addWidget(m_gifBtn);

    // 滚动截图按钮（长截图）：左键默认垂直，右键菜单可选垂直/水平
    m_scrollBtn = makeActionButton({&m_scrollBtn, "scroll.button", ":/icons/scroll.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Scroll capture requested (vertical)");
        emit scrollCaptureRequested(0);
    });
    m_scrollBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_scrollBtn, &QPushButton::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *verticalAction = menu.addAction(
            TranslationManager::instance()->get("scroll.vertical"));
        QAction *horizontalAction = menu.addAction(
            TranslationManager::instance()->get("scroll.horizontal"));
        QAction *chosen = menu.exec(m_scrollBtn->mapToGlobal(pos));
        if (chosen == verticalAction) {
            LOG_INFO("ScreenshotToolBar: Scroll capture requested (vertical, menu)");
            emit scrollCaptureRequested(0);
        } else if (chosen == horizontalAction) {
            LOG_INFO("ScreenshotToolBar: Scroll capture requested (horizontal, menu)");
            emit scrollCaptureRequested(1);
        }
    });
    m_primaryLayout->addWidget(m_scrollBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    m_copyBtn = makeActionButton({&m_copyBtn, "copy", ":/icons/copy.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Copy requested");
        emit copyRequested();
    });
    m_primaryLayout->addWidget(m_copyBtn);

    m_saveBtn = makeActionButton({&m_saveBtn, "save", ":/icons/save.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Save requested");
        emit saveRequested();
    });
    m_primaryLayout->addWidget(m_saveBtn);

    m_pinBtn = makeActionButton({&m_pinBtn, "pin", ":/icons/pin.svg"}, [this]() {
        LOG_INFO("ScreenshotToolBar: Pin requested");
        emit pinRequested();
    });
    m_primaryLayout->addWidget(m_pinBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    m_closeBtn = makeActionButton({&m_closeBtn, "cancel", ":/icons/close.svg", /*closeStyle=*/true}, [this]() {
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
    // 定位规则统一在基类（右对齐选区右边界）
    move(positionNearRect(selectionRect, /*alignRight=*/true));
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

    // 操作按钮样式统一遍历基类注册表
    updateActionButtons(isIcon);

    // 长截图按钮提示方向入口：applyButtonStyle 会覆盖 tooltip，需在其后设置
    if (m_scrollBtn) {
        m_scrollBtn->setToolTip(tm->get("scroll.tooltip"));
    }

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

    // 操作按钮文案统一遍历基类注册表
    retranslateActionButtons();

    updateButtonStyles();
    adjustSize();
}
