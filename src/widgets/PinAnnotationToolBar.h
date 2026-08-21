#ifndef PINANNOTATIONTOOLBAR_H
#define PINANNOTATIONTOOLBAR_H

#include "BaseToolBar.h"

/**
 * @brief Pin 窗口标注工具栏
 *
 * 继承 BaseToolBar，为 PinWindow 提供标注工具和操作按钮。
 * 作为独立顶层窗口（Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint）
 * 显示在 PinWindow 下边界贴边位置，移动/缩放 PinWindow 时跟随。
 *
 * 工具栏布局：
 * 一级工具栏：矩形 | 椭圆 | 箭头 | 画笔 | 直线 | 文本 | 马赛克 | 橡皮擦 | 撤销 | 重做 | 清除 | 复制 | 保存 | 取消
 * 子工具栏：形状选择 | 线宽/字体大小 | 颜色选择（继承自 BaseToolBar）
 *
 * 信号：
 * - copyRequested: 请求复制（含标注）
 * - saveRequested: 请求保存（含标注）
 * - cancelRequested: 请求退出标注模式（隐藏工具栏，标注保留）
 * - 以下工具信号继承自 BaseToolBar: toolSelected, annotationToolDeselected, shapeTypeChanged, penWidthChanged, penColorChanged, fontSizeChanged, undoRequested, redoRequested, clearRequested
 * @author chiangyang
 */
class PinAnnotationToolBar : public BaseToolBar {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口（PinWindow 场景下传 nullptr，作为独立顶层窗口）
     * @author chiangyang
     */
    explicit PinAnnotationToolBar(QWidget *parent = nullptr);

    /**
     * @brief 将工具栏定位到 PinWindow 下边界贴边位置
     *
     * 优先放在 PinWindow 正下方（左对齐），下方空间不足时放上方，
     * 右侧溢出左移、左侧溢出右移，确保工具栏完整可见。
     *
     * @param pinWindow 目标 PinWindow
     * @author chiangyang
     */
    void positionNearPinWindow(QWidget *pinWindow);

    /**
     * @brief 重新定位子工具栏（保持相对主工具栏的位置）
     *
     * 主工具栏移动后调用，使子工具栏跟随保持在主工具栏正下方。
     * @author chiangyang
     */
    void repositionSubToolbar();

    /**
     * @brief 重新翻译UI文本
     * @author chiangyang
     */
    void retranslateUi() override;

    /**
     * @brief 更新按钮样式（文字/图标模式切换）
     * @author chiangyang
     */
    void updateButtonStyles() override;

signals:
    /**
     * @brief 请求复制（含标注）
     * @author chiangyang
     */
    void copyRequested();

    /**
     * @brief 请求保存（含标注）
     * @author chiangyang
     */
    void saveRequested();

    /**
     * @brief 请求退出标注模式（隐藏工具栏，标注保留）
     * @author chiangyang
     */
    void cancelRequested();

protected:
    /**
     * @brief 设置UI布局
     * @author chiangyang
     */
    void setupUi() override;

    /**
     * @brief 移动事件处理，同步子工具栏位置
     * @param event 移动事件
     * @author chiangyang
     */
    void moveEvent(QMoveEvent *event) override;

    /**
     * @brief 绘制事件，主动绘制圆角矩形背景
     *
     * 透明顶层窗口下 QSS 的 background-color 不绘制，需用 QPainter
     * 主动绘制圆角矩形背景，圆角外区域保持透明。
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 创建操作按钮（撤销、重做、清除、复制、保存、取消）
     * @author chiangyang
     */
    void createActionButtons();

    QPushButton *m_copyBtn = nullptr;    ///< 复制按钮
    QPushButton *m_saveBtn = nullptr;    ///< 保存按钮
    QPushButton *m_cancelBtn = nullptr;  ///< 取消按钮（退出标注模式）
};

#endif // PINANNOTATIONTOOLBAR_H
