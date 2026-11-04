#ifndef TRANSLATEOVERLAYWINDOW_H
#define TRANSLATEOVERLAYWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QStringList>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <functional>

class QLabel;
class QTextEdit;
class QToolButton;
class TranslateImageView;

/**
 * @brief 翻译结果窗口
 *
 * 采用「左原图、右译文」的左右分栏布局：左侧等比缩放显示截图原图，
 * 右侧以可选中、可滚动的文本区域展示识别/翻译结果，底部提供复制全文与
 * 另存为图片操作，标题栏支持拖动窗口、双击关闭。
 * 支持原文/译文/对照三种视图模式（仅影响右侧文本内容）。
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
        Original,    ///< 仅原文（右侧显示 OCR 原文）
        Translation, ///< 仅译文（右侧显示译文）
        Both         ///< 对照（右侧按段显示原文 + 译文）
    };

    /**
     * @brief 构造函数
     * @param pixmap 截图原图（已包含原文文字）
     * @param texts 原文文本列表
     * @param translatedTexts 译文文本列表（与 texts 索引一一对应）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit TranslateOverlayWindow(const QPixmap &pixmap,
                                    const QStringList &texts,
                                    const QStringList &translatedTexts,
                                    QWidget *parent = nullptr);

    /**
     * @brief 一站式批量翻译并显示翻译结果窗口
     *
     * 封装 OCR 完成后的翻译显示流程，供 SnipScreen、PinWindow 等所有翻译入口复用，
     * 消除重复的信号连接与标签管理代码。流程：
     * 1. 创建"翻译中"加载提示标签（居中在 labelRect）
     * 2. 连接 TranslateService::batchFinished → 隐藏提示、创建结果窗口、定位到 overlayPos
     * 3. 连接 TranslateService::failed → 隐藏提示、显示本地化错误标签（3秒自动消失）
     * 4. 发起 translateBatch 逐段翻译
     * @param parent 父窗口（用于创建标签和信号接收者）
     * @param pixmap 截图原图（用于左侧原图显示）
     * @param texts 原文文本列表
     * @param overlayPos 结果窗口定位点（全局坐标，自动夹取到屏幕内）
     * @param labelRect 加载/错误标签的居中区域（parent 相对坐标）
     * @param onOverlayShown 翻译成功并显示结果窗口后的回调（可选）。
     *        SnipScreen 传入此回调以在窗口显示后退出截图框（类似贴图完成后销毁截图框）；
     *        PinWindow 不传（保持贴图窗口继续显示）。
     * @author chiangyang
     */
    static void translateAndShow(QWidget *parent,
                                 const QPixmap &pixmap,
                                 const QStringList &texts,
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

protected:
    /**
     * @brief 绘制事件：绘制窗口背景与边框
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 右键菜单事件：视图切换、复制、另存、关闭
     * @param event 右键菜单事件
     * @author chiangyang
     */
    void contextMenuEvent(QContextMenuEvent *event) override;

    /**
     * @brief 事件过滤器：把原图与文本区域的右键事件转发给本窗口
     *
     * 子控件会吞掉 contextMenuEvent 弹出自己的标准菜单，导致右侧文本区域
     * 无法呼出本窗口的完整右键菜单。此处拦截 QEvent::ContextMenu，
     * 统一交给 showContextMenu 处理。
     * @param watched 被监听的对象（原图控件或文本编辑框 viewport）
     * @param event 事件
     * @return true 表示已处理，false 表示继续传递
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @brief 根据当前视图模式刷新右侧文本内容
     * @author chiangyang
     */
    void updateTextContent();

    /**
     * @brief 弹出窗口右键菜单
     * @param globalPos 菜单弹出位置（全局坐标）
     * @author chiangyang
     */
    void showContextMenu(const QPoint &globalPos);

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
     * @brief 另存原图为图片文件
     * @author chiangyang
     */
    void saveAsImage();

    QPixmap m_pixmap;                   ///< 截图原图
    QStringList m_texts;                ///< 原文文本列表
    QStringList m_translatedTexts;      ///< 译文文本列表（与 m_texts 索引对应）
    ViewMode m_viewMode = Translation;  ///< 当前视图模式，默认显示译文

    QColor m_windowBg;                  ///< 窗口背景色（跟随主题）
    QColor m_borderColor;               ///< 窗口边框色（跟随主题）

    TranslateImageView *m_imageView = nullptr; ///< 左侧原图显示控件
    QTextEdit *m_textView = nullptr;           ///< 右侧结果文本编辑框
};

#endif // TRANSLATEOVERLAYWINDOW_H
