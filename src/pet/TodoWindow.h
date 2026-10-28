#ifndef TODOWINDOW_H
#define TODOWINDOW_H

#include <QDate>
#include <QVector>
#include <QWidget>
#include <functional>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QComboBox;
class QDateEdit;
class QScrollArea;
class QVBoxLayout;
struct TodoItem;

/**
 * @brief 待办清单窗口
 *
 * 右击桌面宠物打开（独立普通窗口，可拖动、可常驻）。顶部一行输入区
 * （内容、优先级、截止日期、添加按钮），下方为待办列表，每行支持：
 * - 勾选完成（完成后变灰并自动排到未完成项之后）
 * - 直接下拉切换优先级、直接修改截止日期（勾选"截止"后才启用）
 * - 删除（× 按钮）
 * 截止当天以橙色提示、已逾期以红色高亮；数据由 TodoStore 持久化到
 * 应用数据目录下的 todos.json。
 * @author chiangyang
 */
class TodoWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象（顶层窗口传 nullptr）
     * @author chiangyang
     */
    explicit TodoWindow(QWidget *parent = nullptr);

    /**
     * @brief 按当前主题刷新窗口、输入区与所有待办行的配色
     * @author chiangyang
     */
    void updateWindowColors();

    /**
     * @brief 按当前语言刷新窗口标题、占位文字与列表内容
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 从 TodoStore 重新加载并重建待办列表
     * @author chiangyang
     */
    void reloadItems();

protected:
    /**
     * @brief 显示时按最新主题/语言刷新，并重新计算截止日期高亮
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * @brief 构建窗口布局与输入区控件
     * @author chiangyang
     */
    void setupUi();

    /**
     * @brief 读取输入区内容新增一条待办（内容为空时忽略）
     * @author chiangyang
     */
    void addItemFromInput();

    /**
     * @brief 按待办项创建一行列表控件
     * @param item 待办项
     * @return 行控件（父对象为列表容器）
     * @author chiangyang
     */
    QWidget *createRowWidget(const TodoItem &item);

    /**
     * @brief 清空当前列表（含行配色刷新回调）
     * @author chiangyang
     */
    void clearRows();

    /**
     * @brief 用当前语言填充优先级下拉项（清空后重建）
     * @param combo 目标下拉框
     * @author chiangyang
     */
    void fillPriorityCombo(QComboBox *combo);

    /**
     * @brief 待办是否已逾期（未完成且截止日期早于今天）
     * @param done 是否已完成
     * @param due 截止日期（无效表示不设截止）
     * @return 是否逾期
     * @author chiangyang
     */
    static bool isOverdue(bool done, const QDate &due);

    /**
     * @brief 待办是否今天到期（未完成且截止日期为今天）
     * @param done 是否已完成
     * @param due 截止日期（无效表示不设截止）
     * @return 是否今天到期
     * @author chiangyang
     */
    static bool isDueToday(bool done, const QDate &due);

    QLineEdit *m_input = nullptr;        ///< 待办内容输入框
    QComboBox *m_priority = nullptr;     ///< 新增项的优先级选择
    QCheckBox *m_dueCheck = nullptr;     ///< 新增项是否设置截止日期
    QDateEdit *m_dueEdit = nullptr;      ///< 新增项的截止日期选择（未勾选"截止"时禁用）
    QPushButton *m_addBtn = nullptr;     ///< 添加按钮
    QScrollArea *m_scroll = nullptr;     ///< 列表滚动区
    QWidget *m_rowsContainer = nullptr;  ///< 列表容器
    QVBoxLayout *m_rowsLayout = nullptr; ///< 列表布局
    QVector<std::function<void()>> m_rowRefreshers; ///< 各行配色刷新回调（主题切换时重刷）
};

#endif // TODOWINDOW_H
