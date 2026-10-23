#ifndef CLOTHESLINEWINDOW_H
#define CLOTHESLINEWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
#include <QList>

class CardPreviewPopup;

/**
 * @brief 晾衣绳历史窗口
 *
 * 点击桌面宠物后弹出：一条铺满主屏幕宽度的悬链线晾衣绳（默认位于屏幕顶部
 * 往下 1/4 高度处），绳上挂着最近的截图与剪贴板文本（数据来源 HistoryManager），
 * 卡片带入场下落与轻微摆动动画。
 * - 拖动空白区域可移动窗口位置；单击卡片：把对应截图/文本重新复制到系统剪贴板
 * - 悬停卡片：以晾衣绳窗口中心为基准弹出放大预览，移出卡片即消失
 * - 双击空白区域：隐藏晾衣绳；左上角/右上角/左下角/右下角与上下边界可拖拽调整窗口大小；右上角 × 按钮关闭
 * - 关闭：× 按钮、双击空白、Esc、再次点击宠物、点击窗口外
 * - 历史增删时实时刷新（刷新时保留用户拖动后的位置）
 * @author chiangyang
 */
class ClotheslineWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @author chiangyang
     */
    explicit ClotheslineWindow(QWidget *parent = nullptr);

    /**
     * @brief 以主屏幕全宽显示在屏幕上方（顶部往下 1/4 高度处）
     * @author chiangyang
     */
    void showFullWidth();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    /**
     * @brief 晾衣绳上的一张卡片
     */
    struct Card {
        qint64 id = 0;          ///< 历史记录 ID
        bool isImage = false;   ///< 截图 or 文本
        QPixmap thumb;          ///< 截图缩略图
        QString text;           ///< 文本摘要 / 图片原路径
        qreal entrance = 0.0;   ///< 入场进度 [0,1]（下落+淡入）
        QRect rect;             ///< 卡片矩形（未含摆动旋转的命中矩形）
        QPointF anchor;         ///< 衣夹在绳上的锚点
    };

    /**
     * @brief 从 HistoryManager 重新加载最近记录并重建卡片布局
     * @author chiangyang
     */
    void reload();

    /**
     * @brief 计算晾衣绳在 x 处的高度（悬链线采样）
     * @param x 窗口局部 x 坐标
     * @return 绳上对应点的 y 坐标
     * @author chiangyang
     */
    qreal ropeYAt(qreal x) const;

    /**
     * @brief 动画 tick：推进摆动相位与入场进度
     * @author chiangyang
     */
    void onTick();

    /**
     * @brief 点击卡片：复制内容到剪贴板并触发弹跳反馈
     * @param index 卡片下标
     * @author chiangyang
     */
    void activateCard(int index);

    /**
     * @brief 重建卡片布局（锚点由绳子形状决定，依赖当前窗口宽度）
     * @author chiangyang
     */
    void layoutCards();

    /**
     * @brief 窗口边缘调整模式
     */
    enum ResizeMode {
        ResizeNone = 0,   ///< 非调整状态
        ResizeTopLeft,    ///< 左上角
        ResizeTopRight,   ///< 右上角
        ResizeBottomLeft, ///< 左下角
        ResizeBottomRight,///< 右下角
        ResizeTop,        ///< 上边界
        ResizeBottom      ///< 下边界
    };

    /**
     * @brief 命中检测：判断 pos 落在哪个边缘/角落调整区
     * @param pos 窗口局部坐标
     * @return 调整模式，不在调整区时返回 ResizeNone
     * @author chiangyang
     */
    ResizeMode hitTestResize(const QPoint &pos) const;

    /**
     * @brief 右上角关闭按钮（×）的矩形区域
     * @return 窗口局部坐标矩形
     * @author chiangyang
     */
    QRect closeButtonRect() const;

    /**
     * @brief 更新悬停光标（调整区/卡片/空白区域各自的光标形状）
     * @param pos 窗口局部坐标
     * @author chiangyang
     */
    void updateHoverCursor(const QPoint &pos);

    /**
     * @brief 更新卡片悬停预览：悬停卡片时以窗口中心为基准弹出放大预览，
     *        移出卡片/拖动/调整大小时隐藏
     * @param pos 窗口局部坐标
     * @author chiangyang
     */
    void updateHoverPreview(const QPoint &pos);

    /**
     * @brief 隐藏卡片放大预览弹窗
     * @author chiangyang
     */
    void hidePreview();

    QList<Card> m_cards;            ///< 卡片列表
    QTimer *m_animTimer = nullptr;  ///< 摆动/入场动画定时器
    qreal m_phase = 0.0;            ///< 摆动相位
    QElapsedTimer m_showElapsed;    ///< 显示至今的时间（入场动画用）
    int m_bounceIndex = -1;         ///< 正在弹跳反馈的卡片下标
    QElapsedTimer m_bounceTimer;    ///< 弹跳开始时间
    bool m_dragging = false;        ///< 窗口拖动中
    QPoint m_dragOffset;            ///< 按下点相对窗口左上角的偏移
    ResizeMode m_resizeMode = ResizeNone; ///< 当前进行中的边缘调整模式
    QPoint m_resizeStartGlobal;     ///< 调整开始时的全局鼠标坐标
    QRect m_resizeStartGeo;         ///< 调整开始时的窗口全局几何
    bool m_closeHovered = false;    ///< 鼠标悬停在关闭按钮上
    bool m_pendingClose = false;    ///< 双击空白区域请求关闭（等本次鼠标序列结束后执行，防止事件穿透到桌面）
    CardPreviewPopup *m_preview = nullptr; ///< 悬停放大预览弹窗（不接收鼠标事件）
    int m_hoverIndex = -1;          ///< 当前悬停的卡片下标（-1 表示无悬停）
};

#endif // CLOTHESLINEWINDOW_H
