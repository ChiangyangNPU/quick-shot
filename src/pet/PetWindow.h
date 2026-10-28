#ifndef PETWINDOW_H
#define PETWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QPixmap>
#include <QAbstractAnimation>
#include <QPointer>
#include "PetUiScale.h"

class QMovie;
class QPainter;

/**
 * @brief 桌面宠物窗口
 *
 * 无边框透明置顶小窗口，用 QPainter 代码绘制一只圆滚滚的卡通宠物，
 * 支持多种外观样式（小橘猫/柴犬/熊猫/兔子/企鹅/青蛙，PetStyle 枚举），
 * 共用闭嘴/张嘴/咀嚼三态动画：
 * - 鼠标拖动改变位置（拖动结束发射 dragFinished）
 * - 单击发射 clicked（与拖动按移动距离区分）
 * - openMouth() 张嘴等待食物，chew() 咀嚼弹跳
 * - 待机时随机眨眼
 * - setStyle() 切换样式（配合 renderStylePreview 可生成设置页预览图）
 * @author chiangyang
 */
class PetWindow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal mouthOpen READ mouthOpen WRITE setMouthOpen)
    Q_PROPERTY(qreal chewScale READ chewScale WRITE setChewScale)
    Q_PROPERTY(qreal starProgress READ starProgress WRITE setStarProgress)

public:
    /**
     * @brief 宠物外观样式
     *
     * 枚举值稳定不变（pet/style 按数字持久化）。UI 分组由设置页
     * （SettingsPetTab 的 kClassicStyles/kZodiacStyles 下标表）决定：
     * 经典组 = 猫/熊猫/企鹅/青蛙；十二生肖组（按传统顺序鼠牛虎兔龙蛇马羊
     * 猴鸡狗猪）中，兔复用 Bunny 造型、狗复用 Shiba 造型（显示名"兔/狗"），
     * 其余十种为生肖专属造型。
     * @author chiangyang
     */
    enum class PetStyle {
        Cat = 0,      ///< 小橘猫（默认，经典组）
        Shiba = 1,    ///< 狗（柴犬造型，生肖组）
        Panda = 2,    ///< 熊猫（经典组）
        Bunny = 3,    ///< 兔（长耳造型，生肖组）
        Penguin = 4,  ///< 企鹅（经典组）
        Frog = 5,     ///< 青蛙（经典组）
        Mouse = 6,    ///< 生肖·鼠
        Ox = 7,       ///< 生肖·牛
        Tiger = 8,    ///< 生肖·虎
        Dragon = 9,   ///< 生肖·龙
        Snake = 10,   ///< 生肖·蛇
        Horse = 11,   ///< 生肖·马
        Goat = 12,    ///< 生肖·羊
        Monkey = 13,  ///< 生肖·猴
        Rooster = 14, ///< 生肖·鸡
        Pig = 15      ///< 生肖·猪
    };

    /**
     * @brief 样式总数（设置页遍历用）
     * @author chiangyang
     */
    static constexpr int petStyleCount() { return 16; }

    /**
     * @brief 构造函数
     * @param parent 父对象（顶层窗口传 nullptr）
     * @author chiangyang
     */
    explicit PetWindow(QWidget *parent = nullptr);

    /**
     * @brief 宠物窗口边长（正方形，96 逻辑基准按屏幕 DPI 缩放）
     * @return 边长像素值
     * @author chiangyang
     */
    static int petSize() { return qRound(96 * PetUi::scale()); }

    /**
     * @brief 渲染某样式的静态预览图（设置页样式选择下拉框图标用）
     * @param style 宠物样式
     * @param size 预览图边长（像素）
     * @return 透明背景的预览图（待机姿态：闭嘴、睁眼、无星星）
     * @author chiangyang
     */
    static QPixmap renderStylePreview(PetStyle style, int size);

    /**
     * @brief 渲染自定义宠物素材的预览图（设置页缩略图用）
     * @param sourcePath 素材文件路径（静态图或 GIF，GIF 取首帧）
     * @param size 预览图边长（像素）
     * @return 透明背景、等比居中缩放的预览图；无法解码时返回空图
     * @author chiangyang
     */
    static QPixmap renderCustomPreview(const QString &sourcePath, int size);

    /**
     * @brief 切换为自定义宠物素材（替代内置造型绘制）
     *
     * GIF 用 QMovie 循环播放，静态图直接绘制；两者均按窗口等比居中，
     * 进食/咀嚼动效改为整体挤压回弹（见 paintCustomPet）。
     * @param sourcePath 素材文件路径，传空字符串等同于 clearCustomPet()
     * @author chiangyang
     */
    void setCustomPet(const QString &sourcePath);

    /**
     * @brief 清除自定义素材，恢复内置造型绘制
     * @author chiangyang
     */
    void clearCustomPet() { setCustomPet(QString()); }

    /**
     * @brief 当前是否处于自定义素材模式
     * @return 是否有自定义素材
     * @author chiangyang
     */
    bool isCustomActive() const { return !m_customPath.isEmpty(); }

    /**
     * @brief 切换宠物外观样式（立即重绘）
     * @param style 新样式
     * @author chiangyang
     */
    void setStyle(PetStyle style);

    /**
     * @brief 当前外观样式
     * @return 样式枚举值
     * @author chiangyang
     */
    PetStyle style() const { return m_style; }

    /**
     * @brief 张嘴等待食物（飞行动画开始时调用）
     * @author chiangyang
     */
    void openMouth();

    /**
     * @brief 咀嚼吞下（飞行动画到达时调用）：嘴巴反复张合咀嚼，
     *        身体挤压回弹并行，完成后左眼闪一颗星星作为奖励
     * @author chiangyang
     */
    void chew();

    /**
     * @brief 宠物嘴部的全局屏幕坐标（飞行动画终点）
     * @return 嘴部中心全局坐标
     * @author chiangyang
     */
    QPoint mouthCenterGlobal() const;

    qreal mouthOpen() const { return m_mouthOpen; }
    void setMouthOpen(qreal v);

    qreal chewScale() const { return m_chewScale; }
    void setChewScale(qreal v);

    qreal starProgress() const { return m_starProgress; }
    void setStarProgress(qreal v);

signals:
    /**
     * @brief 单击信号（与拖动区分：移动距离未超阈值）
     * @author chiangyang
     */
    void clicked();

    /**
     * @brief 拖动结束信号
     * @param newPos 拖动结束后的窗口左上角全局坐标
     * @author chiangyang
     */
    void dragFinished(const QPoint &newPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /**
     * @brief 绘制自定义素材（静态图或 GIF 当前帧）
     *
     * 与内置造型共用咀嚼挤压/张嘴动效参数：形象整体绕中心挤压回弹，
     * 张嘴时轻微上浮放大；完成后同样在形象左上闪一颗奖励星星。
     * @param p 画笔（已按 96 基准坐标系缩放）
     * @author chiangyang
     */
    void paintCustomPet(QPainter &p);

    /**
     * @brief 计算自定义素材在 96 基准坐标系中的绘制矩形
     *
     * 按素材宽高比等比 contain 到窗口内并居中，非正方形素材不会变形。
     * @return 素材绘制矩形
     * @author chiangyang
     */
    QRectF customTargetRect() const;

    /**
     * @brief 调度下一次眨眼（随机 2.4~5 秒；星星闪烁期间自动跳过）
     * @author chiangyang
     */
    void scheduleBlink();

    /**
     * @brief 左眼星星闪烁动画（咀嚼完成后触发，闪完恢复眨眼状态）
     * @author chiangyang
     */
    void sparkleStar();

    qreal m_mouthOpen = 0.0;        ///< 嘴张开程度 [0,1]，驱动 paintEvent 嘴形
    qreal m_chewScale = 1.0;        ///< 咀嚼挤压系数（垂直方向），1 为常态
    qreal m_starProgress = 0.0;     ///< 左眼星星闪烁进度 [0,1]，0 为无星星
    bool m_eyesClosed = false;      ///< 眨眼瞬间为 true
    PetStyle m_style = PetStyle::Cat; ///< 当前外观样式
    QPointer<QAbstractAnimation> m_mouthAnim; ///< 进行中的嘴部/咀嚼动画（新动画开始前 stop，避免叠加互相覆写）

    QString m_customPath;           ///< 自定义素材路径（空表示使用内置造型）
    QPixmap m_customImage;          ///< 自定义静态素材图像（GIF 模式为空）
    QPointer<QMovie> m_movie;       ///< GIF 播放器（静态素材/内置造型时为 nullptr）

    bool m_pressing = false;        ///< 左键按下中
    bool m_dragging = false;        ///< 已判定为拖动（移动距离超阈值）
    QPoint m_dragOffset;            ///< 按下点相对窗口左上角的偏移
    QPoint m_pressGlobalPos;        ///< 按下时的全局坐标（拖动阈值判定用）
};

#endif // PETWINDOW_H
