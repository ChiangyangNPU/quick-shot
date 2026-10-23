#ifndef CLOTHESLINEWINDOW_H
#define CLOTHESLINEWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QElapsedTimer>
#include <QList>

class PetWindow;

/**
 * @brief 晾衣绳历史窗口
 *
 * 点击桌面宠物后弹出：一条悬链线晾衣绳上挂着最近的截图与剪贴板文本
 * （数据来源 HistoryManager），卡片带入场下落与轻微摆动动画。
 * - 单击卡片：把对应截图/文本重新复制到系统剪贴板，并弹跳反馈
 * - 关闭：Esc、再次点击宠物、点击窗口外
 * - 历史增删时实时刷新
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
     * @brief 在宠物附近显示（优先宠物上方，空间不足则下方）
     * @param pet 宠物窗口
     * @author chiangyang
     */
    void showNear(PetWindow *pet);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
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

    QList<Card> m_cards;            ///< 卡片列表
    QTimer *m_animTimer = nullptr;  ///< 摆动/入场动画定时器
    qreal m_phase = 0.0;            ///< 摆动相位
    QElapsedTimer m_showElapsed;    ///< 显示至今的时间（入场动画用）
    int m_bounceIndex = -1;         ///< 正在弹跳反馈的卡片下标
    QElapsedTimer m_bounceTimer;    ///< 弹跳开始时间
};

#endif // CLOTHESLINEWINDOW_H
