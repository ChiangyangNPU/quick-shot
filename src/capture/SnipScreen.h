#ifndef SNIPSCREEN_H
#define SNIPSCREEN_H

#include "Selector.h"
#include "../core/Hunter.h"
#include "ScreenshotToolBar.h"
#include "RecordingToolBar.h"
#include "../widgets/RecordingControlWindow.h"
#include "ScreenRecorder.h"
#include "AnnotationInteractionHandler.h"
#include "OverlayTextEdit.h"
#include "../shortcut/IShortcutHandler.h"
#include <QPixmap>
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <memory>

class PinWindow;
class SettingsWindow;
class AnnotationShortcutController;

/**
 * @brief 截图主界面
 *
 * 覆盖整个虚拟桌面的全屏遮罩窗口，协调选区、截图、导出等功能。
 * 使用"虚拟桌面全局坐标系"：窗口 geometry = 虚拟桌面并集，
 * 截图和选区都在同一坐标系中，无需坐标转换。
 *
 * 核心流程：
 * 1. start() → 设置窗口覆盖所有显示器
 * 2. grabVirtualDesktop() → 一次抓取整个虚拟桌面截图
 * 3. selector_->start() → 开始选区交互
 * 4. 用户完成选区 → 显示工具栏
 * 5. 用户可进行标注或直接导出（copy/save/pin）
 *
 * 标注系统：
 * - 选区完成后进入标注模式
 * - 工具栏选择标注工具后，鼠标事件用于绘制标注
 * - 标注绘制在选区范围内
 * - 支持撤销/重做/清除
 * @author chiangyang
 */
class SnipScreen final : public QWidget, public IShortcutHandler {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     *
     * 初始化截图主界面，创建选区组件、工具栏、录屏控制窗口等子组件，
     * 注册全局快捷键，连接信号槽，并设置窗口为无边框覆盖整个虚拟桌面。
     *
     * @param parent 父窗口指针
     * @author chiangyang
     */
    explicit SnipScreen(QWidget *parent = nullptr);

signals:
    /**
     * @brief 截图已完成（用于通知外部，如贴图窗口）
     * @param pixmap 截取的图片
     * @param pos 截取区域的全局左上角坐标
     * @author chiangyang
     */
    void snipped(const QPixmap &pixmap, const QPoint &pos);

    /**
     * @brief 截图已保存到文件
     * @param path 保存的文件路径
     * @author chiangyang
     */
    void saved(const QString &path);

public slots:
    /**
     * @brief 开始截图
     *
     * 覆盖整个虚拟桌面，抓取屏幕截图，启动选区交互。
     * 如果已经在截图中则忽略。
     * @author chiangyang
     */
    void start();

    /**
     * @brief 开始录屏模式
     *
     * 覆盖整个虚拟桌面，抓取屏幕截图，启动选区交互。
     * 使用绿色边框和录屏工具栏。
     * @author chiangyang
     */
    void startRecording();

    /**
     * @brief 退出截图
     *
     * 关闭选区、隐藏界面、清理状态。
     * @author chiangyang
     */
    void exit();

    /**
     * @brief 复制选区到剪贴板并退出
     * @author chiangyang
     */
    void copy();

    /**
     * @brief 保存选区到文件
     * @author chiangyang
     */
    void save();

    /**
     * @brief 贴图截图（创建贴图窗口）
     *
     * 将选区截图显示在屏幕上的置顶窗口中。
     * 复用 PinWindow 组件实现。
     * @author chiangyang
     */
    void pin();

    /**
     * @brief 贴图历史截图（全局热键入口）
     *
     * 从 HistoryManager 的截图记录中按时间倒序依次贴图：
     * - 首次按下（无活跃翻页贴图）：贴最新一张截图，位置在鼠标所在屏幕中央
     * - 后续按下（有活跃翻页贴图）：贴上一张截图，位置在上一张贴图位置偏移 (24,24)
     * - 循环翻页：到最早一张后回到最新
     * - 旧贴图保留不关闭，可多张同时存在
     * 无截图历史时记录日志并返回。
     * @author chiangyang
     */
    void pinClipboard();

    /**
     * @brief 全屏截图并复制到剪贴板（全局热键入口）
     *
     * 抓取整个虚拟桌面（多屏并集）直接复制到剪贴板，不进入选区交互。
     * @author chiangyang
     */
    void grabFullscreen();

    /**
     * @brief 活动窗口截图并复制到剪贴板（全局热键入口）
     *
     * 抓取前台活动窗口直接复制到剪贴板。
     * @author chiangyang
     */
    void grabActiveWindow();

    /**
     * @brief 切换录屏暂停/恢复（全局热键入口）
     *
     * 仅在录制中生效，非录制状态为 no-op。
     * @author chiangyang
     */
    void togglePauseRecording();

    /**
     * @brief 停止录屏（全局热键入口）
     *
     * 仅在录制中生效，停止录制并保留视频文件（区别于取消录制）。
     * 非录制状态为 no-op。
     * @author chiangyang
     */
    void stopRecording();

    /**
     * @brief 根据配置刷新标注工具默认值
     *
     * 当 SettingsWindow 中默认画笔粗细或字号变更时调用，
     * 更新截图工具栏和录屏工具栏的默认值设置。
     * @author chiangyang
     */
    void refreshAnnotationToolDefaults();

    /**
     * @brief 连接工具栏信号到设置窗口
     *
     * 当 SettingsWindow 创建后调用，建立工具栏到设置窗口的双向同步。
     * @param settingsWindow 设置窗口指针
     * @author chiangyang
     */
    void connectToolBarToSettingsWindow(SettingsWindow *settingsWindow);

    /**
     * @brief 获取当前选区截图
     *
     * 裁剪背景图中选区对应的区域。
     * 坐标系：选区使用全局坐标，背景图也覆盖整个虚拟桌面，
     * 所以直接 copy(rect) 即可，无需坐标转换。
     *
     * @return {截图像素图, 选区左上角全局坐标}
     * @author chiangyang
     */
    std::pair<QPixmap, QPoint> snip();

    /**
     * @brief 显示截图工具栏
     *
     * 在选区附近显示工具栏，提供标注和操作功能。
     * @author chiangyang
     */
    void showToolBar();

    /**
     * @brief 隐藏截图工具栏
     * @author chiangyang
     */
    void hideToolBar();

    /**
     * @brief 撤销标注
     * @author chiangyang
     */
    void undoAnnotation();

    /**
     * @brief 重做标注
     * @author chiangyang
     */
    void redoAnnotation();

    /**
     * @brief 清除所有标注
     * @author chiangyang
     */
    void clearAnnotations();

protected:
    /**
     * @brief 绘制背景（截图）、标注和半透明遮罩
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 事件过滤器，拦截发送给 Selector 的鼠标事件
     *
     * 当处于标注模式时，拦截鼠标事件用于绘制标注，
     * 而不是让 Selector 处理选区移动/调整。
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief 鼠标按下事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 滚轮事件，转发给 Selector
     * @author chiangyang
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @brief 键盘按下事件，处理全局快捷键
     * @author chiangyang
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief 键盘释放事件，处理十字准线切换
     * @author chiangyang
     */
    void keyReleaseEvent(QKeyEvent *event) override;

    /**
     * @brief 双击事件：快速复制选区
     * @author chiangyang
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    /**
     * @brief 抓取整个虚拟桌面的截图
     *
     * 使用 Win32 API 抓取所有显示器的并集。
     * 结果存储在 m_background 中。
     * @author chiangyang
     */
    void grabVirtualDesktop();

    // ========== IShortcutHandler 接口实现（标注快捷键策略）==========
    /**
     * @brief 判断当前是否可进行标注操作
     * @return 选区状态为 Captured 或 Locked 时返回 true
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
     * @brief 切换截图/录屏模式（`·` 键触发）
     *
     * 截图模式→录屏模式；录屏模式且未录制→截图模式；
     * 录屏正在录制时禁止切换并提示先停止录制。
     * @author chiangyang
     */
    void onSwitchMode() override;

    /**
     * @brief 复制到剪贴板（Ctrl+C 触发，复制后退出截图模式）
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
     * 优先级：取消文本编辑 → 取消录制 → 退出截图
     * @author chiangyang
     */
    void onCancel() override;

    /**
     * @brief 刷新截图（F5 触发，重新抓取屏幕保留选区）
     * @author chiangyang
     */
    void onRefresh() override;

    /**
     * @brief 开始选区（内部方法）
     * @param recording true=录屏模式, false=截图模式
     * @author chiangyang
     */
    void startCapture(bool recording);

    /**
     * @brief 切换到录屏模式
     * @author chiangyang
     */
    void switchToRecordingMode();

    /**
     * @brief 切换到截图模式
     * @author chiangyang
     */
    void switchToScreenshotMode();

    /**
     * @brief 更新 Selector 边框颜色
     * @author chiangyang
     */
    void updateBorderColor();

    /**
     * @brief 显示录屏工具栏
     * @author chiangyang
     */
    void showRecordingToolBar();

    /**
     * @brief 隐藏录屏工具栏
     * @author chiangyang
     */
    void hideRecordingToolBar();

    /**
     * @brief 更新当前活动工具栏的状态
     * @author chiangyang
     */
    void updateToolBarState(bool canUndo, bool canRedo);

    /**
     * @brief 更新工具栏和子工具栏位置
     * @author chiangyang
     */
    void updateMenuPosition();

    /**
     * @brief 计算工具栏位置的辅助方法
     *
     * 根据选区位置和屏幕空间，计算一级工具栏、子工具栏和录屏控制栏的位置。
     * 顺序始终保持：一级工具栏 → 子工具栏 → 录屏控制栏。
     *
     * @param toolbar 一级工具栏
     * @param subToolbar 子工具栏
     * @param recordingControl 录屏控制栏（可为nullptr）
     * @param selection 选区矩形
     * @param screen 屏幕对象
     * @param alignRight 是否右对齐（截屏模式为true，录屏模式为false）
     * @author chiangyang
     */
    void calculateToolbarPositions(QWidget *toolbar, QWidget *subToolbar,
                                   QWidget *recordingControl, const QRect &selection,
                                   bool alignRight);

    /**
     * @brief 截取选区画面用于 OCR（不关闭 selector）
     * @author chiangyang
     */
    QPixmap captureSelectionForOcr();

    /**
     * @brief 对图片进行 OCR 识别并显示结果
     * @param pixmap 要识别的图片
     * @author chiangyang
     */
    void performOcr(const QPixmap &pixmap);

    /**
     * @brief 执行翻译流程：截图选区 → OCR 识别 → 批量翻译 → 显示译文叠加窗口
     *
     * 二期翻译入口，由截图/录屏工具栏的"翻译"按钮触发。
     * 流程：
     * 1. 截取当前选区画面（含标注）
     * 2. 首次使用时弹出隐私提示
     * 3. 异步 OCR 识别，获取文本与位置多边形
     * 4. 调用 TranslateService 批量逐段翻译
     * 5. 翻译完成后创建 TranslateOverlayWindow 在选区位置叠加显示
     * @author chiangyang
     */
    void performTranslate();

    /**
     * @brief 检查鼠标是否在工具栏区域
     * @param globalPos 全局坐标
     * @return true 如果鼠标在工具栏上
     * @author chiangyang
     */
    bool isMouseInToolBar(const QPoint &globalPos) const;

    /**
     * @brief 检查鼠标是否在选区内
     * @param globalPos 全局坐标
     * @return true 如果鼠标在选区内
     * @author chiangyang
     */
    bool isMouseInSelection(const QPoint &globalPos) const;

    /**
     * @brief 将坐标限制在选区边界内
     * @param pos 全局坐标
     * @return 限制后的坐标
     * @author chiangyang
     */
    QPoint clampToSelection(const QPoint &pos) const;

    /**
     * @brief 完成文本编辑，将文本内容创建为标注
     *
     * 读取 OverlayTextEdit 的内容，创建 TextAnnotation 并清理编辑框。
     * 如果文本为空则不创建标注。
     * @author chiangyang
     */
    void finalizeTextEdit();

    /**
     * @brief 渲染标注叠加图像（输出分辨率）
     *
     * 将标注管理器中的标注渲染到 outputSize 分辨率的透明图像上。
     * 坐标映射：全局坐标 → 相对于 captureRect → 缩放到 outputSize。
     *
     * @return 标注叠加图像（ARGB32格式），无标注时返回空图像
     * @author chiangyang
     */
    QImage renderAnnotationOverlay() const;

    /**
     * @brief 推送标注叠加到录屏器
     *
     * 仅在录制中时有效。调用 renderAnnotationOverlay() 并将结果传给 recorder。
     * @author chiangyang
     */
    void pushAnnotationOverlay();

#ifdef Q_OS_MACOS
    /**
     * @brief 检测鼠标是否跨屏，若跨屏则将窗口切换到鼠标所在屏
     *
     * macOS 单屏窗口模式专用：通过 QTimer 轮询鼠标位置，
     * 当鼠标移到另一屏且处于 PreySelecting 状态时，
     * 移动窗口到新屏、重新抓取背景、更新选区。
     * @author chiangyang
     */
    void checkScreenSwitch();
#endif

    /**
     * @brief 更新输入遮罩，使非交互区域鼠标事件穿透到桌面
     *
     * 录屏中未选中标注工具时，只保留工具栏、控制栏和选区边框接收事件，
     * 其余区域 setMask 裁剪后自动穿透。
     * @author chiangyang
     */
    void updateInputMask();

    /**
     * @brief 将全局坐标转换为相对于选区的坐标
     * @param globalPos 全局坐标
     * @return 选区内坐标
     * @author chiangyang
     */
    QPoint mapToSelection(const QPoint &globalPos) const;

    /**
     * @brief 将坐标转换为背景图坐标
     * @param globalPos 全局坐标
     * @return 背景图坐标
     * @author chiangyang
     */
    QPoint mapToBackground(const QPoint &globalPos) const;

    // ---------- 核心组件 ----------
    Hunter m_hunter;                           ///< 猎物检测器（Hunter 实现 IPreyDetector）
    Selector *m_selector = nullptr;           ///< 选区组件
    ScreenshotToolBar *m_toolbar = nullptr;   ///< 截图工具栏
    RecordingToolBar *m_recordingToolbar = nullptr;   ///< 录屏工具栏
    RecordingControlWindow *m_recordingControl = nullptr; ///< 录屏控制栏
    ScreenRecorder *m_screenRecorder = nullptr;       ///< 录屏器

    // ---------- 标注交互处理器（提取公共逻辑，通过 Host 回调注入差异）----------
    std::unique_ptr<AnnotationInteractionHandler> m_annotationHandler;

    // ---------- 截图数据 ----------
    QPixmap m_background;              ///< 整个虚拟桌面的截图
    QPixmap m_annotatedBackground;     ///< 带标注的背景图（用于最终导出）
    QRect m_virtualGeometry;           ///< 虚拟桌面几何（物理像素坐标）

    // ---------- 标注状态（SnipScreen 专有，未提取到 Handler）----------
    bool m_annotationToolSelected = false;  ///< 是否选中了标注工具（非移动/调整模式）
    bool m_isRecordingMode = false;    ///< 是否为录屏模式
    bool m_isRecordingCanceled = false; ///< 录制是否被取消（取消时不退出截图）
    OverlayTextEdit *m_textEdit = nullptr;  ///< 当前活动的文本编辑框（仅 Text 工具使用）
    AnnotationShortcutController *m_annotationController = nullptr;  ///< 标注快捷键控制器（策略模式，构造时创建）

    // ---------- 历史截图翻页贴图 ----------
    int m_pinHistoryIndex = -1;              ///< 历史截图翻页索引（-1=未开始，0=最新，递增=更早）
    QPoint m_lastPinPos;                     ///< 上一个翻页贴图的左上角位置（用于位置继承）
    QPointer<PinWindow> m_lastPinHistoryWindow; ///< 跟踪最后一个翻页贴图（关闭后自动置 null）

    // ---------- macOS 跨屏切换 ----------
    QTimer *m_screenWatchTimer = nullptr;    ///< 轮询鼠标所在屏的定时器（macOS 单屏窗口模式）
};

#endif // SNIPSCREEN_H
