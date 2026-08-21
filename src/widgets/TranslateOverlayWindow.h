#ifndef TRANSLATEOVERLAYWINDOW_H
#define TRANSLATEOVERLAYWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QVector>
#include <QPolygonF>
#include <QStringList>
#include <QPoint>
#include <QRect>
#include <QList>
#include <functional>

class QTextEdit;

/**
 * @brief 翻译叠加窗口（二期）
 *
 * 显示截图原图，并在原文位置叠加显示译文。支持原文/译文/对照三种视图模式。
 * 参考 PinWindow 的无边框置顶窗口机制，支持拖动、滚轮缩放、ESC 关闭。
 *
 * 译文通过 QPainter 在原文本多边形位置绘制半透明背景遮盖原文后叠加显示，
 * 字号根据区域大小自动缩放，文本自动换行适配区域宽度。
 * @author chiangyang
 */
class TranslateOverlayWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 视图模式枚举
     * @author chiangyang
     */
    enum ViewMode {
        Original,    ///< 仅原文（显示原图）
        Translation, ///< 仅译文（遮挡原文位置显示译文）
        Both         ///< 对照（原文下方追加显示译文）
    };

    /**
     * @brief 构造函数
     * @param pixmap 截图原图（已包含原文文字）
     * @param texts 原文文本列表
     * @param polygons 原文区域多边形（像素坐标，相对截图图像）
     * @param translatedTexts 译文文本列表（与 texts 索引一一对应）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit TranslateOverlayWindow(const QPixmap &pixmap,
                                    const QStringList &texts,
                                    const QVector<QPolygonF> &polygons,
                                    const QStringList &translatedTexts,
                                    QWidget *parent = nullptr);

    /**
     * @brief 一站式批量翻译并显示译文叠加窗口
     *
     * 封装 OCR 完成后的翻译显示流程，供 SnipScreen、PinWindow 等所有翻译入口复用，
     * 消除重复的信号连接与标签管理代码。流程：
     * 1. 创建"翻译中"加载提示标签（居中在 labelRect）
     * 2. 连接 TranslateService::batchFinished → 隐藏提示、创建 Overlay 窗口、定位到 overlayPos
     * 3. 连接 TranslateService::failed → 隐藏提示、显示本地化错误标签（3秒自动消失）
     * 4. 发起 translateBatch 逐段翻译
     * @param parent 父窗口（用于创建标签和信号接收者）
     * @param pixmap 截图原图（用于 Overlay 背景显示）
     * @param texts 原文文本列表
     * @param polygons 原文区域多边形（像素坐标，相对截图图像）
     * @param overlayPos Overlay 窗口定位点（全局坐标）
     * @param labelRect 加载/错误标签的居中区域（parent 相对坐标）
     * @param onOverlayShown 翻译成功并显示 Overlay 后的回调（可选）。
     *        SnipScreen 传入此回调以在 Overlay 显示后退出截图框（类似贴图完成后销毁截图框）；
     *        PinWindow 不传（保持贴图窗口继续显示）。
     * @author chiangyang
     */
    static void translateAndShow(QWidget *parent,
                                 const QPixmap &pixmap,
                                 const QStringList &texts,
                                 const QVector<QPolygonF> &polygons,
                                 const QPoint &overlayPos,
                                 const QRect &labelRect,
                                 std::function<void()> onOverlayShown = nullptr);

    /**
     * @brief 设置视图模式
     * @param mode 视图模式
     * @author chiangyang
     */
    void setViewMode(ViewMode mode);

    /**
     * @brief 获取当前视图模式
     * @return 当前视图模式
     * @author chiangyang
     */
    ViewMode viewMode() const;

    /**
     * @brief 切换文字选择模式
     *
     * 开启时为每个译文段创建可选文字的 QLabel，支持鼠标拖选和 Ctrl+C 复制；
     * 关闭时移除 QLabel，恢复 QPainter 绘制。
     * @param enabled true 进入选择模式，false 退出
     * @author chiangyang
     */
    void setTextSelectionMode(bool enabled);

protected:
    /**
     * @brief 绘制事件：绘制原图与译文叠加层
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 鼠标按下事件：开始拖动或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件：拖动窗口或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件：结束拖动或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标双击事件：关闭窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标滚轮事件：缩放窗口
     * @param event 滚轮事件
     * @author chiangyang
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @brief 键盘按键事件：ESC 关闭窗口
     * @param event 键盘事件
     * @author chiangyang
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief 右键菜单事件：视图切换、复制、另存、关闭
     * @param event 右键菜单事件
     * @author chiangyang
     */
    void contextMenuEvent(QContextMenuEvent *event) override;

    /**
     * @brief 事件过滤器：拦截选择模式编辑器的右键菜单事件
     *
     * 选字模式下覆盖窗口的 QTextEdit 会吞掉 contextMenuEvent 弹出自己的标准菜单，
     * 导致用户无法呼出 TranslateOverlayWindow 的完整右键菜单（含"退出选字模式"选项）。
     * 此处拦截 QEvent::ContextMenu，转发给本窗口的 contextMenuEvent。
     * @param watched 被监听的对象（m_selectEditor）
     * @param event 事件
     * @return true 表示已处理，false 表示继续传递
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief 窗口大小变化事件：重新定位选择模式标签
     * @param event 大小变化事件
     * @author chiangyang
     */
    void resizeEvent(QResizeEvent *event) override;

private:
    /**
     * @brief 在指定矩形区域绘制译文（半透明背景 + 自动换行 + 缩放字号）
     * @param painter 画笔
     * @param text 译文文本
     * @param rect 目标矩形区域
     * @author chiangyang
     */
    void drawTranslatedText(QPainter &painter, const QString &text, const QRectF &rect);

    /**
     * @brief 绘制译文叠加层（根据当前视图模式）
     * @param painter 画笔（调用方需已将坐标系设置到 pixmap 坐标系）
     * @author chiangyang
     */
    void drawOverlay(QPainter &painter);

    /**
     * @brief 复制原文到剪贴板
     * @author chiangyang
     */
    void copyOriginal();

    /**
     * @brief 复制译文到剪贴板
     * @author chiangyang
     */
    void copyTranslation();

    /**
     * @brief 另存为图片文件
     * @author chiangyang
     */
    void saveAsImage();

    /**
     * @brief 检查鼠标是否在右下角调整大小区域内
     * @param pos 鼠标位置（相对窗口）
     * @return 是否在调整大小区域内
     * @author chiangyang
     */
    bool isInResizeArea(const QPoint &pos) const;

    /**
     * @brief 更新选择模式编辑器的大小（窗口缩放后调用）
     * @author chiangyang
     */
    void updateSelectEditorGeometry();

    QPixmap m_pixmap;                   ///< 截图原图
    QStringList m_texts;                ///< 原文文本列表
    QVector<QPolygonF> m_polygons;      ///< 原文区域多边形（像素坐标）
    QStringList m_translatedTexts;      ///< 译文文本列表（与 m_texts 索引对应）
    ViewMode m_viewMode = Translation;  ///< 当前视图模式，默认显示译文

    QPoint m_dragPosition;              ///< 拖拽起始位置
    bool m_isMoving = false;            ///< 是否正在拖动
    bool m_isResizing = false;          ///< 是否正在调整大小

    bool m_textSelectionMode = false;   ///< 是否处于文字选择模式
    QTextEdit *m_selectEditor = nullptr; ///< 选择模式下的全文编辑器
};

#endif // TRANSLATEOVERLAYWINDOW_H
