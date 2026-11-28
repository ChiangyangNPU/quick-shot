#ifndef COLORLOUPE_H
#define COLORLOUPE_H

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

/**
 * @brief 取色复制格式枚举
 * @author chiangyang
 */
enum class ColorFormat {
    Hex, ///< #RRGGBB
    Rgb, ///< rgb(r, g, b)
    Hsl, ///< hsl(h, s%, l%)
};

/**
 * @brief ColorFormat 与配置字符串互转（QSettings 持久化用）
 * @author chiangyang
 */
inline QString colorFormatToString(ColorFormat format) {
    switch (format) {
    case ColorFormat::Rgb: return QStringLiteral("rgb");
    case ColorFormat::Hsl: return QStringLiteral("hsl");
    case ColorFormat::Hex: break;
    }
    return QStringLiteral("hex");
}

/**
 * @brief 配置字符串转 ColorFormat，无法识别时回退 HEX
 * @author chiangyang
 */
inline ColorFormat colorFormatFromString(const QString &str) {
    if (str == QLatin1String("rgb")) return ColorFormat::Rgb;
    if (str == QLatin1String("hsl")) return ColorFormat::Hsl;
    return ColorFormat::Hex;
}

/**
 * @brief 按格式输出颜色字符串
 *
 * Hex: #RRGGBB；Rgb: rgb(255, 87, 51)；Hsl: hsl(11, 100%, 60%)
 * @author chiangyang
 */
inline QString colorToString(const QColor &color, ColorFormat format) {
    switch (format) {
    case ColorFormat::Hex:
        return QString::asprintf("#%02X%02X%02X", color.red(), color.green(), color.blue());
    case ColorFormat::Rgb:
        return QStringLiteral("rgb(%1, %2, %3)")
            .arg(color.red()).arg(color.green()).arg(color.blue());
    case ColorFormat::Hsl: {
        // QColor::hslHue() 对无彩色（灰/黑/白）返回 -1，此时色相约定为 0
        const int h = qMax(0, color.hslHue());
        return QStringLiteral("hsl(%1, %2%, %3%)")
            .arg(h)
            .arg(qRound(color.hslSaturationF() * 100))
            .arg(qRound(color.lightnessF() * 100));
    }
    }
    return QString();
}

/**
 * @brief 取色放大镜控件（独立取色器与截图选区共用）
 *
 * 显示光标附近源像素的最近邻放大图 + 中心取色像素高亮框，
 * 下方信息条显示色块与当前格式的颜色字符串。
 *
 * 用法：setColorSource() 传入冻结截图及其覆盖的全局几何后，
 * 每次鼠标移动调用 setCursorGlobal()（全局坐标），控件自行采样、
 * 翻转定位与重绘；控件对鼠标事件透明，不影响宿主交互。
 *
 * 坐标约定与 SnipScreen::grabVirtualDesktop 一致：全局坐标为逻辑坐标
 * （禁用 Qt 高 DPI 缩放后即物理像素），源图像按 devicePixelRatio 换算。
 * @author chiangyang
 */
class ColorLoupe : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 宿主控件（应为覆盖屏幕/虚拟桌面的窗口）
     * @author chiangyang
     */
    explicit ColorLoupe(QWidget *parent = nullptr);

    /**
     * @brief 设置取色源（冻结截图及其覆盖的全局几何）
     *
     * pixmap 需按 SnipScreen::grabVirtualDesktop 的约定设置 devicePixelRatio
     * （Windows BitBlt 为 1.0，macOS grabWindow 为所在屏 DPR）。
     * 传空 QPixmap 表示禁用（放大镜不再显示）。
     * @param pixmap 冻结截图
     * @param globalGeometry 截图覆盖的全局几何
     * @author chiangyang
     */
    void setColorSource(const QPixmap &pixmap, const QRect &globalGeometry);

    /**
     * @brief 更新光标全局位置：采样 + 重新定位 + 重绘
     *
     * 光标超出取色源范围时隐藏放大镜。
     * @param globalPos 光标全局坐标
     * @author chiangyang
     */
    void setCursorGlobal(const QPoint &globalPos);

    /**
     * @brief 是否已设置取色源
     * @author chiangyang
     */
    bool hasSource() const { return m_hasSource; }

    /**
     * @brief 当前光标处是否取到有效颜色
     * @author chiangyang
     */
    bool hasValidColor() const { return m_valid; }

    /**
     * @brief 当前取到的颜色
     * @author chiangyang
     */
    QColor currentColor() const { return m_color; }

    /**
     * @brief 当前格式的颜色字符串
     * @author chiangyang
     */
    QString colorString() const { return colorToString(m_color, m_format); }

    /**
     * @brief 当前颜色格式
     * @author chiangyang
     */
    ColorFormat format() const { return m_format; }

    /**
     * @brief 放大内容区矩形（控件内坐标，即整张正方形卡片）
     *
     * 供测试断言与宿主特殊定位使用。
     * @author chiangyang
     */
    QRect loupeContentRect() const;

    /**
     * @brief 设置信息条第二行的操作提示文字（如"滚轮 切换格式 · 单击 复制"）
     *
     * 两种使用场景的提示不同：独立取色器与截图选区分别由宿主设置。
     * @param text 提示文字（空串隐藏提示行）
     * @author chiangyang
     */
    void setHintText(const QString &text);

    /**
     * @brief 设置信息条第三行的 Esc 提示文字（如"Esc 退出"）
     *
     * 独立取色器为"Esc 退出"，截图选区为"Esc 取消截图"。
     * @param text 提示文字（空串隐藏提示行）
     * @author chiangyang
     */
    void setEscHint(const QString &text);

public slots:
    /**
     * @brief 切换到下一种颜色格式（Hex -> Rgb -> Hsl 回绕），并持久化到配置
     * @author chiangyang
     */
    void cycleFormat();

signals:
    /**
     * @brief 颜色格式变化（宿主如需联动可监听）
     * @param format 新格式
     * @author chiangyang
     */
    void formatChanged(ColorFormat format);

protected:
    /**
     * @brief 绘制放大区、像素网格、中心取色框与信息条
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 依据光标位置计算自身位置，超出宿主边界时向内翻转
     * @author chiangyang
     */
    void reposition();

    QImage m_source;          ///< 取色源图像（物理像素）
    QRect m_sourceGeometry;   ///< 源图像覆盖的全局几何（逻辑坐标）
    qreal m_dpr = 1.0;        ///< 源图像的设备像素比
    QPoint m_cursor;          ///< 最近一次光标全局位置
    QColor m_color;           ///< 当前取到的颜色
    bool m_hasSource = false; ///< 是否已设置取色源
    bool m_valid = false;     ///< 光标是否位于取色源范围内
    ColorFormat m_format = ColorFormat::Hex;
    QString m_hint;           ///< 信息条第二行的操作提示文字
    QString m_escHint;        ///< 信息条第三行的 Esc 提示文字
};

#endif // COLORLOUPE_H
