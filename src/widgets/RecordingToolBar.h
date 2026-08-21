#ifndef RECORDINGTOOLBAR_H
#define RECORDINGTOOLBAR_H

#include "BaseToolBar.h"
#include <QColor>
#include <QLabel>
#include <QPoint>
#include <QTimer>
#include <vector>

/**
 * @brief 录屏工具栏
 *
 * 继承 BaseToolBar，提供录屏时的标注工具和操作按钮。
 * 在选区完成后显示，定位在选区附近。
 *
 * 工具栏布局：
 * 一级工具栏：录屏 | 截图 | 矩形 | 椭圆 | 箭头 | 画笔 | 直线 | 文本 | 马赛克 | 橡皮擦 | 撤销 | 重做 | 清除 | OCR | 快照 | 取消
 * 子工具栏：形状选择 | 线宽/字体大小 | 颜色选择
 *
 * 信号：
 * - showControlRequested: 请求显示/隐藏录屏控制栏
 * - timerUpdated: 录制时间更新
 * - 以下操作信号继承自 BaseToolBar: screenshotRequested, cancelRecordRequested, undoRequested, redoRequested, clearRequested, snapshotRequested, ocrRequested
 * - 以下工具信号继承自 BaseToolBar: toolSelected, annotationToolDeselected, shapeTypeChanged, penWidthChanged, penColorChanged, fontSizeChanged
 * @author chiangyang
 */
class RecordingToolBar : public BaseToolBar {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口
     * @author chiangyang
     */
    explicit RecordingToolBar(QWidget *parent = nullptr);

    /**
     * @brief 定位工具栏到选区附近
     * @param selectionRect 选区矩形（全局坐标）
     *
     * 工具栏优先放在选区下方，左对齐选区左边界。如果空间不足则放在选区上方。
     * @author chiangyang
     */
    void positionNearSelection(const QRect &selectionRect);

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

    /**
     * @brief 重置录屏按钮为未选中状态
     * @author chiangyang
     */
    void resetRecordBtn();

    /**
     * @brief 设置截图按钮的启用状态
     * @param enabled true 启用，false 禁用（置灰）
     * @author chiangyang
     */
    void setScreenshotButtonEnabled(bool enabled);

    /**
     * @brief 开始计时
     * @author chiangyang
     */
    void startTimer();

    /**
     * @brief 暂停计时
     * @author chiangyang
     */
    void pauseTimer();

    /**
     * @brief 恢复计时
     * @author chiangyang
     */
    void resumeTimer();

    /**
     * @brief 停止计时并重置
     * @author chiangyang
     */
    void stopTimer();

signals:
    /**
     * @brief 请求显示/隐藏录屏控制栏
     * @param show true=显示, false=隐藏
     * @author chiangyang
     */
    void showControlRequested(bool show);

    /**
     * @brief 录制时间更新
     * @param text 时间文本，如 "00:05"
     * @author chiangyang
     */
    void timerUpdated(const QString &text);

protected:
    /**
     * @brief 设置UI布局
     * @author chiangyang
     */
    void setupUi() override;

private:
    /**
     * @brief 创建操作按钮
     * @author chiangyang
     */
    void createActionButtons();

    // ---------- 按钮指针 ----------
    QPushButton *m_recordBtn = nullptr;      ///< 录屏按钮（checkable）
    QPushButton *m_screenshotBtn = nullptr;  ///< 截图按钮
    QPushButton *m_snapshotBtn = nullptr;    ///< 快照按钮
    QPushButton *m_cancelBtn = nullptr;      ///< 取消按钮

    QTimer *m_timer = nullptr;          ///< 计时器
    int m_elapsedSeconds = 0;           ///< 已录制秒数
};

#endif // RECORDINGTOOLBAR_H
