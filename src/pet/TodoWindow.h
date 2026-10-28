#ifndef TODOWINDOW_H
#define TODOWINDOW_H

#include <QDate>
#include <QVector>
#include <QWidget>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QDateEdit;
class QScrollArea;
class QVBoxLayout;
struct TodoItem;

/**
 * @brief 待办清单气泡窗口
 *
 * 右击桌面宠物打开，以无边框气泡的形式悬浮在宠物上方（空间不足时显示在下方），
 * 气泡带指向宠物的小三角，可按住空白处拖动，Esc 收起。
 * 顶部为标题与统计，其下是极简输入区（内容、优先级、可选的截止日期、添加按钮）
 * 与一条细分隔线，再下方为单行式任务列表，每行支持：
 * - 勾选完成（完成后文字置灰、下沉排序，行结构与行高保持不变）
 * - 行内下拉切换优先级、点击日期修改截止时间（右键清除）
 * - 删除（× 按钮）
 * 截止当天以橙色、已逾期以红色显示日期文字；数据由 TodoStore 持久化到
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
     * @brief 设置气泡相对宠物的朝向
     * @param arrowBelow true 表示气泡在宠物上方、小三角朝下；false 表示在下方、朝上
     * @param anchorX 小三角在窗口内的水平位置（宠物中心）
     * @author chiangyang
     */
    void setBubblePlacement(bool arrowBelow, int anchorX);

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

signals:
    /**
     * @brief 气泡高度随内容变化时发出（用于重新贴合宠物定位）
     * @author chiangyang
     */
    void bubbleSizeChanged();

protected:
    /**
     * @brief 绘制气泡背景：圆角面板、柔和投影与指向宠物的小三角
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 显示时按最新主题/语言刷新，并重新计算截止日期高亮
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

    /**
     * @brief 记录拖动起点（无标题栏，按住气泡空白处移动）
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 拖动气泡窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief Esc 收起气泡
     * @param event 按键事件
     * @author chiangyang
     */
    void keyPressEvent(QKeyEvent *event) override;

private:
    /**
     * @brief 按小三角所在侧重新计算内容边距（给小三角和投影留出空间）
     * @author chiangyang
     */
    void updateContentMargins();

    /**
     * @brief 按任务数量调整气泡高度（内容少时贴合，超过上限时列表滚动）
     * @author chiangyang
     */
    void updateBubbleHeight();

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
     * @brief 刷新标题下方"未完成 / 已完成"统计文字
     * @author chiangyang
     */
    void updateHeaderStats();

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

    QLabel *m_headerTitle = nullptr;     ///< 窗口内标题
    QLabel *m_statsLabel = nullptr;      ///< 标题右侧未完成/已完成统计
    QLineEdit *m_input = nullptr;        ///< 待办内容输入框
    QComboBox *m_priority = nullptr;     ///< 新增项的优先级选择
    QPushButton *m_dueCheck = nullptr;   ///< "截止"开关（选中后才显示日期控件）
    QDateEdit *m_dueEdit = nullptr;      ///< 新增项的截止日期选择（未开启"截止"时隐藏）
    QPushButton *m_addBtn = nullptr;     ///< 添加按钮
    QWidget *m_separator = nullptr;      ///< 输入区与列表之间的细分隔线
    QScrollArea *m_scroll = nullptr;     ///< 列表滚动区
    QWidget *m_rowsContainer = nullptr;  ///< 列表容器
    QVBoxLayout *m_rowsLayout = nullptr; ///< 列表布局
    QVBoxLayout *m_rootLayout = nullptr; ///< 根布局（气泡朝向变化时调整边距）
    QVector<std::function<void()>> m_rowRefreshers; ///< 各行配色刷新回调（主题切换时重刷）

    int m_shadowPad = 10;    ///< 投影留白（逻辑像素，构造时按 DPI 缩放）
    int m_arrowHeight = 10;  ///< 小三角高度（逻辑像素，构造时按 DPI 缩放）
    int m_contentPad = 16;   ///< 气泡内容内边距（逻辑像素，构造时按 DPI 缩放）
    bool m_arrowBelow = true; ///< 小三角是否朝下（气泡位于宠物上方）
    int m_anchorX = -1;       ///< 小三角在窗口内的水平位置
    QPoint m_dragOffset;      ///< 拖动时鼠标相对窗口左上角的偏移
};

#endif // TODOWINDOW_H
