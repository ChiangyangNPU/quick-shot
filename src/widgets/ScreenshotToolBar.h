#ifndef SCREENSHOTTOOLBAR_H
#define SCREENSHOTTOOLBAR_H

#include "BaseToolBar.h"
#include <QColor>
#include <QPoint>
#include <vector>

/**
 * @brief 截图工具栏
 *
 * 继承 BaseToolBar，提供截图时的标注工具和操作按钮。
 * 在选区完成后显示，定位在选区附近。
 *
 * 工具栏布局：
 * 一级工具栏：录屏 | 矩形 | 椭圆 | 箭头 | 画笔 | 直线 | 文本 | 马赛克 | 橡皮擦 | 撤销 | 重做 | 清除 | OCR | 复制 | 保存 | 贴图 | 关闭
 * 子工具栏：形状选择 | 线宽/字体大小 | 颜色选择
 *
 * 信号：
 * - copyRequested: 复制截图到剪贴板
 * - saveRequested: 保存截图到文件
 * - pinRequested: 贴图截图（贴图窗口）
 * - closeRequested: 关闭截图
 * - 以下信号继承自 BaseToolBar: toolSelected, undoRequested, redoRequested, clearRequested, shapeTypeChanged, penWidthChanged, penColorChanged, fontSizeChanged
 * @author chiangyang
 */
class ScreenshotToolBar : public BaseToolBar {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父窗口
     * @author chiangyang
     */
    explicit ScreenshotToolBar(QWidget *parent = nullptr);

    /**
     * @brief 定位工具栏到选区附近
     * @param selectionRect 选区矩形（全局坐标）
     *
     * 工具栏优先放在选区下方，右对齐选区右边界。如果空间不足则放在选区上方。
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

signals:
    /**
     * @brief 请求复制截图到剪贴板
     * @author chiangyang
     */
    void copyRequested();

    /**
     * @brief 请求保存截图到文件
     * @author chiangyang
     */
    void saveRequested();

    /**
     * @brief 请求贴图截图（贴图窗口）
     * @author chiangyang
     */
    void pinRequested();

    /**
     * @brief 请求关闭截图
     * @author chiangyang
     */
    void closeRequested();

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
    QPushButton *m_recordBtn = nullptr;  ///< 录屏按钮
    QPushButton *m_copyBtn = nullptr;    ///< 复制按钮
    QPushButton *m_saveBtn = nullptr;    ///< 保存按钮
    QPushButton *m_pinBtn = nullptr;     ///< 贴图按钮
    QPushButton *m_closeBtn = nullptr;   ///< 关闭按钮
};

#endif // SCREENSHOTTOOLBAR_H
