#include "TodoWindow.h"
#include "TodoStore.h"
#include "PetUiScale.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
 * @brief 待办行卡片（自绘圆角背景，避免样式表级联影响子控件的原生渲染）
 *
 * 不通过 setStyleSheet 上色：Qt 中给父控件设置样式表会让子控件
 * （尤其是 QCheckBox）退出原生主题绘制，深色模式下对勾可能不可见。
 * @author chiangyang
 */
class TodoRowWidget : public QWidget {
public:
    explicit TodoRowWidget(QWidget *parent = nullptr) : QWidget(parent) {}

    /**
     * @brief 更新行背景与描边颜色
     * @param background 背景色
     * @param border 描边色
     * @author chiangyang
     */
    void setColors(const QColor &background, const QColor &border) {
        m_background = background;
        m_border = border;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(m_border, 1));
        p.setBrush(m_background);
        p.drawRoundedRect(r, 8, 8);
    }

private:
    QColor m_background; ///< 行背景色
    QColor m_border;     ///< 行描边色
};

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

/**
 * @brief 行背景色（普通 / 今天到期 / 已逾期，随主题）
 * @author chiangyang
 */
QColor rowBackground(bool dark, bool overdue, bool today) {
    if (overdue) {
        return dark ? QColor(58, 34, 36) : QColor(253, 236, 236);
    }
    if (today) {
        return dark ? QColor(58, 47, 29) : QColor(255, 246, 229);
    }
    return dark ? QColor(35, 35, 42) : QColor(247, 247, 249);
}

/**
 * @brief 行描边色（普通 / 今天到期 / 已逾期，随主题）
 * @author chiangyang
 */
QColor rowBorder(bool dark, bool overdue, bool today) {
    if (overdue) {
        return dark ? QColor(192, 85, 85) : QColor(224, 108, 117);
    }
    if (today) {
        return dark ? QColor(200, 155, 74) : QColor(232, 180, 90);
    }
    return dark ? QColor(255, 255, 255, 38) : QColor(227, 227, 230);
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
    resize(PetUi::scaled(580), PetUi::scaled(620));
    setMinimumSize(PetUi::scaled(480), PetUi::scaled(360));

    setupUi();
    updateWindowColors();
    reloadItems();
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
    const int margin = PetUi::scaled(14);
    root->setContentsMargins(margin, margin, margin, margin);
    root->setSpacing(PetUi::scaled(10));

    // ===== 输入区 =====
    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(PetUi::scaled(8));

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tm->get("todo.placeholder", "Add a todo..."));
    m_input->setStyleSheet(StyleManager::getPathEditStyle());
    connect(m_input, &QLineEdit::returnPressed, this, &TodoWindow::addItemFromInput);

    m_priority = new QComboBox(this);
    StyleManager::useHighlightOnlyPopup(m_priority);
    fillPriorityCombo(m_priority);
    m_priority->setCurrentIndex(m_priority->findData(int(TodoPriority::Medium)));
    m_priority->setMinimumWidth(PetUi::scaled(76));
    m_priority->setStyleSheet(StyleManager::getSettingsComboBoxStyle());

    m_dueCheck = new QCheckBox(tm->get("todo.dueLabel", "Due"), this);
    m_dueEdit = new QDateEdit(this);
    m_dueEdit->setCalendarPopup(true);
    m_dueEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dueEdit->setDate(QDate::currentDate());
    m_dueEdit->setEnabled(false);
    m_dueEdit->setMinimumWidth(PetUi::scaled(120));
    m_dueEdit->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    connect(m_dueCheck, &QCheckBox::toggled, m_dueEdit, &QWidget::setEnabled);

    m_addBtn = new QPushButton(tm->get("todo.add", "Add"), this);
    m_addBtn->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(m_addBtn, &QPushButton::clicked, this, &TodoWindow::addItemFromInput);

    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_priority);
    inputRow->addWidget(m_dueCheck);
    inputRow->addWidget(m_dueEdit);
    inputRow->addWidget(m_addBtn);
    root->addLayout(inputRow);

    // ===== 列表区 =====
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_rowsContainer = new QWidget(m_scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, PetUi::scaled(4), 0);
    m_rowsLayout->setSpacing(PetUi::scaled(8));
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
 * @brief 按待办项创建一行列表控件
 * @author chiangyang
 */
QWidget *TodoWindow::createRowWidget(const TodoItem &item) {
    TranslationManager *tm = TranslationManager::instance();

    auto *row = new TodoRowWidget(m_rowsContainer);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(PetUi::scaled(10), PetUi::scaled(8),
                               PetUi::scaled(10), PetUi::scaled(8));
    layout->setSpacing(PetUi::scaled(8));

    // 完成勾选
    auto *doneBox = new QCheckBox(row);
    doneBox->setChecked(item.done);
    doneBox->setToolTip(tm->get("todo.markDone", "Mark as done"));

    // 待办内容（长文本自动换行）
    auto *title = new QLabel(item.title, row);
    title->setWordWrap(true);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // 优先级
    auto *prio = new QComboBox(row);
    StyleManager::useHighlightOnlyPopup(prio);
    fillPriorityCombo(prio);
    prio->setCurrentIndex(prio->findData(int(item.priority)));
    prio->setMinimumWidth(PetUi::scaled(70));

    // 截止日期：勾选"截止"后才可编辑，取消勾选即清除截止日期
    auto *dueCheck = new QCheckBox(tm->get("todo.dueLabel", "Due"), row);
    dueCheck->setChecked(item.dueDate.isValid());
    auto *dueEdit = new QDateEdit(row);
    dueEdit->setCalendarPopup(true);
    dueEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dueEdit->setDate(item.dueDate.isValid() ? item.dueDate : QDate::currentDate());
    dueEdit->setEnabled(item.dueDate.isValid());
    dueEdit->setMinimumWidth(PetUi::scaled(115));

    // 删除
    auto *delBtn = new QPushButton(QStringLiteral("×"), row);
    delBtn->setToolTip(tm->get("todo.delete", "Delete"));
    delBtn->setFixedSize(PetUi::scaled(26), PetUi::scaled(26));

    layout->addWidget(doneBox);
    layout->addWidget(title, 1);
    layout->addWidget(prio);
    layout->addWidget(dueCheck);
    layout->addWidget(dueEdit);
    layout->addWidget(delBtn);

    // 按当前完成/截止状态刷新行的颜色与文字（主题切换时也复用此回调）
    auto refresh = [row, title, doneBox, prio, dueCheck, dueEdit, delBtn]() {
        const bool dark = StyleManager::themeMode() == StyleManager::ThemeMode::Dark;
        const bool done = doneBox->isChecked();
        const QDate due = dueCheck->isChecked() ? dueEdit->date() : QDate();
        const bool overdue = TodoWindow::isOverdue(done, due);
        const bool today = TodoWindow::isDueToday(done, due);

        row->setColors(rowBackground(dark, overdue, today),
                       rowBorder(dark, overdue, today));

        if (done) {
            title->setStyleSheet(QStringLiteral("color: %1;")
                .arg(dark ? QStringLiteral("#8a8a90") : QStringLiteral("#9a9a9a")));
        } else if (overdue) {
            title->setStyleSheet(QStringLiteral("color: %1;")
                .arg(dark ? QStringLiteral("#ff7b72") : QStringLiteral("#c0392b")));
        } else if (today) {
            title->setStyleSheet(QStringLiteral("color: %1;")
                .arg(dark ? QStringLiteral("#f0b85c") : QStringLiteral("#b9770e")));
        } else {
            title->setStyleSheet(QString());
        }

        prio->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
        QString dueStyle = StyleManager::getSettingsComboBoxStyle();
        if (!done && (overdue || today)) {
            dueStyle += QStringLiteral("QDateEdit { color: %1; }")
                .arg(overdue ? (dark ? QStringLiteral("#ff7b72") : QStringLiteral("#c0392b"))
                             : (dark ? QStringLiteral("#f0b85c") : QStringLiteral("#b9770e")));
        }
        dueEdit->setStyleSheet(dueStyle);
        delBtn->setStyleSheet(StyleManager::getSettingsButtonStyle());
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

    // 勾选/取消"截止"：取消即清除截止日期
    connect(dueCheck, &QCheckBox::toggled, this, [id = item.id, dueEdit, refresh](bool checked) {
        dueEdit->setEnabled(checked);
        TodoItem current = TodoStore::instance()->find(id);
        if (current.id.isEmpty()) {
            return;
        }
        current.dueDate = checked ? dueEdit->date() : QDate();
        TodoStore::instance()->update(current);
        refresh();
    });

    // 修改截止日期
    connect(dueEdit, &QDateEdit::dateChanged, this, [id = item.id, dueCheck, refresh](const QDate &date) {
        if (!dueCheck->isChecked()) {
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
        auto *empty = new QLabel(
            TranslationManager::instance()->get("todo.empty", "No todos yet"), m_rowsContainer);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_rowsLayout->addWidget(empty);
        // 空提示的文字色同样随主题刷新
        m_rowRefreshers.append([empty]() {
            const bool dark = StyleManager::themeMode() == StyleManager::ThemeMode::Dark;
            empty->setStyleSheet(QStringLiteral("color: %1;")
                .arg(dark ? QStringLiteral("#8a8a90") : QStringLiteral("#8a8a8a")));
        });
        m_rowRefreshers.last()();
    } else {
        for (const TodoItem &item : items) {
            m_rowsLayout->addWidget(createRowWidget(item));
        }
    }
    m_rowsLayout->addStretch(1);
}

/**
 * @brief 按当前主题刷新窗口、输入区与所有待办行的配色
 * @author chiangyang
 */
void TodoWindow::updateWindowColors() {
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

    // 输入区控件配色随主题
    if (m_input) {
        m_input->setStyleSheet(StyleManager::getPathEditStyle());
    }
    if (m_priority) {
        m_priority->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    }
    if (m_dueEdit) {
        m_dueEdit->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    }
    if (m_addBtn) {
        m_addBtn->setStyleSheet(StyleManager::getSettingsButtonStyle());
    }

    // 系统标题栏浅色/深色随主题
    StyleManager::applyDarkTitleBar(winId());

    // 各行背景/文字颜色
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
