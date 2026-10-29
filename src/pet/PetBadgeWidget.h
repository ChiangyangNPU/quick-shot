#ifndef PETBADGEWIDGET_H
#define PETBADGEWIDGET_H

#include <QWidget>

/**
 * @brief 宠物头顶提醒角标（红点气泡）
 *
 * 无边框透明小窗口，悬浮在宠物右上角：有待办逾期时显示红底数字（逾期数），
 * 仅今天到期时显示橙底数字（到期数），都为 0 时隐藏。点击发射 clicked 信号
 * （由 DesktopPetManager 展开待办气泡）。
 * 配色取 PetTheme 的逾期/今天到期语义色，随主题切换刷新。
 * @author chiangyang
 */
class PetBadgeWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象（顶层窗口传 nullptr）
     * @author chiangyang
     */
    explicit PetBadgeWidget(QWidget *parent = nullptr);

    /**
     * @brief 更新提醒数量（都为 0 时自动隐藏）
     * @param overdue 未完成且已逾期的待办数量
     * @param dueToday 未完成且今天到期的待办数量
     * @author chiangyang
     */
    void setReminder(int overdue, int dueToday);

signals:
    /**
     * @brief 角标点击信号（展开待办气泡用）
     * @author chiangyang
     */
    void clicked();

protected:
    /**
     * @brief 绘制圆形角标：柔和投影 + 填充圆 + 白色数字
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 左键松开发射 clicked
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int m_overdue = 0;  ///< 逾期数量（>0 时红色，优先于橙色）
    int m_dueToday = 0; ///< 今天到期数量（无逾期时显示，橙色）
};

#endif // PETBADGEWIDGET_H
