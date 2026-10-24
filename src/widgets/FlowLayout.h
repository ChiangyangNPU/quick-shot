#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>
#include <QWidget>

/**
 * @brief 流式布局：子项从左到右排列，宽度不够自动换行
 *
 * Qt 无内置流式布局，本类为 Qt 官方 FlowLayout 示例的精简 header-only 版。
 * 供设置页宠物样式十二生肖分组使用（一行放不下自动换到下一行）。
 * @author chiangyang
 */
class FlowLayout : public QLayout {
public:
    /**
     * @brief 构造函数
     * @param parent 父控件
     * @param hSpacing 水平间距（-1 = 取布局默认 spacing）
     * @param vSpacing 垂直间距（-1 = 取布局默认 spacing）
     * @author chiangyang
     */
    explicit FlowLayout(QWidget *parent, int hSpacing = -1, int vSpacing = -1)
        : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
        setContentsMargins(0, 0, 0, 0);
    }

    ~FlowLayout() override {
        while (QLayoutItem *item = takeAt(0)) delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override {
        return (index >= 0 && index < m_items.size()) ? m_items.value(index) : nullptr;
    }
    QLayoutItem *takeAt(int index) override {
        return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override {
        return doLayout(QRect(0, 0, width, 0), /*testOnly=*/true);
    }

    QSize sizeHint() const override { return minimumSize(); }

    QSize minimumSize() const override {
        QSize size;
        for (const QLayoutItem *item : std::as_const(m_items))
            size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        size += QSize(m.left() + m.right(), m.top() + m.bottom());
        return size;
    }

    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        doLayout(rect, /*testOnly=*/false);
    }

private:
    /**
     * @brief 执行布局（testOnly=true 时只计算高度不移动子项）
     * @return 布局所需总高度
     * @author chiangyang
     */
    int doLayout(const QRect &rect, bool testOnly) const {
        const QMargins m = contentsMargins();
        int x = rect.x() + m.left();
        int y = rect.y() + m.top();
        int lineHeight = 0;

        for (QLayoutItem *item : std::as_const(m_items)) {
            const int spaceX = (m_hSpace >= 0) ? m_hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
            const int spaceY = (m_vSpace >= 0) ? m_vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
            const int nextX = x + item->sizeHint().width() + spaceX;
            if (nextX - spaceX > rect.right() - m.right() && lineHeight > 0) {
                x = rect.x() + m.left();
                y += lineHeight + spaceY;
                lineHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
            x += item->sizeHint().width() + spaceX;
            lineHeight = qMax(lineHeight, item->sizeHint().height());
        }
        return y + lineHeight - rect.y() + m.bottom();
    }

    int smartSpacing(QStyle::PixelMetric pm) const {
        QObject *parent = this->parent();
        if (!parent) return -1;
        if (parent->isWidgetType()) {
            auto *w = static_cast<QWidget *>(parent);
            return w->style()->pixelMetric(pm, nullptr, w);
        }
        return static_cast<QLayout *>(parent)->spacing();
    }

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};

#endif // FLOWLAYOUT_H
