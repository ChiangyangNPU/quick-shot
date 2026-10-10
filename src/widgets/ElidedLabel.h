#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QString>

/**
 * @brief 单行省略号标签
 *
 * 文字超过可用宽度时右侧以省略号截断（配合 setWordWrap(false) 保持单行），
 * 并同步设置 tooltip，鼠标悬停可查看完整文字。
 * 用于固定宽度窗口下避免提示文字被直接裁掉且无迹可寻。
 * 注：Qt6 的 QLabel 没有 setTextElideMode，需在 paintEvent 中用 QFontMetrics 手动省略。
 * @author chiangyang
 */
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {}

    // QLabel::setText 非虚函数，此处以同名覆盖（hiding），
    // 调用方通过 ElidedLabel* 访问即可命中本实现
    void setText(const QString &text) {
        m_fullText = text;
        QLabel::setText(text);
        setToolTip(text);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        // 以当前实际宽度省略，超出部分显示为 "…"，悬停 tooltip 查看全文
        const QString elided = fontMetrics().elidedText(m_fullText, Qt::ElideRight, width());
        painter.drawText(rect(), alignment(), elided);
    }

private:
    QString m_fullText; ///< 完整文字（绘制时按宽度省略）
};

#endif // ELIDEDLABEL_H
