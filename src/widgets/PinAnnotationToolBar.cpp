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

    // 独立顶层窗口：工具类型、无边框、置顶，与 PinWindow 同级显示
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 开启窗口透明背景：顶层窗口默认矩形裁剪，圆角外的四角会被系统背景色填充，
    // 导致 QSS 的 border-radius 视觉上失效。开启后圆角外区域真正透明，圆角可见。
    setAttribute(Qt::WA_TranslucentBackground, true);
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
    auto *tm = TranslationManager::instance();

    // 撤销按钮
    m_undoBtn = new QPushButton(tm->get("undo"), this);
    StyleManager::applyActionButtonStyle(m_undoBtn);
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("PinAnnotationToolBar: Undo requested");
        emit undoRequested();
    });
    m_primaryLayout->addWidget(m_undoBtn);

    // 重做按钮
    m_redoBtn = new QPushButton(tm->get("redo"), this);
    StyleManager::applyActionButtonStyle(m_redoBtn);
    m_redoBtn->setEnabled(false);
    connect(m_redoBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("PinAnnotationToolBar: Redo requested");
        emit redoRequested();
    });
    m_primaryLayout->addWidget(m_redoBtn);

    // 清除按钮
    m_clearBtn = new QPushButton(tm->get("clear"), this);
    StyleManager::applyActionButtonStyle(m_clearBtn);
    m_clearBtn->setEnabled(false);
    connect(m_clearBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("PinAnnotationToolBar: Clear requested");
        emit clearRequested();
    });
    m_primaryLayout->addWidget(m_clearBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 复制按钮
    m_copyBtn = new QPushButton(tm->get("copy"), this);
    StyleManager::applyActionButtonStyle(m_copyBtn);
    connect(m_copyBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("PinAnnotationToolBar: Copy requested");
        emit copyRequested();
    });
    m_primaryLayout->addWidget(m_copyBtn);

    // 保存按钮
    m_saveBtn = new QPushButton(tm->get("save"), this);
    StyleManager::applyActionButtonStyle(m_saveBtn);
    connect(m_saveBtn, &QPushButton::clicked, [this]() {
        LOG_INFO("PinAnnotationToolBar: Save requested");
        emit saveRequested();
    });
    m_primaryLayout->addWidget(m_saveBtn);

    // 添加分隔符
    addSeparator(m_primaryLayout);

    // 取消按钮（退出标注模式，标注保留）。使用 cancelButton 对象名应用专属红色样式
    m_cancelBtn = new QPushButton(tm->get("cancel"), this);
    m_cancelBtn->setObjectName("cancelButton");
    StyleManager::applyCloseButtonStyle(m_cancelBtn);
    connect(m_cancelBtn, &QPushButton::clicked, [this]() {
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
 * @brief 绘制事件，主动绘制圆角矩形背景
 *
 * 透明顶层窗口下 QSS 的 background-color 不被绘制，需用 QPainter
 * 主动绘制圆角矩形背景；圆角外区域保持透明，从而显示圆角效果。
 * 圆角半径与 QSS border-radius: 0.3em 保持一致。
 * @param event 绘制事件
 * @author chiangyang
 */
void PinAnnotationToolBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 圆角半径与 QSS border-radius: 0.3em 保持一致
    const qreal radius = 0.3 * fontMetrics().horizontalAdvance(QLatin1Char('M'));
    p.setPen(Qt::NoPen);
    p.setBrush(StyleManager::getToolbarBgColor());
    p.drawRoundedRect(rect(), radius, radius);
}

/**
 * @brief 更新按钮样式（文字/图标模式切换）
 * @author chiangyang
 */
void PinAnnotationToolBar::updateButtonStyles() {
    const bool isIcon = StyleManager::getToolbarButtonStyle() == "icon";
    auto *tm = TranslationManager::instance();

    updateAnnotationButtonStyles(isIcon);

    applyButtonStyle(m_undoBtn,   ":/icons/undo.svg",  tm->get("undo"),   isIcon);
    applyButtonStyle(m_redoBtn,   ":/icons/redo.svg",  tm->get("redo"),   isIcon);
    applyButtonStyle(m_clearBtn,  ":/icons/clear.svg", tm->get("clear"),  isIcon);
    applyButtonStyle(m_copyBtn,   ":/icons/copy.svg",  tm->get("copy"),   isIcon);
    applyButtonStyle(m_saveBtn,   ":/icons/save.svg",  tm->get("save"),   isIcon);
    applyButtonStyle(m_cancelBtn, ":/icons/close.svg", tm->get("cancel"), isIcon);

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

    m_undoBtn->setText(tm->get("undo"));
    m_redoBtn->setText(tm->get("redo"));
    m_clearBtn->setText(tm->get("clear"));
    m_copyBtn->setText(tm->get("copy"));
    m_saveBtn->setText(tm->get("save"));
    m_cancelBtn->setText(tm->get("cancel"));
    updateButtonStyles();
    adjustSize();
}
