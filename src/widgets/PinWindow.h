#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QMenu>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QSet>
#include "../capture/AnnotationInteractionHandler.h"
#include "../shortcut/IShortcutHandler.h"
#include <memory>

// 前置声明，减少头文件耦合
class PinAnnotationToolBar;
class OverlayTextEdit;
class AnnotationShortcutController;

/**
 * @brief 置顶窗口类
 *
 * 用于将截图固定在屏幕上，支持拖拽移动和调整大小，双击关闭。
 * 右键菜单可在 pin 图上进入标注模式，使用独立标注工具栏进行
 * 矩形/椭圆/箭头/画笔/直线/文本/马赛克/橡皮擦等标注，并支持拖动已画痕迹。
 *
 * 标注坐标策略：统一使用窗口本地坐标 event->pos()（区别于 SnipScreen 的
 * 虚拟桌面全局坐标），paintEvent 绘制标注时无需 translate。
 * @author chiangyang
 */
class PinWindow : public QWidget, public IShortcutHandler {
    Q_OBJECT

public:
    /**
 * @brief 构造函数（不设几何尺寸；须在 setScreen 之后由调用方 setGeometry，否则多屏 DPI 会错）
 * @param pixmap 要显示的截图（建议物理像素、dpr=1）
 * @param parent 父对象
 * @author chiangyang
 */
    explicit PinWindow(const QPixmap &pixmap, QWidget *parent = nullptr);

    /**
     * @brief 切换所有 PinWindow 的显隐
     *
     * 若任一 PinWindow 可见则全部隐藏，否则恢复显示曾被本方法隐藏的窗口。
     * 供全局热键"隐藏/显示所有贴图"调用。
     * @author chiangyang
     */
    static void toggleAll();

    /**
     * @brief 获取当前存活的 PinWindow 数量
     * @return 窗口数量
     * @author chiangyang
     */
    static int instanceCount();

protected:
    /**
     * @brief 绘制事件处理
     *
     * 绘制拉伸原图、标注（含马赛克离屏合成）、边框和调整大小手柄。
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 鼠标按下事件处理
     *
     * 标注模式且选中工具时走标注逻辑（橡皮擦/马赛克/文本/命中拖动/创建标注），
     * 否则走现有拖动移动/调整大小逻辑。
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件处理
     *
     * 标注模式且选中工具时走标注拖动/擦除/马赛克/绘制/悬停光标逻辑，
     * 否则走现有拖动/调整大小逻辑。
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件处理
     *
     * 标注模式结束拖动/绘制/擦除/马赛克，否则结束拖动/调整大小。
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标双击事件处理
     *
     * 标注模式下禁用双击关闭（直接返回），否则延迟关闭窗口。
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标滚轮事件处理（缩放窗口，标注模式不变）
     * @param event 鼠标滚轮事件
     * @author chiangyang
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @brief 键盘按键事件处理
     *
     * 处理 PinWindow 的全部快捷键（应用内固定约定，不进设置）：
     * - ESC：标注模式退出标注模式（窗口不关），否则关闭窗口。
     * - Ctrl+C / Ctrl+S：复制 / 保存（含标注）。
     * - 标注模式：Ctrl+Z 撤销，Ctrl+Shift+Z / Ctrl+Y 重做，数字键 1-8 切换标注工具。
     * - 非标注模式：方向键移动窗口（Ctrl+方向键 10px，普通方向键 1px）。
     * @param event 键盘事件
     * @author chiangyang
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief 右键菜单事件处理
     *
     * 菜单项顺序：复制 → 标注 → OCR → 翻译 → 保存。
     * @param event 右键菜单事件
     * @author chiangyang
     */
    void contextMenuEvent(QContextMenuEvent *event) override;

    /**
     * @brief 移动事件处理，标注工具栏跟随
     * @param event 移动事件
     * @author chiangyang
     */
    void moveEvent(QMoveEvent *event) override;

    /**
     * @brief 调整大小事件处理，标注工具栏跟随
     * @param event 调整大小事件
     * @author chiangyang
     */
    void resizeEvent(QResizeEvent *event) override;

    /**
     * @brief 关闭事件处理，清理标注工具栏与文本编辑框
     * @param event 关闭事件
     * @author chiangyang
     */
    void closeEvent(QCloseEvent *event) override;

private:
    QPixmap m_pixmap;          ///< 要显示的截图
    QPoint m_dragPosition;      ///< 拖拽起始位置
    bool m_isMoving;            ///< 是否正在移动
    bool m_isResizing;          ///< 是否正在调整大小
    QLabel *m_ocrLoadingLabel; ///< OCR 识别中的加载提示标签

    // ---------- 标注相关 ----------
    PinAnnotationToolBar *m_toolBar = nullptr;   ///< 标注工具栏（独立顶层窗口）
    OverlayTextEdit *m_textEdit = nullptr;       ///< 当前活动的文本编辑框（仅 Text 工具使用）

    bool m_annotationMode = false;               ///< 是否处于标注模式（工具栏可见）
    bool m_annotationToolSelected = false;       ///< 是否选中了某标注工具
    bool m_hiddenByToggle = false;               ///< 是否被 toggleAll 隐藏（避免 showAll 误显已关闭窗口）

    static QSet<PinWindow*> s_instances;         ///< 存活的 PinWindow 实例注册表

    // ---------- 标注交互处理器（提取公共逻辑，通过 Host 回调注入差异）----------
    std::unique_ptr<AnnotationInteractionHandler> m_annotationHandler;

    /**
     * @brief 检查鼠标是否在调整大小区域内
     * @param pos 鼠标位置
     * @return 是否在调整大小区域内
     * @author chiangyang
     */
    bool isInResizeArea(const QPoint &pos);

    /**
     * @brief 复制图片到剪贴板（含标注）
     * @author chiangyang
     */
    void copyToClipboard();

    /**
     * @brief 保存图片到文件（含标注）
     * @author chiangyang
     */
    void saveToFile();

    /**
     * @brief 执行翻译流程：OCR 识别 → 批量翻译 → 显示译文叠加窗口
     *
     * 对当前 pin 的图片进行翻译，翻译结果在原文本位置叠加显示。
     * @author chiangyang
     */
    void performTranslate();

    /**
     * @brief 进入标注模式
     *
     * 懒创建标注工具栏并显示在 PinWindow 下边界贴边位置，连接工具栏信号。
     * @author chiangyang
     */
    void enterAnnotationMode();

    /**
     * @brief 退出标注模式
     *
     * 隐藏标注工具栏与子工具栏，取消所有工具按钮选中状态。
     * 标注痕迹保留，paintEvent 继续绘制；复制/保存仍含标注。
     * @author chiangyang
     */
    void exitAnnotationMode();

    /**
     * @brief 连接标注工具栏信号（仅在工具栏首次创建时调用一次）
     * @author chiangyang
     */
    void connectToolBarSignals();

    /**
     * @brief 创建文本编辑框
     *
     * 在指定位置（窗口本地坐标）创建 OverlayTextEdit 用于输入文本标注。
     * @param pos 文本框左上角位置（窗口本地坐标）
     * @author chiangyang
     */
    void createTextEdit(const QPoint &pos);

    /**
     * @brief 完成文本编辑，将文本内容创建为标注
     *
     * 读取 OverlayTextEdit 的内容，创建 TextAnnotation 并清理编辑框。
     * 与 SnipScreen 版本的差异：widgetPos 直接使用 m_textEdit->pos()（已是窗口本地坐标，
     * 无需 + m_virtualGeometry.topLeft()）。若文本为空则不创建标注。
     * @author chiangyang
     */
    void finalizeTextEdit();

    /**
     * @brief 合成"所见即所得"图（窗口尺寸画拉伸原图 + 标注 + 马赛克）
     *
     * 无标注时返回原图（最高质量）；有标注时返回窗口尺寸的合成图，
     * 保证复制/保存结果与屏幕显示一致。
     * @return 合成图
     * @author chiangyang
     */
    QPixmap compositePixmap();

    /**
     * @brief 更新标注工具栏按钮状态（撤销/重做/清除启用状态）
     * @author chiangyang
     */
    void updateToolBarState();

    // ========== IShortcutHandler 接口实现（标注快捷键策略）==========
    /**
     * @brief 判断当前是否可进行标注操作
     * @return 处于标注模式时返回 true
     * @author chiangyang
     */
    bool canAnnotate() const override;

    /**
     * @brief 切换标注工具（数字键 1-8 触发）
     * @param toolId 工具 ID（0-7，对应 AnnotationType 枚举值）
     * @author chiangyang
     */
    void onToolSwitch(int toolId) override;

    /**
     * @brief 复制到剪贴板（Ctrl+C 触发）
     * @author chiangyang
     */
    void onCopy() override;

    /**
     * @brief 保存到文件（Ctrl+S 触发）
     * @author chiangyang
     */
    void onSave() override;

    /**
     * @brief 撤销标注（Ctrl+Z 触发）
     * @author chiangyang
     */
    void onUndo() override;

    /**
     * @brief 重做标注（Ctrl+Y / Ctrl+Shift+Z 触发）
     * @author chiangyang
     */
    void onRedo() override;

    /**
     * @brief 清除所有标注（Delete / Backspace 触发）
     * @author chiangyang
     */
    void onClear() override;

    /**
     * @brief 调整画笔宽度（[ / ] 触发，范围 1-20）
     * @param delta 宽度增量（+1 增加，-1 减少）
     * @author chiangyang
     */
    void onPenWidthChange(int delta) override;

    /**
     * @brief 循环切换颜色（Tab 触发）
     * @author chiangyang
     */
    void onCycleColor() override;

    /**
     * @brief 取消当前操作（Esc 触发）
     *
     * 优先级：取消文本编辑 → 退出标注模式 → 关闭窗口
     * @author chiangyang
     */
    void onCancel() override;

private:
    AnnotationShortcutController *m_annotationController = nullptr;  ///< 标注快捷键控制器（标注模式期间创建，退出时销毁）
};

#endif // PINWINDOW_H
