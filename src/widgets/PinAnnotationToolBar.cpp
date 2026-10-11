#include "PinAnnotationToolBar.h"
#include "StyleManager.h"
#include "Logger.h"
#include "TranslationManager.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>
#include <QMoveEvent>
#include <QPainter>
#include <QPaintEvent>

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
PinAnnotationToolBar::PinAnnotationToolBar(QWidget *parent)
    : BaseToolBar(parent)
{
    setupUi();
}

/**
 * @brief 设置 UI 布局
 * @author chiangyang
 */
void PinAnnotationToolBar::setupUi() {
    LOG_INFO("PinAnnotationToolBar: Setting up UI");

    // 独立顶层窗口：工具类型、无边框、置顶，与 PinWindow 同级显示。
    // 窗口属性与两级布局（三个工具栏子类共用的外壳）
    initShell(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // 标注工具按钮（基类统一创建：矩形/椭圆/箭头/画笔/直线/文本/马赛克/橡皮擦）
    createAnnotationTools();

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 操作按钮：撤销/重做/清除 + 复制/保存 + 取消
    createActionButtons();

    m_mainLayout->addLayout(m_primaryLayout);

    updateButtonStyles();
    adjustSize();
    LOG_INFO(QString("PinAnnotationToolBar: UI setup completed, size: %1x%2")
        .arg(width()).arg(height()));
}

/**
 * @brief 创建操作按钮（撤销、重做、清除、复制、保存、取消）
 * @author chiangyang
 */
void PinAnnotationToolBar::createActionButtons() {
    LOG_INFO("PinAnnotationToolBar: Creating action buttons");

    // 通用操作按钮：创建/样式/翻译走基类注册表（spec 表），信号在此显式发射
    m_undoBtn = makeActionButton({&m_undoBtn, "undo", ":/icons/undo.svg"}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Undo requested");
        emit undoRequested();
    });
    m_undoBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_undoBtn);

    m_redoBtn = makeActionButton({&m_redoBtn, "redo", ":/icons/redo.svg"}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Redo requested");
        emit redoRequested();
    });
    m_redoBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_redoBtn);

    m_clearBtn = makeActionButton({&m_clearBtn, "clear", ":/icons/clear.svg"}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Clear requested");
        emit clearRequested();
    });
    m_clearBtn->setEnabled(false);
    m_primaryLayout->addWidget(m_clearBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    m_copyBtn = makeActionButton({&m_copyBtn, "copy", ":/icons/copy.svg"}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Copy requested");
        emit copyRequested();
    });
    m_primaryLayout->addWidget(m_copyBtn);

    m_saveBtn = makeActionButton({&m_saveBtn, "save", ":/icons/save.svg"}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Save requested");
        emit saveRequested();
    });
    m_primaryLayout->addWidget(m_saveBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 取消按钮（退出标注模式，标注保留），closeStyle 应用专属红色样式
    m_cancelBtn = makeActionButton({&m_cancelBtn, "cancel", ":/icons/close.svg", /*closeStyle=*/true}, [this]() {
        LOG_INFO("PinAnnotationToolBar: Cancel requested");
        emit cancelRequested();
    });
    m_primaryLayout->addWidget(m_cancelBtn);

    LOG_INFO("PinAnnotationToolBar: Action buttons created");
}

/**
 * @brief 将工具栏定位到 PinWindow 下边界贴边位置
 * @param pinWindow 目标 PinWindow
 * @author chiangyang
 */
void PinAnnotationToolBar::positionNearPinWindow(QWidget *pinWindow) {
    if (!pinWindow) return;

    LOG_INFO(QString("PinAnnotationToolBar: Positioning near PinWindow: %1,%2 %3x%4")
        .arg(pinWindow->x()).arg(pinWindow->y())
        .arg(pinWindow->width()).arg(pinWindow->height()));

    // 使用虚拟桌面几何作为边界，兼容多屏
    QRect screenGeometry = QApplication::primaryScreen()->virtualGeometry();
    int toolbarWidth = width();
    int toolbarHeight = height();

    // 右对齐：工具栏右边贴 PinWindow 右边框，下边界贴 PinWindow 下边框
    int x = pinWindow->x() + pinWindow->width() - toolbarWidth;
    int y = pinWindow->y() + pinWindow->height();

    // 下方空间不足 → 放到 PinWindow 上方
    if (y + toolbarHeight > screenGeometry.bottom()) {
        y = pinWindow->y() - toolbarHeight;
    }
    // 工具栏左边溢出屏幕左边界 → 右移
    if (x < screenGeometry.left()) {
        x = screenGeometry.left();
    }
    // 工具栏右边溢出屏幕右边界 → 左移
    if (x + toolbarWidth > screenGeometry.right()) {
        x = screenGeometry.right() - toolbarWidth;
    }

    move(x, y);
    repositionSubToolbar();
    LOG_INFO(QString("PinAnnotationToolBar: Positioned at (%1, %2)").arg(x).arg(y));
}

/**
 * @brief 重新定位子工具栏（保持相对主工具栏的位置）
 * @author chiangyang
 */
void PinAnnotationToolBar::repositionSubToolbar() {
    if (subToolbarWindow && subToolbarWindow->isVisible()) {
        // 子工具栏保持在主工具栏正下方
        subToolbarWindow->move(this->x(), this->y() + this->height());
    }
}

/**
 * @brief 移动事件处理，同步子工具栏位置
 * @param event 移动事件
 * @author chiangyang
 */
void PinAnnotationToolBar::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    repositionSubToolbar();
}

/**
 * @brief 更新按钮样式（文字/图标模式切换）
 * @author chiangyang
 */
void PinAnnotationToolBar::updateButtonStyles() {
    const bool isIcon = StyleManager::getToolbarButtonStyle() == "icon";
    auto *tm = TranslationManager::instance();

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
void PinAnnotationToolBar::retranslateUi() {
    auto *tm = TranslationManager::instance();

    retranslateAnnotationButtons();

    // 操作按钮文案统一遍历基类注册表
    retranslateActionButtons();

    updateButtonStyles();
    adjustSize();
}
