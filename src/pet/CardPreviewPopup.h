#ifndef CARDPREVIEWPOPUP_H
#define CARDPREVIEWPOPUP_H

#include <QWidget>
#include <QPixmap>
#include <QString>
#include <QPoint>
#include <QSize>

/**
 * @brief 卡片悬停放大预览弹窗
 *
 * 鼠标悬停在晾衣绳卡片上时，以晾衣绳窗口中心为基准居中弹出：
 * - 图片卡片：加载原图等比放大（不超过 kPreviewMaxW × kPreviewMaxH，按 DPR 输出保证清晰）
 * - 文本卡片：自动换行显示完整文本（超出最大高度时截断）
 * - 鼠标移到预览图上时滚轮可放大/缩小（类似贴图窗口），移出预览即消失
 * - 右键预览弹出菜单（参考贴图窗口）：复制/标注/文字识别/翻译（文本卡片仅复制/翻译），
 *   点击菜单项后预览消失并执行对应功能
 * @author chiangyang
 */
class CardPreviewPopup : public QWidget {
public:
    /**
     * @brief 构造函数：无边框置顶工具窗，不抢焦点
     * @param parent 父对象（晾衣绳窗口，作为对话框父级）
     * @author chiangyang
     */
    explicit CardPreviewPopup(QWidget *parent = nullptr);

    /**
     * @brief 显示图片预览（等比放大）
     * @param pix 原图/缩略图
     * @param centerGlobal 期望中心的全局坐标（晾衣绳窗口中心）
     * @author chiangyang
     */
    void showImage(const QPixmap &pix, const QPoint &centerGlobal);

    /**
     * @brief 显示文本预览（自动换行，限宽限高）
     * @param text 文本内容
     * @param centerGlobal 期望中心的全局坐标（晾衣绳窗口中心）
     * @author chiangyang
     */
    void showText(const QString &text, const QPoint &centerGlobal);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 预览内容有效最大尺寸：kPreviewMaxW×kPreviewMaxH 夹取到所在屏幕
     *        可用区域（四边各留 20px 边距），避免预览超出屏幕
     * @author chiangyang
     */
    QSize effectiveMaxSize(const QPoint &centerGlobal) const;

    /**
     * @brief 以期望中心定位并显示（夹取到所在屏幕可用区域内）
     * @author chiangyang
     */
    void place(const QPoint &centerGlobal, const QSize &windowSize);

    /**
     * @brief 按当前缩放倍数渲染图片：从原图按目标尺寸出图（DPR 物理像素），保持清晰
     * @author chiangyang
     */
    void renderImage();

    /**
     * @brief 按当前缩放倍数渲染文本：字号随缩放变化重新排版（文字始终清晰），
     *        窗口尺寸夹取到屏幕可用区域
     * @author chiangyang
     */
    void renderText();

    QPixmap m_original;         ///< 原图（图片模式的缩放源，保证缩放后清晰）
    QPixmap m_pix;              ///< 当前缩放后待绘制的图
    QString m_text;             ///< 文本预览内容
    bool m_isImage = true;      ///< 当前是图片预览还是文本预览
    QSize m_baseContent;        ///< zoom=1 时图片内容的逻辑尺寸
    qreal m_zoom = 1.0;         ///< 当前缩放倍数
    qreal m_baseFontPt = 12.0;  ///< 文本基准字号（zoom=1）
    int m_wrapWidth = 460;      ///< 文本换行宽度
    QPoint m_centerGlobal;      ///< 居中基准（晾衣绳窗口中心）
    bool m_menuOpen = false;    ///< 右键菜单是否打开中（打开期间 leaveEvent 不隐藏预览）
    int m_pad = 10;             ///< 预览卡片内边距（按 DPI 缩放，96 基准 10px）
};

#endif // CARDPREVIEWPOPUP_H
