#ifndef OVERLAYTEXTEDIT_H
#define OVERLAYTEXTEDIT_H

#include "FloatingWidget.h"
#include <QToolButton>
#include <QGraphicsOpacityEffect>

/**
 * @brief 覆盖层文本编辑类
 *
 * 浮动文本编辑框（FloatingWidget 交互层）+ 文本特有职责：
 * 字体/颜色样式、内容自适应尺寸、字体随角缩放等比缩放、
 * 旋转子系统（旋转按钮 + 实时预览 + 透明度切换）。
 * @author chiangyang
 */
class OverlayTextEdit : public FloatingWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父控件
     * @author chiangyang
     */
    explicit OverlayTextEdit(QWidget *parent = nullptr);

    /**
     * @brief 析构函数
     * @author chiangyang
     */
    ~OverlayTextEdit() override;

    /**
     * @brief 获取旋转角度
     * @return 旋转角度（度）
     * @author chiangyang
     */
    qreal rotationDegrees() const;

    /**
     * @brief 调整文本框大小以适应内容
     * @author chiangyang
     */
    void adjustSizeToContent();

    /**
     * @brief 设置编辑器字体大小
     * @param px 像素大小
     * @author chiangyang
     */
    void setFontSize(int px);

    /**
     * @brief 获取当前字体大小
     * @return 像素大小
     * @author chiangyang
     */
    int fontSize() const;

    /**
     * @brief 设置编辑器颜色
     * @param color 颜色
     * @author chiangyang
     */
    void setEditorColor(const QColor &color);

protected:
    /**
     * @brief geometryChanged 载荷的旋转角度
     * @author chiangyang
     */
    qreal currentRotation() const override;

    /**
     * @brief 角缩放开始：快照字体原值作为等比缩放基准
     * @author chiangyang
     */
    void onFloatingResizeStarted() override;

    /**
     * @brief 角缩放：按对角线比例缩放字体
     * @author chiangyang
     */
    void onFloatingScaled(qreal diagScaleFactor) override;

    QRectF handleOverlayFrame() const override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @brief 更新样式表
     * @author chiangyang
     */
    void updateStyleSheet();

    /**
     * @brief 更新旋转按钮位置
     * @author chiangyang
     */
    void updateRotateButtonPosition();

    /**
     * @brief 更新旋转预览
     * @author chiangyang
     */
    void updateRotationPreview();

    static constexpr int kMinFontSize = 8;

    int m_fontSize;                     ///< 当前字体像素大小
    int m_origFontSize;                 ///< 角缩放起始字体大小
    QColor m_editorColor;               ///< 当前编辑器颜色

    QToolButton *rotateButton;          ///< 旋转按钮
    bool m_isRotating;                  ///< 是否正在旋转
    bool m_rotationPreviewActive;       ///< 旋转预览是否激活
    QPointF m_rotateCenterGlobal;       ///< 旋转中心全局坐标
    QPointF m_rotatePressGlobal;        ///< 旋转按下时全局坐标
    qreal m_currentRotationDegrees;     ///< 当前旋转角度

    QGraphicsOpacityEffect *m_editorOpacityEffect; ///< 编辑器透明度效果
    QWidget *m_rotationPreview;         ///< 旋转预览窗口
};

#endif // OVERLAYTEXTEDIT_H
