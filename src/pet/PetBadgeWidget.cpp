#include "PetBadgeWidget.h"
#include "PetTheme.h"
#include "PetUiScale.h"
#include "../utils/Utils.h"

#include <QMouseEvent>
#include <QPainter>

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetBadgeWidget::PetBadgeWidget(QWidget *parent)
    : QWidget(parent)
{
    // 与宠物窗口同款旗标：无边框置顶工具窗，macOS 下常驻且不激活应用
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);

    // 圆形直径 + 外圈投影留白
    const int d = PetUi::scaled(22) + PetUi::scaled(6);
    setFixedSize(d, d);

#ifdef Q_OS_MACOS
    Utils::setNSPanelNonActivating(this);
#endif
}

/**
 * @brief 更新提醒数量（都为 0 时自动隐藏）
 * @author chiangyang
 */
void PetBadgeWidget::setReminder(int overdue, int dueToday) {
    m_overdue = overdue;
    m_dueToday = dueToday;
    setVisible(overdue > 0 || dueToday > 0);
    update();
}

/**
 * @brief 绘制圆形角标：柔和投影 + 填充圆 + 白色数字
 * @author chiangyang
 */
void PetBadgeWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    const bool dark = PetTheme::isDark();
    const QColor fill = (m_overdue > 0) ? PetTheme::overdueColor(dark)
                                        : PetTheme::todayColor(dark);
    const int pad = PetUi::scaled(3);
    const QRectF circle = QRectF(rect()).adjusted(pad, pad, -pad, -pad);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 两层低透明度圆模拟柔和投影（与待办气泡投影同风格）
    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 2; ++i) {
        QColor shadow(0, 0, 0, dark ? 26 : 14);
        shadow.setAlpha(shadow.alpha() / i);
        p.setBrush(shadow);
        p.drawEllipse(circle.translated(0, PetUi::scaled(i)));
    }

    // 填充圆 + 浅色描边，避免与宠物贴图顺色
    p.setBrush(fill);
    p.setPen(QPen(dark ? QColor(0, 0, 0, 70) : QColor(255, 255, 255, 200), 1));
    p.drawEllipse(circle);

    // 白色数字（超过 99 显示 99+）
    const int total = m_overdue > 0 ? m_overdue : m_dueToday;
    const QString text = total > 99 ? QStringLiteral("99+") : QString::number(total);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(PetUi::scaled(total > 99 ? 9 : 11));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(circle, Qt::AlignCenter, text);
}

/**
 * @brief 左键松开发射 clicked
 * @author chiangyang
 */
void PetBadgeWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
