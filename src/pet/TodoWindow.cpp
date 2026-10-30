#include "TodoWindow.h"
#include "TodoStore.h"
#include "PetTheme.h"
#include "PetUiScale.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateEdit>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {

/**
 * @brief 任务行控件
 *
 * 透明背景，悬停时绘制一层很浅的圆角底色，底部绘制 1px 分隔线。
 * 用细线与灰阶层次替代卡片，避免多重色彩叠加。
 * @author chiangyang
 */
class TodoRowWidget : public QWidget {
public:
    explicit TodoRowWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_Hover, true);
    }

    /**
     * @brief 更新分隔线与悬停底色
     * @param divider 分隔线颜色
     * @param hover 悬停底色
     * @author chiangyang
     */
    void setColors(const QColor &divider, const QColor &hover) {
        m_divider = divider;
        m_hover = hover;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_hovered && m_hover.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_hover);
            p.drawRoundedRect(QRectF(rect()).adjusted(0, 1, 0, -1.5), 8, 8);
        }
        p.setPen(QPen(m_divider, 1));
        p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
    }

    void enterEvent(QEnterEvent *event) override {
        QWidget::enterEvent(event);
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent *event) override {
        QWidget::leaveEvent(event);
        m_hovered = false;
        update();
    }

private:
    QColor m_divider;          ///< 底部分隔线颜色
    QColor m_hover;            ///< 悬停底色
    bool m_hovered = false;    ///< 当前是否悬停
};

/**
 * @brief 单行省略标题（宽度不足时以省略号收尾）
 *
 * 待办标题在气泡内保持单行，既符合紧凑列表，也让行高可预测，
 * 便于气泡按内容高度自适应。
 * @author chiangyang
 */
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    /**
     * @brief 设置完整文本（绘制时按可用宽度省略）
     * @param text 完整标题
     * @author chiangyang
     */
    void setFullText(const QString &text) {
        m_fullText = text;
        updateGeometry();
        update();
    }

    /**
     * @brief 尺寸提示：按文本宽度与单行行高给出，保证行高稳定
     * @return 提示尺寸
     * @author chiangyang
     */
    QSize sizeHint() const override {
        const_cast<ElidedLabel *>(this)->ensurePolished();
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(m_fullText), fm.height());
    }

    /**
     * @brief 最小尺寸提示：高度与提示一致，避免布局压缩成 0
     * @return 最小提示尺寸
     * @author chiangyang
     */
    QSize minimumSizeHint() const override {
        const_cast<ElidedLabel *>(this)->ensurePolished();
        const QFontMetrics fm(font());
        return QSize(0, fm.height());
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        const QFontMetrics fm(font());
        const QString shown = fm.elidedText(m_fullText, Qt::ElideRight, width());
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, shown);
    }

    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        update();
    }

private:
    QString m_fullText; ///< 完整标题
};

/**
 * @brief 递归抛光布局内的控件
 *
 * 新建的控件在首次显示前尚未完成样式抛光，此时 sizeHint 按默认字体计算，
 * 会让按内容自适应的气泡高度偏小；这里主动抛光一次，保证测量稳定。
 * @param layout 目标布局
 * @author chiangyang
 */
void polishLayoutWidgets(QLayout *layout) {
    if (!layout) {
        return;
    }
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (!item) {
            continue;
        }
        if (QWidget *w = item->widget()) {
            w->ensurePolished();
            polishLayoutWidgets(w->layout());
        } else if (QLayout *child = item->layout()) {
            polishLayoutWidgets(child);
        }
    }
}

/**
 * @brief 下拉弹窗通用样式（保证弹出项不透明且可读）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString popupViewStyle(bool dark) {
    return QStringLiteral(
        "QComboBox QAbstractItemView { background-color: %1; color: %2;"
        " border: 1px solid %3; outline: none; padding: 0.2em; }"
        "QComboBox QAbstractItemView::item { padding: 0.3em 0.5em; }"
        "QComboBox QAbstractItemView::item:hover { background-color: %4; }"
        "QComboBox QAbstractItemView::item:selected { background-color: %4; color: %2; }")
        .arg(PetTheme::fieldColor(dark).name(), PetTheme::textColor(dark).name(),
             PetTheme::dividerColor(dark).name(), PetTheme::hoverColor(dark).name());
}

/**
 * @brief 输入框样式（填充式、无边框、圆角，与下拉/日期视觉统一）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString inputStyle(bool dark) {
    return QStringLiteral(
        "QLineEdit { background-color: %1; border: none; border-radius: 8px;"
        " padding: 0 0.7em; color: %2; font-size: 10.5pt;"
        " selection-background-color: %3; selection-color: %4; }")
        .arg(PetTheme::fieldColor(dark).name(), PetTheme::textColor(dark).name(),
             PetTheme::accentColor(dark).name(), PetTheme::accentTextColor(dark).name());
}

/**
 * @brief 输入区下拉样式（填充式小胶囊）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString fieldComboStyle(bool dark) {
    return QStringLiteral(
        "QComboBox { background-color: %1; border: none; border-radius: 8px;"
        " padding: 0 0.6em; color: %2; font-size: 9.5pt; }"
        "QComboBox::drop-down { border: none; width: 1.1em; }"
        "QComboBox::down-arrow { image: url(%3); width: 0.5em; height: 0.5em; }")
        .arg(PetTheme::fieldColor(dark).name(), PetTheme::textColor(dark).name(), PetTheme::downArrowIcon(dark))
        + popupViewStyle(dark);
}

/**
 * @brief 行内优先级下拉样式（透明无边框、小号灰字）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString metaComboStyle(bool dark) {
    return QStringLiteral(
        "QComboBox { background: transparent; border: none; padding: 0 0.1em;"
        " color: %1; font-size: 9pt; }"
        "QComboBox:disabled { color: %1; }"
        "QComboBox::drop-down { border: none; width: 0.85em; }"
        "QComboBox::down-arrow { image: url(%2); width: 0.42em; height: 0.42em; }")
        .arg(PetTheme::mutedColor(dark).name(), PetTheme::downArrowIcon(dark))
        + popupViewStyle(dark);
}

/**
 * @brief 输入区日期样式（填充式胶囊）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString fieldDateStyle(bool dark) {
    return QStringLiteral(
        "QDateEdit { background-color: %1; border: none; border-radius: 8px;"
        " padding: 0 0.5em; color: %2; font-size: 9.5pt; }"
        "QDateEdit::drop-down { border: none; width: 1.1em; }"
        "QDateEdit::down-arrow { image: url(%3); width: 0.5em; height: 0.5em; }")
        .arg(PetTheme::fieldColor(dark).name(), PetTheme::textColor(dark).name(), PetTheme::downArrowIcon(dark));
}

/**
 * @brief 行内日期样式（透明无边框文字，颜色表达逾期/今天）
 * @param color 日期文字颜色
 * @param dark 是否深色主题（决定箭头图标明暗）
 * @author chiangyang
 */
QString metaDateStyle(const QColor &color, bool dark) {
    return QStringLiteral(
        "QDateEdit { background: transparent; border: none; padding: 0 0.1em;"
        " color: %1; font-size: 9pt; }"
        "QDateEdit:disabled { color: %1; }"
        "QDateEdit::drop-down { border: none; width: 0.85em; }"
        "QDateEdit::down-arrow { image: url(%2); width: 0.42em; height: 0.42em; }")
        .arg(color.name(), PetTheme::downArrowIcon(dark));
}

/**
 * @brief "截止"开关样式（未开启透明，开启后填充胶囊）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString toggleChipStyle(bool dark) {
    return QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px;"
        " padding: 0 0.6em; color: %1; font-size: 9pt; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:checked { background-color: %3; color: %2; }")
        .arg(PetTheme::mutedColor(dark).name(), PetTheme::textColor(dark).name(), PetTheme::fieldColor(dark).name());
}

/**
 * @brief 添加按钮样式（强调色填充、悬停/按下微调明度）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString primaryButtonStyle(bool dark) {
    const QColor accent = PetTheme::accentColor(dark);
    return QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: none;"
        " border-radius: 8px; padding: 0 1.1em; font-weight: 600; font-size: 9.5pt; }"
        "QPushButton:hover { background-color: %3; }"
        "QPushButton:pressed { background-color: %4; }")
        .arg(accent.name(), PetTheme::accentTextColor(dark).name(),
             accent.lighter(112).name(), accent.darker(112).name());
}

/**
 * @brief 行内勾选框样式
 *
 * 勾选态沿用应用统一的蓝色强调色（与设置菜单的按钮/复选框一致），
 * 并固定指示器尺寸，避免勾选前后尺寸跳变。
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString checkStyle(bool dark) {
    const QColor checked = StyleManager::getToolbarButtonCheckedColor();
    return QStringLiteral(
        "QCheckBox::indicator { width: 1.05em; height: 1.05em;"
        " border: 1px solid %1; border-radius: 0.22em; background: transparent; }"
        "QCheckBox::indicator:hover { border-color: %2; }"
        "QCheckBox::indicator:checked { border-color: %3; background-color: %3;"
        " image: url(:/icons/check.svg); }")
        .arg(PetTheme::dividerColor(dark).name(), PetTheme::mutedColor(dark).name(), checked.name());
}

/**
 * @brief 删除按钮样式（静默幽灵按钮，悬停变红）
 * @author chiangyang
 */
QString ghostDeleteStyle(bool dark) {
    return QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none;"
        " border-radius: 5px; font-size: 12pt; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(PetTheme::mutedColor(dark).name(), PetTheme::overdueColor(dark).name());
}

/**
 * @brief 排序规则：未完成优先 → 有截止的在前且按日期升序 → 优先级高在前 → 创建早在前
 * @author chiangyang
 */
bool todoLessThan(const TodoItem &a, const TodoItem &b) {
    if (a.done != b.done) {
        return !a.done;
    }
    const bool aHasDue = a.dueDate.isValid();
    const bool bHasDue = b.dueDate.isValid();
    if (aHasDue != bHasDue) {
        return aHasDue;
    }
    if (aHasDue && a.dueDate != b.dueDate) {
        return a.dueDate < b.dueDate;
    }
    if (a.priority != b.priority) {
        return int(a.priority) > int(b.priority);
    }
    return a.createdAt < b.createdAt;
}

} // namespace

/**
 * @brief 构造函数
 * @author chiangyang
 */
TodoWindow::TodoWindow(QWidget *parent)
    : QWidget(parent) {
    setWindowTitle(TranslationManager::instance()->get("todo.windowTitle", "Todo List"));
    setWindowIcon(StyleManager::loadAppIcon());

    // 气泡形态：无边框 + 半透明背景，自绘圆角面板与指向宠物的小三角；
    // StaysOnTop 与宿主宠物（置顶）保持同层，避免被前台应用窗口盖住
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
                   | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 尺寸按屏幕 DPI 缩放（与应用其余窗口一致：96 为逻辑基准）
    m_shadowPad = PetUi::scaled(10);
    m_arrowHeight = PetUi::scaled(10);
    m_contentPad = PetUi::scaled(16);
    resize(PetUi::scaled(470), PetUi::scaled(560));
    setMinimumWidth(PetUi::scaled(420));

    setupUi();
    updateWindowColors();
    retranslateUi();
    m_input->setFocus();

    // 语言切换时刷新标题/占位文字/列表文案
    connect(TranslationManager::instance(), &TranslationManager::languageChanged,
            this, [this](const QString &) { retranslateUi(); });

    LOG_INFO("TodoWindow instance created");
}

/**
 * @brief 构建窗口布局与输入区控件
 * @author chiangyang
 */
void TodoWindow::setupUi() {
    TranslationManager *tm = TranslationManager::instance();

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setSpacing(PetUi::scaled(10));
    updateContentMargins();
    QVBoxLayout *root = m_rootLayout;

    // ===== 标题行：左标题、右统计 =====
    auto *header = new QHBoxLayout();
    header->setSpacing(PetUi::scaled(10));
    m_headerTitle = new QLabel(this);
    m_statsLabel = new QLabel(this);
    header->addWidget(m_headerTitle);
    header->addStretch(1);
    header->addWidget(m_statsLabel, 0, Qt::AlignBottom);
    root->addLayout(header);

    // ===== 输入区（无外框，仅控件填充） =====
    auto *inputGroup = new QVBoxLayout();
    inputGroup->setSpacing(PetUi::scaled(8));

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(PetUi::scaled(8));

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tm->get("todo.placeholder", "Add a todo..."));
    m_input->setMinimumHeight(PetUi::scaled(32));
    connect(m_input, &QLineEdit::returnPressed, this, &TodoWindow::addItemFromInput);

    m_addBtn = new QPushButton(tm->get("todo.add", "Add"), this);
    m_addBtn->setFixedHeight(PetUi::scaled(32));
    m_addBtn->setMinimumWidth(PetUi::scaled(66));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &TodoWindow::addItemFromInput);

    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_addBtn);

    auto *optionRow = new QHBoxLayout();
    optionRow->setSpacing(PetUi::scaled(6));

    m_priority = new QComboBox(this);
    StyleManager::useHighlightOnlyPopup(m_priority);
    fillPriorityCombo(m_priority);
    m_priority->setCurrentIndex(m_priority->findData(int(TodoPriority::Medium)));
    m_priority->setFixedHeight(PetUi::scaled(28));
    // 胶囊按文案自适应宽度，避免出现大片空白
    m_priority->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_priority->setCursor(Qt::PointingHandCursor);

    // "截止"用可勾选胶囊按钮，替代复选框，与输入区其他控件保持同一视觉语言
    m_dueCheck = new QPushButton(tm->get("todo.dueLabel", "Due"), this);
    m_dueCheck->setCheckable(true);
    m_dueCheck->setFixedHeight(PetUi::scaled(28));
    m_dueCheck->setCursor(Qt::PointingHandCursor);

    m_dueEdit = new QDateEdit(this);
    m_dueEdit->setCalendarPopup(true);
    m_dueEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dueEdit->setDate(QDate::currentDate());
    m_dueEdit->setFixedHeight(PetUi::scaled(28));
    m_dueEdit->setMinimumWidth(PetUi::scaled(108));
    m_dueEdit->setVisible(false);
    // 未开启"截止"时隐藏日期控件，保持输入区简洁
    connect(m_dueCheck, &QPushButton::toggled, m_dueEdit, &QWidget::setVisible);

    optionRow->addWidget(m_priority);
    optionRow->addWidget(m_dueCheck);
    optionRow->addWidget(m_dueEdit);
    optionRow->addStretch(1);

    inputGroup->addLayout(inputRow);
    inputGroup->addLayout(optionRow);
    root->addLayout(inputGroup);

    // ===== 输入区与列表的细分隔线 =====
    m_separator = new QWidget(this);
    m_separator->setFixedHeight(1);
    root->addWidget(m_separator);

    // ===== 列表区 =====
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_rowsContainer = new QWidget(m_scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, PetUi::scaled(6), 0);
    m_rowsLayout->setSpacing(0);
    m_scroll->setWidget(m_rowsContainer);
    root->addWidget(m_scroll, 1);
}

/**
 * @brief 用当前语言填充优先级下拉项
 * @author chiangyang
 */
void TodoWindow::fillPriorityCombo(QComboBox *combo) {
    TranslationManager *tm = TranslationManager::instance();
    combo->clear();
    combo->addItem(tm->get("todo.priority.low", "Low"), int(TodoPriority::Low));
    combo->addItem(tm->get("todo.priority.medium", "Medium"), int(TodoPriority::Medium));
    combo->addItem(tm->get("todo.priority.high", "High"), int(TodoPriority::High));
}

/**
 * @brief 刷新标题右侧"未完成 / 已完成"统计文字
 * @author chiangyang
 */
void TodoWindow::updateHeaderStats() {
    if (!m_statsLabel) {
        return;
    }
    int done = 0;
    const QVector<TodoItem> items = TodoStore::instance()->list();
    for (const TodoItem &item : items) {
        if (item.done) {
            ++done;
        }
    }
    const int open = items.size() - done;
    m_statsLabel->setText(
        TranslationManager::instance()->get("todo.stats", "Open %1 · Done %2")
            .arg(open)
            .arg(done));
}

/**
 * @brief 读取输入区内容新增一条待办
 * @author chiangyang
 */
void TodoWindow::addItemFromInput() {
    const QString title = m_input->text().trimmed();
    if (title.isEmpty()) {
        return;
    }
    const TodoPriority priority =
        static_cast<TodoPriority>(m_priority->currentData().toInt());
    const QDate due = m_dueCheck->isChecked() ? m_dueEdit->date() : QDate();

    TodoStore::instance()->add(title, priority, due);

    // 清空输入区并回到默认状态，方便连续录入
    m_input->clear();
    m_priority->setCurrentIndex(m_priority->findData(int(TodoPriority::Medium)));
    m_dueCheck->setChecked(false);
    m_dueEdit->setDate(QDate::currentDate());
    reloadItems();
    m_input->setFocus();
}

/**
 * @brief 按待办项创建一行列表控件（单行：勾选 + 标题 + 优先级 + 截止 + 删除）
 * @author chiangyang
 */
QWidget *TodoWindow::createRowWidget(const TodoItem &item) {
    TranslationManager *tm = TranslationManager::instance();

    auto *row = new TodoRowWidget(m_rowsContainer);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(PetUi::scaled(4), PetUi::scaled(6),
                          PetUi::scaled(4), PetUi::scaled(6));
    h->setSpacing(PetUi::scaled(10));

    auto *doneBox = new QCheckBox(row);
    doneBox->setChecked(item.done);
    doneBox->setToolTip(tm->get("todo.markDone", "Mark as done"));
    doneBox->setCursor(Qt::PointingHandCursor);

    auto *title = new ElidedLabel(row);

    // 行内优先级：透明小号下拉，不额外占用视觉重量
    auto *prio = new QComboBox(row);
    StyleManager::useHighlightOnlyPopup(prio);
    fillPriorityCombo(prio);
    prio->setCurrentIndex(prio->findData(int(item.priority)));
    prio->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    prio->setMinimumHeight(PetUi::scaled(22));
    prio->setCursor(Qt::PointingHandCursor);

    // 截止日期：有日期时显示精简日期文字，无日期时显示可点击的"截止"
    auto *dueEdit = new QDateEdit(row);
    dueEdit->setCalendarPopup(true);
    dueEdit->setDisplayFormat(QStringLiteral("MM-dd"));
    dueEdit->setDate(item.dueDate.isValid() ? item.dueDate : QDate::currentDate());
    dueEdit->setFixedWidth(PetUi::scaled(50));
    dueEdit->setMinimumHeight(PetUi::scaled(22));
    dueEdit->setProperty("hasDue", item.dueDate.isValid());
    dueEdit->setCursor(Qt::PointingHandCursor);
    dueEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    dueEdit->setToolTip(tm->get("todo.clearDueHint", "Right-click to clear due date"));

    auto *dueBtn = new QPushButton(tm->get("todo.dueLabel", "Due"), row);
    dueBtn->setFixedHeight(PetUi::scaled(24));
    dueBtn->setFixedWidth(PetUi::scaled(50));
    dueBtn->setCursor(Qt::PointingHandCursor);
    dueBtn->setFlat(true);

    auto *delBtn = new QPushButton(QStringLiteral("×"), row);
    delBtn->setToolTip(tm->get("todo.delete", "Delete"));
    delBtn->setFixedSize(PetUi::scaled(22), PetUi::scaled(22));
    delBtn->setCursor(Qt::PointingHandCursor);

    h->addWidget(doneBox);
    h->addWidget(title, 1);
    h->addWidget(prio);
    h->addWidget(dueEdit);
    h->addWidget(dueBtn);
    h->addWidget(delBtn);

    const QString rawTitle = item.title;

    // 按当前完成/截止状态刷新文字与控件配色（主题切换时复用此回调）
    auto refresh = [row, title, rawTitle, doneBox, prio, dueEdit, dueBtn, delBtn]() {
        const bool dark = PetTheme::isDark();
        const bool done = doneBox->isChecked();
        const bool hasDue = dueEdit->property("hasDue").toBool();
        const QDate due = hasDue ? dueEdit->date() : QDate();
        const bool overdue = TodoWindow::isOverdue(done, due);
        const bool today = TodoWindow::isDueToday(done, due);

        row->setColors(PetTheme::dividerColor(dark), PetTheme::hoverColor(dark));

        // 完成态：标题置灰（勾选 + 灰字 + 下沉排序表达已完成）
        title->setFullText(rawTitle);
        title->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10.5pt; font-weight: %2;")
                .arg(done ? PetTheme::mutedColor(dark).name() : PetTheme::textColor(dark).name())
                .arg(done ? QStringLiteral("400") : QStringLiteral("500")));

        // 勾选框保持主题内配色，避免沿用全局蓝色强调色
        doneBox->setStyleSheet(checkStyle(dark));

        // 完成后保留行结构（仅禁用与置灰），避免勾选瞬间行高和信息跳变
        prio->setVisible(true);
        dueEdit->setVisible(hasDue);
        dueBtn->setVisible(!hasDue);
        prio->setEnabled(!done);
        dueEdit->setEnabled(!done && hasDue);
        dueBtn->setEnabled(!done && !hasDue);

        prio->setStyleSheet(metaComboStyle(dark));
        dueBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none;"
                           " text-align: left; padding: 0 0.2em; font-size: 9pt; color: %1; }"
                           "QPushButton:hover { color: %2; }"
                           "QPushButton:disabled { color: %1; }")
                .arg(PetTheme::mutedColor(dark).name(), PetTheme::textColor(dark).name()));

        QColor dateColor = PetTheme::mutedColor(dark);
        if (overdue) {
            dateColor = PetTheme::overdueColor(dark);
        } else if (today) {
            dateColor = PetTheme::todayColor(dark);
        }
        dueEdit->setStyleSheet(metaDateStyle(dateColor, dark));

        // 日期控件的提示同时说明当前状态与右键清除入口
        TranslationManager *tm = TranslationManager::instance();
        if (overdue) {
            dueEdit->setToolTip(tm->get("todo.status.overdue", "Overdue"));
        } else if (today) {
            dueEdit->setToolTip(tm->get("todo.status.today", "Due today"));
        } else {
            dueEdit->setToolTip(tm->get("todo.clearDueHint", "Right-click to clear due date"));
        }
        delBtn->setStyleSheet(ghostDeleteStyle(dark));
    };

    // 勾选完成：写回存储并重建列表（完成后排到未完成项之后）
    connect(doneBox, &QCheckBox::toggled, this, [this, id = item.id](bool checked) {
        TodoStore::instance()->setDone(id, checked);
        // 延迟到下一轮事件循环重建，避免在信号发送过程中删除当前行控件
        QTimer::singleShot(0, this, &TodoWindow::reloadItems);
    });

    // 切换优先级
    connect(prio, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [id = item.id, prio, refresh](int) {
        TodoItem current = TodoStore::instance()->find(id);
        if (current.id.isEmpty()) {
            return;
        }
        current.priority = static_cast<TodoPriority>(prio->currentData().toInt());
        TodoStore::instance()->update(current);
        refresh();
    });

    // 点击"截止"：设为今天并展开日期控件
    connect(dueBtn, &QPushButton::clicked, this, [id = item.id, dueEdit, refresh]() {
        TodoItem current = TodoStore::instance()->find(id);
        if (current.id.isEmpty()) {
            return;
        }
        const QDate today = QDate::currentDate();
        current.dueDate = today;
        TodoStore::instance()->update(current);
        dueEdit->setProperty("hasDue", true);
        dueEdit->blockSignals(true);
        dueEdit->setDate(today);
        dueEdit->blockSignals(false);
        refresh();
    });

    // 修改截止日期
    connect(dueEdit, &QDateEdit::dateChanged, this,
            [id = item.id, dueEdit, refresh](const QDate &date) {
        if (!dueEdit->property("hasDue").toBool()) {
            return;
        }
        TodoItem current = TodoStore::instance()->find(id);
        if (current.id.isEmpty()) {
            return;
        }
        current.dueDate = date;
        TodoStore::instance()->update(current);
        refresh();
    });

    // 右键清除截止日期
    connect(dueEdit, &QWidget::customContextMenuRequested, this,
            [id = item.id, dueEdit, refresh](const QPoint &) {
        QMenu menu;
        QAction *clear = menu.addAction(
            TranslationManager::instance()->get("todo.clearDue", "Clear due date"));
        if (menu.exec(QCursor::pos()) != clear) {
            return;
        }
        TodoItem current = TodoStore::instance()->find(id);
        if (current.id.isEmpty()) {
            return;
        }
        current.dueDate = QDate();
        TodoStore::instance()->update(current);
        dueEdit->setProperty("hasDue", false);
        refresh();
    });

    // 删除（延迟重建，避免在按钮自己的 clicked 信号中销毁自身）
    connect(delBtn, &QPushButton::clicked, this, [this, id = item.id]() {
        TodoStore::instance()->remove(id);
        QTimer::singleShot(0, this, &TodoWindow::reloadItems);
    });

    m_rowRefreshers.append(refresh);
    refresh();
    return row;
}

/**
 * @brief 清空当前列表
 * @author chiangyang
 */
void TodoWindow::clearRows() {
    m_rowRefreshers.clear();
    if (!m_rowsLayout) {
        return;
    }
    while (QLayoutItem *entry = m_rowsLayout->takeAt(0)) {
        if (QWidget *w = entry->widget()) {
            delete w;
        }
        delete entry;
    }
}

/**
 * @brief 从 TodoStore 重新加载并重建待办列表
 * @author chiangyang
 */
void TodoWindow::reloadItems() {
    clearRows();

    QVector<TodoItem> items = TodoStore::instance()->list();
    std::sort(items.begin(), items.end(), todoLessThan);

    if (items.isEmpty()) {
        // 空状态：极简灰阶提示，不引入额外色彩
        auto *emptyBox = new QWidget(m_rowsContainer);
        auto *emptyLayout = new QVBoxLayout(emptyBox);
        emptyLayout->setContentsMargins(0, 0, 0, 0);
        emptyLayout->setSpacing(PetUi::scaled(8));

        auto *glyph = new QLabel(QStringLiteral("✓"), emptyBox);
        glyph->setAlignment(Qt::AlignCenter);

        auto *emptyText = new QLabel(
            TranslationManager::instance()->get("todo.empty", "No todos yet"), emptyBox);
        emptyText->setAlignment(Qt::AlignCenter);
        emptyText->setWordWrap(true);
        emptyText->setFixedWidth(PetUi::scaled(320));

        emptyLayout->addWidget(glyph);
        emptyLayout->addWidget(emptyText, 0, Qt::AlignHCenter);

        m_rowsLayout->addStretch(1);
        m_rowsLayout->addWidget(emptyBox);
        m_rowsLayout->addStretch(1);
        emptyBox->show(); // 隐藏控件不参与布局尺寸计算

        m_rowRefreshers.append([glyph, emptyText]() {
            const bool dark = PetTheme::isDark();
            glyph->setStyleSheet(
                QStringLiteral("color: %1; font-size: 20pt; font-weight: 500;")
                    .arg(PetTheme::mutedColor(dark).name()));
            emptyText->setStyleSheet(
                QStringLiteral("color: %1; font-size: 9.5pt;").arg(PetTheme::mutedColor(dark).name()));
        });
        m_rowRefreshers.last()();
    } else {
        for (const TodoItem &item : items) {
            QWidget *row = createRowWidget(item);
            m_rowsLayout->addWidget(row);
            row->show(); // 隐藏控件不参与布局尺寸计算
        }
        m_rowsLayout->addStretch(1);
    }

    updateHeaderStats();
    updateBubbleHeight();
    // 布局在下一轮事件循环才完成尺寸计算，届时校准一次高度
    QTimer::singleShot(0, this, [this]() { updateBubbleHeight(); });
    emit itemsChanged();
}

/**
 * @brief 按当前主题刷新窗口、输入区与所有待办行的配色
 * @author chiangyang
 */
void TodoWindow::updateWindowColors() {
    const bool dark = PetTheme::isDark();

    // 气泡背景由 paintEvent 自绘，子控件保持透明以露出面板圆角
    setAutoFillBackground(false);
    if (m_rowsContainer) {
        m_rowsContainer->setAutoFillBackground(false);
    }
    if (m_scroll) {
        m_scroll->viewport()->setAutoFillBackground(false);
    }
    update();

    // 标题行：左标题右统计，靠字号与字重拉开层级
    if (m_headerTitle) {
        m_headerTitle->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 17pt; font-weight: 600;").arg(PetTheme::textColor(dark).name()));
    }
    if (m_statsLabel) {
        m_statsLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 9pt;").arg(PetTheme::mutedColor(dark).name()));
    }

    // 输入区控件：统一填充式外观，消除系统灰白方块的割裂感
    if (m_input) {
        m_input->setStyleSheet(inputStyle(dark));
    }
    if (m_addBtn) {
        m_addBtn->setStyleSheet(primaryButtonStyle(dark));
    }
    if (m_priority) {
        m_priority->setStyleSheet(fieldComboStyle(dark));
    }
    if (m_dueCheck) {
        m_dueCheck->setStyleSheet(toggleChipStyle(dark));
    }
    if (m_dueEdit) {
        m_dueEdit->setStyleSheet(fieldDateStyle(dark));
    }

    // 细分隔线
    if (m_separator) {
        QPalette sepPalette = m_separator->palette();
        sepPalette.setColor(QPalette::Window, PetTheme::dividerColor(dark));
        m_separator->setPalette(sepPalette);
        m_separator->setAutoFillBackground(true);
    }

    // 各行的文字/控件风格
    for (const auto &refresh : m_rowRefreshers) {
        refresh();
    }
}

/**
 * @brief 按当前语言刷新窗口标题、占位文字与列表内容
 * @author chiangyang
 */
void TodoWindow::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();
    setWindowTitle(tm->get("todo.windowTitle", "Todo List"));
    if (m_headerTitle) {
        m_headerTitle->setText(tm->get("todo.windowTitle", "Todo List"));
    }
    if (m_input) {
        m_input->setPlaceholderText(tm->get("todo.placeholder", "Add a todo..."));
    }
    if (m_addBtn) {
        m_addBtn->setText(tm->get("todo.add", "Add"));
    }
    if (m_dueCheck) {
        m_dueCheck->setText(tm->get("todo.dueLabel", "Due"));
    }
    if (m_priority) {
        // 保留当前选择，仅替换下拉项文案
        const QVariant current = m_priority->currentData();
        fillPriorityCombo(m_priority);
        const int index = m_priority->findData(current);
        m_priority->setCurrentIndex(index >= 0 ? index
                                              : m_priority->findData(int(TodoPriority::Medium)));
    }
    reloadItems();
}

/**
 * @brief 设置气泡相对宠物的朝向
 * @param arrowBelow true 表示气泡在宠物上方、小三角朝下；false 表示在下方、朝上
 * @param anchorX 小三角在窗口内的水平位置（宠物中心）
 * @author chiangyang
 */
void TodoWindow::setBubblePlacement(bool arrowBelow, int anchorX) {
    m_arrowBelow = arrowBelow;
    // 三角不在两端过近处，避免圆心落在圆角外
    const int edge = PetUi::scaled(28);
    m_anchorX = qBound(edge, anchorX, width() - edge);
    updateContentMargins();
    update();
}

/**
 * @brief 按小三角所在侧重新计算内容边距（给小三角和投影留出空间）
 * @author chiangyang
 */
void TodoWindow::updateContentMargins() {
    if (!m_rootLayout) {
        return;
    }
    const int side = m_shadowPad + m_contentPad;
    const int top = m_shadowPad + m_contentPad + (m_arrowBelow ? 0 : m_arrowHeight);
    const int bottom = m_shadowPad + m_contentPad + (m_arrowBelow ? m_arrowHeight : 0);
    m_rootLayout->setContentsMargins(side, top, side, bottom);
}

/**
 * @brief 按任务数量调整气泡高度（内容少时贴合，超过上限时列表滚动）
 * @author chiangyang
 */
void TodoWindow::updateBubbleHeight() {
    if (!m_scroll || !m_rowsLayout || !m_rootLayout) {
        return;
    }
    // 先抛光新建控件，避免读到按默认字体算出的偏小尺寸
    polishLayoutWidgets(m_rowsLayout);
    polishLayoutWidgets(m_rootLayout);

    const int minRows = PetUi::scaled(110);
    const int maxRows = PetUi::scaled(380);
    const int rows = qBound(minRows, m_rowsLayout->sizeHint().height(), maxRows);
    m_scroll->setFixedHeight(rows);

    // 累加列表中除滚动区以外的固定内容（标题行、输入区、分隔线）与内边距
    const QMargins margins = m_rootLayout->contentsMargins();
    int fixedPart = margins.top() + margins.bottom();
    int fixedItems = 0;
    for (int i = 0; i < m_rootLayout->count(); ++i) {
        QLayoutItem *item = m_rootLayout->itemAt(i);
        if (!item || item->widget() == m_scroll) {
            continue;
        }
        fixedPart += item->sizeHint().height();
        ++fixedItems;
    }
    fixedPart += m_rootLayout->spacing() * fixedItems;

    const int total = fixedPart + rows;
    if (height() != total) {
        resize(width(), total);
        emit bubbleSizeChanged();
    }
}

/**
 * @brief 绘制气泡背景：圆角面板、柔和投影与指向宠物的小三角
 * @param event 绘制事件
 * @author chiangyang
 */
void TodoWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool dark = PetTheme::isDark();
    const QColor bg = StyleManager::getTabWidgetBgColor();
    const QColor border = PetTheme::dividerColor(dark);
    const int radius = PetUi::scaled(14);

    // 面板矩形：四周留投影空间，三角所在侧额外让出三角高度
    QRectF panel = QRectF(rect()).adjusted(m_shadowPad, m_shadowPad,
                                           -m_shadowPad, -m_shadowPad);
    if (m_arrowBelow) {
        panel.setBottom(panel.bottom() - m_arrowHeight);
    } else {
        panel.setTop(panel.top() + m_arrowHeight);
    }

    // 柔和投影：几层逐渐外扩的低透明度圆角矩形
    p.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        const int spread = PetUi::scaled(i);
        p.setBrush(QColor(0, 0, 0, dark ? 28 : 14));
        p.drawRoundedRect(panel.adjusted(-spread, -spread + PetUi::scaled(2),
                                         spread, spread + PetUi::scaled(2)),
                          radius + spread, radius + spread);
    }

    // 面板
    p.setBrush(bg);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    // 指向宠物的小三角（填充面板色，仅描两侧边，避免压住面板描边）
    const int half = PetUi::scaled(9);
    const qreal x = (m_anchorX >= 0) ? m_anchorX : width() / 2.0;
    QPolygonF tri;
    if (m_arrowBelow) {
        tri << QPointF(x - half, panel.bottom())
            << QPointF(x, panel.bottom() + m_arrowHeight)
            << QPointF(x + half, panel.bottom());
    } else {
        tri << QPointF(x - half, panel.top())
            << QPointF(x, panel.top() - m_arrowHeight)
            << QPointF(x + half, panel.top());
    }
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawPolygon(tri);
    p.setPen(QPen(border, 1));
    p.drawLine(tri.at(0), tri.at(1));
    p.drawLine(tri.at(1), tri.at(2));
}

/**
 * @brief 按住空白处记录拖动起点（无标题栏）
 * @param event 鼠标事件
 * @author chiangyang
 */
void TodoWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

/**
 * @brief 拖动气泡窗口
 * @param event 鼠标事件
 * @author chiangyang
 */
void TodoWindow::mouseMoveEvent(QMouseEvent *event) {
    if ((event->buttons() & Qt::LeftButton) && !m_dragOffset.isNull()) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

/**
 * @brief Esc 收起气泡
 * @param event 按键事件
 * @author chiangyang
 */
void TodoWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

/**
 * @brief 显示时按最新主题/语言刷新，并重新计算截止日期高亮
 * @author chiangyang
 */
void TodoWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // 重新加载可让"今天/逾期"判断基于当前日期，并让语言/主题立即生效
    reloadItems();
    updateWindowColors();
    if (m_input) {
        m_input->setFocus();
    }
}

/**
 * @brief 待办是否已逾期
 * @author chiangyang
 */
bool TodoWindow::isOverdue(bool done, const QDate &due) {
    return !done && due.isValid() && due < QDate::currentDate();
}

/**
 * @brief 待办是否今天到期
 * @author chiangyang
 */
bool TodoWindow::isDueToday(bool done, const QDate &due) {
    return !done && due.isValid() && due == QDate::currentDate();
}
