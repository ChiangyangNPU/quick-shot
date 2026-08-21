#ifndef SELECTOR_H
#define SELECTOR_H

#include "IPreyDetector.h"
#include "Hunter.h"
#include "Resizer.h"
#include "StyleManager.h"
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QRegion>
#include <QWidget>

/**
 * @brief 选区状态枚举
 *
 * 状态转换：
 *   Ready -> PreySelecting（鼠标进入）
 *   PreySelecting -> FreeSelecting（按下并拖拽）
 *   PreySelecting -> Captured（按下后释放，吸附到目标）
 *   FreeSelecting -> Captured（释放鼠标）
 *   Captured -> Moving（点击选区内部）
 *   Captured -> Resizing（点击锚点/边框）
 *   Captured -> PreySelecting（右键，重新选择）
 *   任意状态 -> Ready（关闭选区）
 *   Captured -> Locked（进入标注模式，鼠标穿透）
 * @author chiangyang
 */
enum class SelectorStatus {
    Ready,          ///< 等待操作
    PreySelecting,  ///< 鼠标移动中，自动吸附到窗口/显示器
    FreeSelecting,  ///< 按住拖拽，自由选区
    Captured,       ///< 选区完成，等待操作
    Moving,         ///< 移动选区
    Resizing,       ///< 调整选区大小
    Locked,         ///< 锁定（标注模式，鼠标事件穿透到下层）
};

/**
 * @brief 选区作用域枚举
 *
 * Desktop: 跨显示器选区，范围为整个虚拟桌面
 * Display: 限制在单个显示器内
 * @author chiangyang
 */
enum class SelectionScope {
    Desktop, ///< 跨显示器
    Display, ///< 单显示器
};

/**
 * @brief 选区 Widget
 *
 * 覆盖整个虚拟桌面，提供区域选择功能。
 * 支持自动吸附到窗口/显示器、自由选区、移动、调整大小等操作。
 * 通过依赖注入 IPreyDetector 实现与 Hunter 的解耦。
 *
 * 使用流程：
 * 1. 创建 Selector，设置父窗口为全屏 SnipScreen
 * 2. 设置 IPreyDetector 检测器实例
 * 3. 调用 start() 开始选区
 * 4. 用户通过鼠标/键盘完成选区
 * 5. 通过 selected() 获取选区矩形
 * 6. 通过 prey() 获取选区吸附的目标信息
 * 7. 调用 close() 结束选区
 * @author chiangyang
 */
class Selector : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param detector 猎物检测器实例（可选，默认 nullptr）
     * @param parent 父窗口（应为覆盖虚拟桌面的全屏窗口）
     * @author chiangyang
     */
    explicit Selector(IPreyDetector *detector = nullptr, QWidget *parent = nullptr);

    /**
     * @brief 获取当前选区状态
     * @author chiangyang
     */
    SelectorStatus status() const { return m_status; }

    /**
     * @brief 获取当前吸附的猎物信息
     * @author chiangyang
     */
    Prey prey() const { return m_prey; }

    /**
     * @brief 获取选区矩形
     * @param relative true 返回相对于父窗口的坐标，false 返回全局坐标
     * @return 选区矩形
     * @author chiangyang
     */
    QRect selected(bool relative = false) const;

    /**
     * @brief 设置选区作用域
     * @author chiangyang
     */
    void setScope(SelectionScope scope) { m_scope = scope; }

    /**
     * @brief 获取选区作用域
     * @author chiangyang
     */
    SelectionScope scope() const { return m_scope; }

    /**
     * @brief 设置选区最小有效尺寸
     * @param w 最小宽度
     * @param h 最小高度
     * @author chiangyang
     */
    void setMinValidSize(int w, int h) { m_minSize = QSize(qMax(2, w), qMax(2, h)); }

    /**
     * @brief 检查选区是否无效（太小）
     * @author chiangyang
     */
    bool isInvalid() const { return m_box.width() < m_minSize.width() || m_box.height() < m_minSize.height(); }

    /**
     * @brief 设置选区为指定猎物
     * @param prey 猎物信息
     * @author chiangyang
     */
    void select(const Prey &prey);

    /**
     * @brief 设置选区为指定矩形
     * @param rect 全局坐标矩形
     * @author chiangyang
     */
    void select(const QRect &rect);

    /**
     * @brief 设置画笔坐标系
     *
     * 当 Selector 的父窗口不在 (0,0) 时，需要设置此偏移
     * 使绘制坐标与全局坐标对齐。
     *
     * @param window 父窗口的全局几何
     * @author chiangyang
     */
    void setCoordinate(const QRect &window) { m_coordinate = window; }

    /**
     * @brief 设置边框样式
     * @author chiangyang
     */
    void setBorderPen(const QPen &pen) { m_borderPen = pen; }

    /**
     * @brief 设置遮罩颜色
     * @author chiangyang
     */
    void setMaskColor(const QColor &color) { m_maskColor = color; }

    /**
     * @brief 是否显示十字准线
     * @author chiangyang
     */
    void showCrossHair(bool show) { m_crossHair = show; update(); }

    /**
     * @brief 设置标注模式
     * @param enabled true 表示进入标注模式，此时鼠标在选区内显示十字光标
     * @author chiangyang
     */
    void setAnnotationMode(bool enabled) { m_isAnnotationMode = enabled; }

    /**
     * @brief 获取当前是否为标注模式
     * @author chiangyang
     */
    bool isAnnotationMode() const { return m_isAnnotationMode; }

    /**
     * @brief 更新尺寸信息标签内容和位置
     * @author chiangyang
     */
    void updateInfoLabel();

    /**
     * @brief 切换到新屏幕（macOS 单屏窗口模式专用）
     *
     * 当鼠标跨屏移动时，更新坐标系和选区范围到新屏幕，
     * 并重新吸附到鼠标下的目标。
     * @param screenGeometry 新屏幕的全局几何
     * @author chiangyang
     */
    void switchScreen(const QRect &screenGeometry);

    /**
     * @brief 显示录制时间标签
     * @author chiangyang
     */
    void showTimerLabel();

    /**
     * @brief 更新录制时间文本
     * @param text 时间文本，如 "00:05"
     * @author chiangyang
     */
    void updateTimerText(const QString &text);

    /**
     * @brief 隐藏录制时间标签
     * @author chiangyang
     */
    void hideTimerLabel();

    /**
     * @brief 获取信息标签和时间标签的独立区域（用于遮罩计算）
     * @return 各标签独立矩形的区域（不合并，保持各自宽度）
     * @author chiangyang
     */
    QRegion getLabelGeometry() const;

signals:
    /**
     * @brief 正在选区（鼠标拖拽中）
     * @author chiangyang
     */
    void selecting();

    /**
     * @brief 选区完成
     * @author chiangyang
     */
    void captured();

    /**
     * @brief 选区被移动
     * @author chiangyang
     */
    void moved();

    /**
     * @brief 选区被调整大小
     * @author chiangyang
     */
    void resized();

    /**
     * @brief 选区被锁定（进入标注模式）
     * @author chiangyang
     */
    void locked();

    /**
     * @brief 选区被停止（用户取消）
     * @author chiangyang
     */
    void stopped();

    /**
     * @brief 选区状态变化
     * @author chiangyang
     */
    void statusChanged(SelectorStatus status);

public slots:
    /**
     * @brief 开始选区
     * @param flags 窗口过滤标志（暂未使用，预留接口）
     * @author chiangyang
     */
    void start();

    /**
     * @brief 设置选区状态
     * @author chiangyang
     */
    void setStatus(SelectorStatus status);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    /**
     * @brief 注册快捷键
     * @author chiangyang
     */
    void registerShortcuts();

    /**
     * @brief 更新录制时间标签位置
     * @author chiangyang
     */
    void updateTimerLabel();

    /**
     * @brief 移动选区
     * @author chiangyang
     */
    void translateSelection(int dx, int dy);

    /**
     * @brief 调整选区大小
     * @author chiangyang
     */
    void adjustSelection(int dx1, int dy1, int dx2, int dy2);

    /**
     * @brief 调整选区边距
     * @author chiangyang
     */
    void marginsSelection(int dt, int dr, int db, int dl);

    // ---------- 状态 ----------
    SelectorStatus m_status = SelectorStatus::Ready;
    SelectionScope m_scope = SelectionScope::Desktop;
    Resizer m_box;           ///< 选区矩形管理器
    Prey m_prey;             ///< 当前吸附的猎物
    QRect m_coordinate;      ///< 画笔坐标系偏移
    IPreyDetector *m_detector = nullptr; ///< 猎物检测器（依赖注入）

    // ---------- 鼠标交互 ----------
    QPoint m_dragStart;      ///< 拖拽起始点
    QPoint m_moveStart;      ///< 拖动选区时的起始点
    QPoint m_moveEnd;        ///< 拖动选区时的结束点
    bool m_isDragging = false; ///< 是否正在拖拽（按下但尚未进入 FreeSelecting）
    ResizerLocation m_cursorPos = ResizerLocation::DEFAULT; ///< 鼠标按下时的位置类型
    bool m_isAnnotationMode = false; ///< 是否处于标注模式

    // ---------- 样式 ----------
    QPen m_borderPen{StyleManager::getCaptureBorderColor(), StyleManager::SNIP_BORDER_WIDTH, Qt::SolidLine};
    QColor m_maskColor{0, 0, 0, 100};
    bool m_crossHair = false;

    // ---------- UI ----------
    QLabel *m_infoLabel = nullptr;  ///< 尺寸信息标签
    QLabel *m_timerLabel = nullptr; ///< 录制时间标签
    QSize m_minSize{2, 2};          ///< 最小有效尺寸
};

#endif // SELECTOR_H
