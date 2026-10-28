#include "TodoWindow.h"
#include "TodoStore.h"
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
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
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
 * @brief 是否深色主题
 * @author chiangyang
 */
bool isDarkTheme() {
    return StyleManager::themeMode() == StyleManager::ThemeMode::Dark;
}

/**
 * @brief 主文字颜色
 * @author chiangyang
 */
QColor textColor(bool dark) {
    return dark ? QColor(232, 234, 237) : QColor(32, 33, 36);
}

/**
 * @brief 次要文字颜色（统计、日期、完成态、空状态）
 * @author chiangyang
 */
QColor mutedColor(bool dark) {
    return dark ? QColor(142, 144, 149) : QColor(128, 132, 139);
}

/**
 * @brief 输入框/下拉等控件的填充底色（以选项卡背景为基准微调一档）
 * @author chiangyang
 */
QColor fieldColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(142) : base.darker(105);
}

/**
 * @brief 分隔线颜色
 * @author chiangyang
 */
QColor dividerColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(150) : base.darker(111);
}

/**
 * @brief 行悬停底色（极浅，仅用于反馈）
 * @author chiangyang
 */
QColor hoverColor(bool dark) {
    const QColor base = StyleManager::getTabWidgetBgColor();
    return dark ? base.lighter(126) : base.darker(103);
}

/**
 * @brief 主强调色（宠物主题的暖橙色，深浅主题各取一档保证对比度）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor accentColor(bool dark) {
    return dark ? QColor(240, 160, 97) : QColor(199, 94, 36);
}

/**
 * @brief 强调色上的文字颜色
 * @param dark 是否深色主题
 * @author chiangyang
 */
QColor accentTextColor(bool dark) {
    return dark ? QColor(42, 22, 8) : QColor(255, 255, 255);
}

/**
 * @brief 逾期提示色
 * @author chiangyang
 */
QColor overdueColor(bool dark) {
    return dark ? QColor(255, 123, 114) : QColor(200, 68, 64);
}

/**
 * @brief 今天到期提示色
 * @author chiangyang
 */
QColor todayColor(bool dark) {
    return dark ? QColor(236, 178, 84) : QColor(178, 116, 18);
}

/**
 * @brief 下拉箭头图标（深色主题换用白色箭头资源，SVG 无法动态染色）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString downArrowIcon(bool dark) {
    return dark ? QStringLiteral(":/icons/spinner-down-dark.svg")
                : QStringLiteral(":/icons/spinner-down.svg");
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
        .arg(fieldColor(dark).name(), textColor(dark).name(),
             dividerColor(dark).name(), hoverColor(dark).name());
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
        .arg(fieldColor(dark).name(), textColor(dark).name(),
             accentColor(dark).name(), accentTextColor(dark).name());
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
        .arg(fieldColor(dark).name(), textColor(dark).name(), downArrowIcon(dark))
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
        .arg(mutedColor(dark).name(), downArrowIcon(dark))
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
        .arg(fieldColor(dark).name(), textColor(dark).name(), downArrowIcon(dark));
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
        .arg(color.name(), downArrowIcon(dark));
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
        .arg(mutedColor(dark).name(), textColor(dark).name(), fieldColor(dark).name());
}

/**
 * @brief 添加按钮样式（强调色填充、悬停/按下微调明度）
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString primaryButtonStyle(bool dark) {
    const QColor accent = accentColor(dark);
    return QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: none;"
        " border-radius: 8px; padding: 0 1.1em; font-weight: 600; font-size: 9.5pt; }"
        "QPushButton:hover { background-color: %3; }"
        "QPushButton:pressed { background-color: %4; }")
        .arg(accent.name(), accentTextColor(dark).name(),
             accent.lighter(112).name(), accent.darker(112).name());
}

/**
 * @brief 行内勾选框样式
 *
 * 覆盖全局 QCheckBox 的应用强调色（偏蓝），改用窗口自身的暖橙，
 * 并固定指示器尺寸，避免勾选前后尺寸跳变。
 * @param dark 是否深色主题
 * @author chiangyang
 */
QString checkStyle(bool dark) {
    // 深色下亮橙底配白色对勾对比不足，选中态取一档更深的橙
    const QColor checked = dark ? QColor(198, 118, 64) : accentColor(false);
    return QStringLiteral(
        "QCheckBox::indicator { width: 1.05em; height: 1.05em;"
        " border: 1px solid %1; border-radius: 0.22em; background: transparent; }"
        "QCheckBox::indicator:hover { border-color: %2; }"
        "QCheckBox::indicator:checked { border-color: %3; background-color: %3;"
        " image: url(:/icons/check.svg); }")
        .arg(dividerColor(dark).name(), mutedColor(dark).name(), checked.name());
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
        .arg(mutedColor(dark).name(), overdueColor(dark).name());
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
    setWindowFlags(windowFlags() | Qt::Window);

    // 窗口尺寸按屏幕 DPI 缩放（与应用其余窗口一致：96 为逻辑基准）
    resize(PetUi::scaled(520), PetUi::scaled(620));
    setMinimumSize(PetUi::scaled(460), PetUi::scaled(380));

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

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(PetUi::scaled(20), PetUi::scaled(18),
                             PetUi::scaled(20), PetUi::scaled(8));
    root->setSpacing(PetUi::scaled(10));

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
    m_priority->setMinimumWidth(PetUi::scaled(88));
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

    auto *title = new QLabel(row);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

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
        const bool dark = isDarkTheme();
        const bool done = doneBox->isChecked();
        const bool hasDue = dueEdit->property("hasDue").toBool();
        const QDate due = hasDue ? dueEdit->date() : QDate();
        const bool overdue = TodoWindow::isOverdue(done, due);
        const bool today = TodoWindow::isDueToday(done, due);

        row->setColors(dividerColor(dark), hoverColor(dark));

        // 完成态：标题置灰（勾选 + 灰字 + 下沉排序表达已完成）
        title->setText(rawTitle);
        title->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10.5pt; font-weight: %2;")
                .arg(done ? mutedColor(dark).name() : textColor(dark).name())
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
                .arg(mutedColor(dark).name(), textColor(dark).name()));

        QColor dateColor = mutedColor(dark);
        if (overdue) {
            dateColor = overdueColor(dark);
        } else if (today) {
            dateColor = todayColor(dark);
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
        emptyLayout->setContentsMargins(0, PetUi::scaled(56), 0, 0);
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

        m_rowRefreshers.append([glyph, emptyText]() {
            const bool dark = isDarkTheme();
            glyph->setStyleSheet(
                QStringLiteral("color: %1; font-size: 20pt; font-weight: 500;")
                    .arg(mutedColor(dark).name()));
            emptyText->setStyleSheet(
                QStringLiteral("color: %1; font-size: 9.5pt;").arg(mutedColor(dark).name()));
        });
        m_rowRefreshers.last()();
    } else {
        for (const TodoItem &item : items) {
            m_rowsLayout->addWidget(createRowWidget(item));
        }
        m_rowsLayout->addStretch(1);
    }

    updateHeaderStats();
}

/**
 * @brief 按当前主题刷新窗口、输入区与所有待办行的配色
 * @author chiangyang
 */
void TodoWindow::updateWindowColors() {
    const bool dark = isDarkTheme();
    const QColor bg = StyleManager::getTabWidgetBgColor();

    // 窗口与滚动区背景用调色板上色（不用样式表，避免影响子控件原生绘制）
    QPalette windowPalette = palette();
    windowPalette.setColor(QPalette::Window, bg);
    setPalette(windowPalette);
    setAutoFillBackground(true);

    if (m_rowsContainer) {
        QPalette rowsPalette = m_rowsContainer->palette();
        rowsPalette.setColor(QPalette::Window, bg);
        m_rowsContainer->setPalette(rowsPalette);
        m_rowsContainer->setAutoFillBackground(true);
    }
    if (m_scroll) {
        m_scroll->viewport()->setAutoFillBackground(true);
        QPalette viewportPalette = m_scroll->viewport()->palette();
        viewportPalette.setColor(QPalette::Window, bg);
        m_scroll->viewport()->setPalette(viewportPalette);
    }

    // 标题行：左标题右统计，靠字号与字重拉开层级
    if (m_headerTitle) {
        m_headerTitle->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 17pt; font-weight: 600;").arg(textColor(dark).name()));
    }
    if (m_statsLabel) {
        m_statsLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 9pt;").arg(mutedColor(dark).name()));
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
        sepPalette.setColor(QPalette::Window, dividerColor(dark));
        m_separator->setPalette(sepPalette);
        m_separator->setAutoFillBackground(true);
    }

    // 系统标题栏浅色/深色随主题
    StyleManager::applyDarkTitleBar(winId());

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
