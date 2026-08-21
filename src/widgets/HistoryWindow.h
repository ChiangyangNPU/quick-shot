#ifndef HISTORYWINDOW_H
#define HISTORYWINDOW_H

#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QTimer>
#include "../history/HistoryItem.h"

/**
 * @brief 历史记录查看窗口类
 *
 * 提供历史记录的浏览、搜索、筛选和操作功能。
 * 支持截图预览、文本查看、复制、保存、删除等操作。
 * @author chiangyang
 */
class HistoryWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit HistoryWindow(QWidget *parent = nullptr);

    /**
     * @brief 刷新历史记录列表
     * @author chiangyang
     */
    void refreshItems();

    /**
     * @brief 重新翻译 UI 文本
     *
     * 在语言切换后更新窗口中所有界面文本。
     * @author chiangyang
     */
    void retranslateUi();

    /**
     * @brief 更新窗口及各容器背景色
     *
     * 从 StyleManager 读取最新的选项卡背景色，同步刷新窗口及顶部工具栏、
     * 列表区、底部操作栏容器的背景，保证与 SettingsWindow 视觉一致。
     * 由 SettingsWindow::tabWidgetBgColorChanged 信号触发。
     * @author chiangyang
     */
    void updateWindowColors();

protected:
    /**
     * @brief 窗口显示事件
     * @param event 事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

private slots:
    /**
     * @brief 选项卡切换槽函数
     * @param index 新的选项卡索引
     * @author chiangyang
     */
    void onTabChanged(int index);

    /**
     * @brief 搜索按钮点击槽函数
     * @author chiangyang
     */
    void onSearchClicked();

    /**
     * @brief 搜索框回车槽函数
     * @author chiangyang
     */
    void onSearchReturnPressed();

    /**
     * @brief 列表项双击槽函数
     * @param item 被双击的列表项
     * @author chiangyang
     */
    void onItemDoubleClicked(QListWidgetItem *item);

    /**
     * @brief 列表选择变化槽函数
     *
     * 选中项变化时（包括点击、框选、Ctrl/Shift+点击）更新按钮状态。
     * @author chiangyang
     */
    void onItemSelectionChanged();

    /**
     * @brief 复制文本槽函数
     * @author chiangyang
     */
    void onCopyText();

    /**
     * @brief 保存截图槽函数
     * @author chiangyang
     */
    void onSaveScreenshot();

    /**
     * @brief 删除记录槽函数（支持单条和批量删除）
     * @author chiangyang
     */
    void onDeleteItem();

    /**
     * @brief 清空历史槽函数
     * @author chiangyang
     */
    void onClearAll();

    /**
     * @brief 加载更多数据槽函数（分页/滚动加载）
     * @author chiangyang
     */
    void onLoadMore();

    /**
     * @brief 右键菜单槽函数
     * @param pos 右键位置
     * @author chiangyang
     */
    void onCustomContextMenu(const QPoint &pos);

    /**
     * @brief 屏幕 DPI 变化槽函数
     *
     * 系统缩放比例调整时触发，重新应用全局 qss 并重新计算控件尺寸。
     * @author chiangyang
     */
    void onDpiChanged();

private:
    /**
     * @brief 初始化 UI
     * @author chiangyang
     */
    void setupUi();

    /**
     * @brief 设置工具栏
     * @author chiangyang
     */
    void setupToolBar();

    /**
     * @brief 设置列表控件
     * @author chiangyang
     */
    void setupListWidget();

    /**
     * @brief 加载历史记录列表
     * @param type 记录类型筛选
     * @param reset 是否重置分页
     * @author chiangyang
     */
    void loadItems(HistoryType type, bool reset = false);

    /**
     * @brief 更新记录统计信息
     * @author chiangyang
     */
    void updateItemCount();

    /**
     * @brief 获取选中记录的 ID
     * @return 第一个选中记录的 ID，未选中返回 -1
     * @author chiangyang
     */
    qint64 getSelectedItemId() const;

    /**
     * @brief 获取所有选中记录的 ID 列表
     * @return 选中记录的 ID 列表，未选中返回空列表
     * @author chiangyang
     */
    QList<qint64> getSelectedItemIds() const;

    /**
     * @brief 获取当前选项卡对应的记录类型
     * @return 记录类型
     * @author chiangyang
     */
    HistoryType currentType() const;

    /**
     * @brief 创建截图列表项
     * @param item 历史记录项
     * @return 列表项
     * @author chiangyang
     */
    QListWidgetItem* createScreenshotItem(const HistoryItem &item);

    /**
     * @brief 创建文本列表项
     * @param item 历史记录项
     * @return 列表项
     * @author chiangyang
     */
    QListWidgetItem* createTextItem(const HistoryItem &item);

    /**
     * @brief 更新按钮状态
     * @author chiangyang
     */
    void updateButtonStates();

    /**
     * @brief 重新计算并应用控件尺寸
     *
     * DPI 变化时调用。基于当前字体（随 DPI 缩放）重新计算所有控件高度
     * （搜索框、按钮、下拉框等）与列表图标大小，确保 DPI 变化后控件
     * 尺寸与字体协调，避免内容截断或图标偏小。
     * @author chiangyang
     */
    void updateControlSizes();

    /**
     * @brief 根据当前屏幕逻辑 DPI 计算窗口初始尺寸
     *
     * 96 DPI（100% 缩放）为基准，对应 1000×720；按比例放大。
     * 用于构造时设置初始尺寸，以及 DPI 变化时更新窗口尺寸。
     * 注意：历史记录窗口允许用户拖拽改变大小，此处只设初始/建议尺寸，
     * 不像 SettingsWindow 那样锁定。
     * @return 基于 DPI 缩放后的窗口尺寸
     * @author chiangyang
     */
    QSize calculateDpiScaledSize() const;

    QWidget *m_toolbarContainer;   ///< 顶部工具栏容器（搜索/筛选）
    QWidget *m_listContainer;     ///< 列表区容器（列表 + 加载更多按钮）
    QWidget *m_bottomContainer;   ///< 底部操作栏容器（复制/保存/删除/清空）

    QTabWidget *m_tabWidget;      ///< 选项卡控件（全部/截图/文本）
    QLineEdit *m_searchEdit;      ///< 搜索输入框
    QLabel *m_searchLabel;        ///< 搜索标签
    QPushButton *m_searchBtn;     ///< 搜索按钮
    QLabel *m_filterLabel;        ///< 筛选标签
    QComboBox *m_filterCombo;     ///< 时间筛选下拉框
    QListWidget *m_listWidget;    ///< 历史记录列表
    QPushButton *m_copyBtn;       ///< 复制按钮
    QPushButton *m_saveBtn;       ///< 保存按钮
    QPushButton *m_deleteBtn;     ///< 删除按钮
    QPushButton *m_clearBtn;      ///< 清空历史按钮
    QPushButton *m_loadMoreBtn;   ///< 加载更多按钮
    QLabel *m_countLabel;         ///< 记录统计标签

    int m_currentPage;            ///< 当前页码
    int m_pageSize;               ///< 每页数量
    bool m_isLoading;             ///< 是否正在加载（防止重复触发）
    QString m_currentSearch;      ///< 当前搜索关键词
    QTimer *m_refreshTimer;       ///< 延迟刷新定时器（避免频繁刷新）
};

#endif // HISTORYWINDOW_H
