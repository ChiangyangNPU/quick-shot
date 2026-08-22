#ifndef BASETOOLBAR_H
#define BASETOOLBAR_H

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QColor>
#include <QComboBox>
#include <QMap>
#include <QList>
#include <vector>

class QSlider;

/**
 * @brief 工具设置结构体
 *
 * 保存每个工具独立的颜色、粗细、字体大小等设置
 * @author chiangyang
 */
struct ToolSettings {
    QColor color = Qt::red;        ///< 画笔颜色
    int penWidth = 5;              ///< 画笔粗细（默认值，最大值的1/4）
    int fontSize = 28;             ///< 字体大小（默认中间值）
    bool penWidthModified = false; ///< 画笔粗细是否被用户修改过
    bool fontSizeModified = false; ///< 字体大小是否被用户修改过
};

/**
 * @brief 标注工具定义结构体
 *
 * 统一的标注工具元数据，用于创建和更新标注按钮
 * @author chiangyang
 */
struct AnnotationToolDef {
    int toolId;
    QString iconPath;
};

/**
 * @brief 基础工具栏类
 *
 * 提供工具栏的通用功能，如子工具栏的显示、颜色调色板的添加等
 * @author chiangyang
 */
class BaseToolBar : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     * @author chiangyang
     */
    explicit BaseToolBar(QWidget *parent = nullptr);

    /**
     * @brief 重新翻译UI文本
     * @author chiangyang
     */
    virtual void retranslateUi();

    /**
     * @brief 获取子工具栏窗口
     * @return 子工具栏窗口指针
     * @author chiangyang
     */
    QWidget *getSubToolbarWindow() const;

    /**
     * @brief 更新背景样式
     * @author chiangyang
     */
    void updateBackgroundStyle();

    /**
     * @brief 更新工具栏按钮状态
     * @param hasSelection 是否有选区
     * @param canUndo 是否可撤销
     * @param canRedo 是否可重做
     * @author chiangyang
     */
    void updateState(bool hasSelection, bool canUndo = false, bool canRedo = false);

    /**
     * @brief 取消所有标注工具按钮的选中状态
     * @author chiangyang
     */
    void uncheckAllAnnotationBtns();

    /**
     * @brief 按 toolId 选中标注按钮
     * @param toolId 标注工具 ID（AnnotationType 枚举值）
     * @author chiangyang
     */
    void selectAnnotationTool(int toolId);

    /**
     * @brief 设置当前工具的画笔宽度（供 [/] 快捷键调用）
     *
     * 更新 m_toolSettings 中当前工具的画笔宽度，同步子工具栏滑块显示
     * （blockSignals 防止 penWidthChanged 重入），再发射 penWidthChanged
     * 让 SnipScreen 同步 m_currentPenWidth。
     * @param width 新的画笔宽度
     * @author chiangyang
     */
    void setCurrentPenWidth(int width);

    /**
     * @brief 设置当前工具的画笔颜色（供 Tab 快捷键循环切色调用）
     *
     * 更新 m_toolSettings 中当前工具的颜色，同步颜色面板选中态
     * （重置所有颜色按钮为未选中，匹配色设为选中），再发射 penColorChanged
     * 让 SnipScreen 同步 m_currentColor。
     * @param color 新的画笔颜色
     * @author chiangyang
     */
    void setCurrentColor(const QColor &color);

    /**
     * @brief 切换到颜色面板中的下一个颜色（供 Tab 快捷键调用）
     *
     * 基于颜色面板 m_colorBtns 列表，从当前选中按钮 m_selectedColorBtn 的下一个开始，
     * 末尾回绕到首部，实现按面板顺序循环切换全部预设颜色。内部复用 setCurrentColor
     * 同步选中态并发射 penColorChanged，SnipScreen 通过信号同步 m_currentColor。
     * @author chiangyang
     */
    void selectNextColor();

    /**
     * @brief 更新形状图标/文字
     * @param shapeType 形状类型（1=矩形, 2=椭圆, 3=三角形）
     * @author chiangyang
     */
    void updateShapeIcon(int shapeType);

    /**
     * @brief 更新按钮样式（文字/图标模式切换，子类必须实现）
     * @author chiangyang
     */
    virtual void updateButtonStyles() = 0;

    /**
     * @brief 根据配置刷新工具默认值
     *
     * 从 StyleManager 读取当前默认画笔粗细和字号，
     * 更新所有工具的初始设置。
     * @author chiangyang
     */
    void refreshDefaultValues();

    /**
     * @brief 获取指定工具的当前画笔粗细
     * @param toolId 工具ID
     * @return 画笔粗细值
     * @author chiangyang
     */
    int getToolPenWidth(int toolId) const;

    /**
     * @brief 获取指定工具的当前字号
     * @param toolId 工具ID
     * @return 字号值
     * @author chiangyang
     */
    int getToolFontSize(int toolId) const;

signals:
    // --- 工具信号 ---
    /**
     * @brief 工具被选中
     * @param toolId 工具ID（AnnotationType 枚举值）
     * @author chiangyang
     */
    void toolSelected(int toolId);

    /**
     * @brief 标注工具取消选中
     * @author chiangyang
     */
    void annotationToolDeselected();

    /**
     * @brief 形状类型变更
     * @param type 形状类型（1=矩形, 2=椭圆, 3=三角形）
     * @author chiangyang
     */
    void shapeTypeChanged(int type);

    /**
     * @brief 画笔宽度变更
     * @param width 画笔宽度
     * @author chiangyang
     */
    void penWidthChanged(int width);

    /**
     * @brief 画笔颜色变更
     * @param color 新颜色
     * @author chiangyang
     */
    void penColorChanged(const QColor &color);

    /**
     * @brief 字体大小变更
     * @param size 字体大小
     * @author chiangyang
     */
    void fontSizeChanged(int size);

    /**
     * @brief 工具画笔粗细变更（用于同步设置窗口默认值显示）
     * @param width 新的画笔粗细值
     * @author chiangyang
     */
    void toolPenWidthChanged(int width);

    /**
     * @brief 工具字号变更（用于同步设置窗口默认值显示）
     * @param size 新的字号值
     * @author chiangyang
     */
    void toolFontSizeChanged(int size);

    /**
     * @brief 橡皮擦粗细变更（用于同步设置窗口默认值显示）
     * @param width 新的橡皮擦粗细值
     * @author chiangyang
     */
    void toolEraserWidthChanged(int width);

    /**
     * @brief 马赛克大小变更（用于同步设置窗口默认值显示）
     * @param size 新的马赛克大小值
     * @author chiangyang
     */
    void toolMosaicSizeChanged(int size);

    // --- 操作信号 ---
    /**
     * @brief 请求开始录屏
     * @author chiangyang
     */
    void recordRequested();

    /**
     * @brief 请求截图
     * @author chiangyang
     */
    void screenshotRequested();

    /**
     * @brief 请求取消录屏
     * @author chiangyang
     */
    void cancelRecordRequested();

    /**
     * @brief 请求清除所有标注
     * @author chiangyang
     */
    void clearRequested();

    /**
     * @brief 请求撤销
     * @author chiangyang
     */
    void undoRequested();

    /**
     * @brief 请求重做
     * @author chiangyang
     */
    void redoRequested();

    /**
     * @brief 请求录制中快照
     * @author chiangyang
     */
    void snapshotRequested();

    /**
     * @brief 请求 OCR 识别
     * @author chiangyang
     */
    void ocrRequested();

    /**
     * @brief 请求翻译
     * @author chiangyang
     */
    void translateRequested();

protected:
    /**
     * @brief 设置UI布局（子类实现）
     * @author chiangyang
     */
    virtual void setupUi() = 0;

    /**
     * @brief 绘制事件：主动绘制圆角矩形背景
     *
     * 当设置 WA_TranslucentBackground 后，QSS 的 background-color 不会自动绘制，
     * 因此用 QPainter 主动绘制圆角矩形背景，保证圆角外区域透明且圆角内有底色。
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override;

    // --- 子工具栏相关 ---
    /**
     * @brief 显示子工具栏
     *
     * 根据工具ID显示对应的子工具栏（如形状选择、颜色面板等），
     * 子工具栏以独立弹出窗口形式显示在主工具栏上方。
     *
     * @param toolId 触发显示的工具ID
     * @author chiangyang
     */
    void showSubTools(int toolId);

    /**
     * @brief 在指定布局中添加颜色调色板
     *
     * 创建一组预设颜色按钮（红/蓝/黑/黄/绿/白）和自定义取色按钮，
     * 添加到目标布局中。点击颜色按钮时更新对应工具的画笔颜色。
     *
     * @param layout 目标布局
     * @author chiangyang
     */
    void addColorPalette(QLayout *layout);

    /**
     * @brief 创建标注工具按钮组
     * @author chiangyang
     */
    void createAnnotationTools();

    /**
     * @brief 在布局中添加垂直分隔线
     * @param layout 目标布局
     * @author chiangyang
     */
    void addSeparator(QHBoxLayout *layout);

    /**
     * @brief 应用单个按钮的文字/图标样式
     * @param btn 按钮
     * @param iconPath 图标路径
     * @param text 文字
     * @param isIcon 是否为图标模式
     * @author chiangyang
     */
    void applyButtonStyle(QPushButton *btn, const QString &iconPath,
                          const QString &text, bool isIcon);

    /**
     * @brief 更新所有标注工具按钮的文字/图标样式
     * @param isIcon 是否为图标模式
     * @author chiangyang
     */
    void updateAnnotationButtonStyles(bool isIcon);

    /**
     * @brief 重新翻译标注工具按钮
     * @author chiangyang
     */
    void retranslateAnnotationButtons();

    /**
     * @brief 获取主布局
     * @return 主布局
     * @author chiangyang
     */
    QVBoxLayout *getMainLayout() const;

    /**
     * @brief 事件过滤器
     * @author chiangyang
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    // --- 子工具栏预创建（消除切换闪烁 / 压缩）相关 ---
    /**
     * @brief 子工具栏变体枚举
     *
     * 子工具栏只有 4 种内部结构，多个标注工具可共享同一种变体。
     * 热切换时变体窗口保持常驻，仅在 show/hide 间切换，避免 Windows 顶层窗口重建闪烁。
     * @author chiangyang
     */
    enum SubToolVariant {
        VariantRectangle = 0,  ///< 形状下拉 + 线宽滑块 + 颜色面板（RECTANGLE）
        VariantColorPen  = 1,  ///< 线宽滑块 + 颜色面板（ELLIPSE / ARROW / PEN / LINE）
        VariantText      = 2,  ///< 字号滑块 + 颜色面板（TEXT）
        VariantNoColor   = 3,  ///< 线宽滑块（MOSAIC / ERASER，无颜色）
        VariantCount     = 4
    };

    /**
     * @brief 单个变体的内部控件数据
     * @author chiangyang
     */
    struct SubToolVariantData {
        QWidget *window = nullptr;                ///< 变体所在窗口
        QHBoxLayout *layout = nullptr;            ///< 变体布局
        QSlider *slider = nullptr;                ///< 线宽或字号滑块
        QComboBox *shapeCombo = nullptr;          ///< 形状下拉框（仅 Rectangle 变体）
        QList<QPushButton*> colorBtns;            ///< 颜色按钮列表（NoColor 变体为空）
        QPushButton *selectedColorBtn = nullptr;  ///< 当前选中颜色按钮
        bool built = false;                       ///< 是否已完成构建
    };

    /**
     * @brief 工具 ID → 子工具栏变体枚举映射
     * @param toolId 工具 ID
     * @return 对应的 SubToolVariant 枚举值
     * @author chiangyang
     */
    static SubToolVariant variantForToolId(int toolId);

    /**
     * @brief 懒构建指定变体（已构建则直接返回）
     * @param v 变体枚举
     * @author chiangyang
     */
    void buildSubVariant(SubToolVariant v);

    /**
     * @brief 将变体的内部控件指针绑定到当前成员变量（m_penWidthSlider 等）
     * @param v 变体枚举
     * @author chiangyang
     */
    void bindVariantInternals(SubToolVariant v);

    /**
     * @brief 用指定工具的设置同步变体控件状态（滑块值 / 选中色等）
     * @param v 变体枚举
     * @param toolId 工具 ID（取 m_toolSettings[toolId] 为同步源）
     * @author chiangyang
     */
    void syncVariantState(SubToolVariant v, int toolId);

    // --- 子类共享的成员变量 ---
    QVBoxLayout *m_mainLayout = nullptr;
    QHBoxLayout *m_primaryLayout = nullptr;
    QWidget *subToolbarWindow = nullptr;       ///< 当前激活子工具栏窗口（作为外部 getSubToolbarWindow 的别名）
    QHBoxLayout *subToolbarLayout = nullptr;   ///< 当前激活子工具栏布局（同上）

    bool m_isChinese = true;

    int m_currentToolId = -1;
    QMap<int, ToolSettings> m_toolSettings;

    ///< 标注工具按钮列表
    std::vector<QPushButton *> m_annotationBtns;

    ///< 共有操作按钮
    QPushButton *m_undoBtn = nullptr;
    QPushButton *m_redoBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_ocrBtn = nullptr;
    QPushButton *m_translateBtn = nullptr;

private:
    SubToolVariantData m_subVariants[VariantCount];  ///< 4 种子工具栏变体（常驻，切换时不销毁）
    SubToolVariant m_activeSubVariant = VariantCount;///< 当前激活变体，VariantCount 表示未激活

    QPushButton *m_selectedColorBtn = nullptr;
    QSlider *m_penWidthSlider = nullptr;       ///< 当前子工具栏的画笔宽度滑块（变体切换时重绑定）
    QList<QPushButton*> m_colorBtns;           ///< 当前子工具栏的颜色按钮列表（变体切换时重绑定）
};

#endif // BASETOOLBAR_H
