#include "PomodoroCard.h"
#include "PetTheme.h"
#include "PetUiScale.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QVBoxLayout>

/**
 * @brief 构造函数：无边框透明气泡，标题 + 按钮行
 * @author chiangyang
 */
PomodoroCard::PomodoroCard(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    setAttribute(Qt::WA_TranslucentBackground, true);

    m_shadowPad = PetUi::scaled(8);
    m_arrowHeight = PetUi::scaled(9);

    m_title = new QLabel(this);
    m_primaryBtn = new QPushButton(this);
    m_primaryBtn->setCursor(Qt::PointingHandCursor);
    m_ghostBtn = new QPushButton(this);
    m_ghostBtn->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(PetUi::scaled(8));
    btnRow->addStretch();
    btnRow->addWidget(m_ghostBtn);
    btnRow->addWidget(m_primaryBtn);

    connect(m_primaryBtn, &QPushButton::clicked, this, [this]() {
        if (m_mode == Mode::BreakDue) {
            emit breakTaken();
        } else {
            emit resumed();
        }
    });
    connect(m_ghostBtn, &QPushButton::clicked, this, [this]() { emit skipped(); });

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setSpacing(PetUi::scaled(10));
    updateContentMargins();
    m_rootLayout->addWidget(m_title);
    m_rootLayout->addLayout(btnRow);

    updateTexts();
    applyStyle();
    adjustSize();
}

/**
 * @brief 按当前主题刷新标题与按钮配色（构造与每次弹出时调用）
 * @author chiangyang
 */
void PomodoroCard::applyStyle() {
    const bool dark = PetTheme::isDark();
    const QColor accent = PetTheme::accentColor(dark);
    const QColor divider = PetTheme::dividerColor(dark);

    // 主按钮：蓝色强调（与待办气泡/设置页蓝色按钮同源）
    m_primaryBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: white; border: none;"
        " border-radius: %2px; padding: %3px %4px; font-weight: 600; }"
        "QPushButton:hover { background: %5; }"
        "QPushButton:pressed { background: %6; }")
        .arg(accent.name())
        .arg(PetUi::scaled(7)).arg(PetUi::scaled(6)).arg(PetUi::scaled(14))
        .arg(accent.lighter(112).name())
        .arg(accent.darker(110).name()));

    // 次按钮：幽灵样式
    m_ghostBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %2;"
        " border-radius: %3px; padding: %4px %5px; }"
        "QPushButton:hover { color: %6; border-color: %6; }")
        .arg(PetTheme::mutedColor(dark).name())
        .arg(divider.name())
        .arg(PetUi::scaled(7)).arg(PetUi::scaled(6)).arg(PetUi::scaled(14))
        .arg(PetTheme::textColor(dark).name()));
}

/**
 * @brief 显示"工作结束，该休息了"形态
 * @author chiangyang
 */
void PomodoroCard::showBreakDue(int breakMinutes) {
    m_mode = Mode::BreakDue;
    m_breakMinutes = breakMinutes;
    updateTexts();
    applyStyle();
    m_ghostBtn->setVisible(true);
    adjustSize();
    show();
    raise();
}

/**
 * @brief 显示"休息结束，继续工作"形态
 * @author chiangyang
 */
void PomodoroCard::showWorkDue() {
    m_mode = Mode::WorkDue;
    updateTexts();
    applyStyle();
    m_ghostBtn->setVisible(false);
    adjustSize();
    show();
    raise();
}

/**
 * @brief 设置小三角朝向与水平锚点
 * @author chiangyang
 */
void PomodoroCard::setBubblePlacement(bool arrowBelow, int anchorX) {
    m_arrowBelow = arrowBelow;
    const int edge = PetUi::scaled(24);
    m_anchorX = qBound(edge, anchorX, width() - edge);
    updateContentMargins();
    update();
}

/**
 * @brief 绘制气泡背景：圆角面板、柔和投影与指向宠物的小三角
 * @author chiangyang
 */
void PomodoroCard::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    const bool dark = PetTheme::isDark();
    const QColor bg = StyleManager::getTabWidgetBgColor();
    const QColor border = PetTheme::dividerColor(dark);

    // 面板区域：四周让出投影，小三角所在侧再让出三角高度
    QRectF panel = QRectF(rect()).adjusted(m_shadowPad, m_shadowPad,
                                           -m_shadowPad, -m_shadowPad);
    if (m_arrowBelow) {
        panel.setBottom(panel.bottom() - m_arrowHeight);
    } else {
        panel.setTop(panel.top() + m_arrowHeight);
    }
    const qreal radius = PetUi::scaled(12);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 多层低透明度圆角矩形模拟柔和投影（与待办气泡同风格）
    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 3; ++i) {
        QColor shadow(0, 0, 0, dark ? 26 : 13);
        shadow.setAlpha(shadow.alpha() / i);
        p.setBrush(shadow);
        p.drawRoundedRect(panel.translated(0, PetUi::scaled(i)),
                          radius + i, radius + i);
    }

    // 面板本体
    p.setBrush(bg);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(panel, radius, radius);

    // 小三角：填充面板色，只描两侧边避免压住面板描边
    const int cx = (m_anchorX >= 0) ? m_anchorX : qRound(panel.center().x());
    const qreal half = PetUi::scaled(8);
    QPolygonF tri;
    if (m_arrowBelow) {
        tri << QPointF(cx - half, panel.bottom())
            << QPointF(cx, panel.bottom() + m_arrowHeight)
            << QPointF(cx + half, panel.bottom());
    } else {
        tri << QPointF(cx - half, panel.top())
            << QPointF(cx, panel.top() - m_arrowHeight)
            << QPointF(cx + half, panel.top());
    }
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawPolygon(tri);
    p.setPen(QPen(border, 1));
    p.drawLine(tri.at(0), tri.at(1));
    p.drawLine(tri.at(1), tri.at(tri.size() - 1));
}

/**
 * @brief Esc 关闭卡片（按当前形态等效为跳过/继续）
 * @author chiangyang
 */
void PomodoroCard::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        if (m_mode == Mode::BreakDue) {
            emit skipped();
        } else {
            emit resumed();
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

/**
 * @brief 按当前语言与形态刷新标题与按钮文案
 * @author chiangyang
 */
void PomodoroCard::updateTexts() {
    TranslationManager *tm = TranslationManager::instance();
    const bool dark = PetTheme::isDark();
    m_title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                               .arg(PetTheme::textColor(dark).name()));
    if (m_mode == Mode::BreakDue) {
        m_title->setText(tm->get("pet.pomodoro.breakDue", "Time for a break"));
        m_primaryBtn->setText(tm->get("pet.pomodoro.takeBreak", "Take a %1-min break")
                                  .arg(m_breakMinutes));
    } else {
        m_title->setText(tm->get("pet.pomodoro.workDue", "Break over, keep going"));
        m_primaryBtn->setText(tm->get("pet.pomodoro.resume", "Resume work"));
    }
    m_ghostBtn->setText(tm->get("pet.pomodoro.skip", "Skip"));
}

/**
 * @brief 按小三角所在侧重新计算内容边距
 * @author chiangyang
 */
void PomodoroCard::updateContentMargins() {
    if (!m_rootLayout) {
        return;
    }
    const int side = m_shadowPad + PetUi::scaled(14);
    const int vPad = m_shadowPad + PetUi::scaled(10);
    const int top = vPad + (m_arrowBelow ? 0 : m_arrowHeight);
    const int bottom = vPad + (m_arrowBelow ? m_arrowHeight : 0);
    m_rootLayout->setContentsMargins(side, top, side, bottom);
}
