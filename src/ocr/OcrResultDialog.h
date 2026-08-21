#ifndef OCR_RESULT_DIALOG_H
#define OCR_RESULT_DIALOG_H

#include <QWidget>
#include <QTextEdit>
#include <QEvent>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>

#include "OcrEngine.h"
#include "../translate/TranslateEngine.h"

/**
 * @brief OCR 结果弹窗类
 *
 * 显示 OCR 识别结果，提供复制和关闭功能。
 * 支持鼠标拖动窗口移动，支持四角和边缘拖拽调整大小。
 * @author chiangyang
 */
class OcrResultDialog : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param result OCR 识别结果
     * @param parent 父窗口
     * @author chiangyang
     */
    explicit OcrResultDialog(const OcrEngine::OcrResult &result, QWidget *parent = nullptr);

    /**
     * @brief 更新界面语言
     * @author chiangyang
     */
    void retranslateUi();

protected:
    /**
     * @brief 鼠标按下事件，用于开始拖动窗口或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件，用于拖动窗口或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件，结束拖动或调整大小
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 事件过滤器，处理子控件上的鼠标移动事件
     * @param obj 监听对象
     * @param event 事件
     * @return 是否拦截事件
     * @author chiangyang
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

    /**
     * @brief 显示事件，macOS 下将窗口级别提升到截图窗口之上
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * @brief 文本视图模式枚举
     * @author chiangyang
     */
    enum class ViewMode {
        Original,    ///< 原文
        Translation, ///< 译文
        Both         ///< 对照
    };

    /**
     * @brief 调整大小的边缘类型枚举
     * @author chiangyang
     */
    enum class ResizeEdge {
        None,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    /**
     * @brief 获取指定点的边缘类型
     * @param pos 窗口内坐标
     * @return 边缘类型
     * @author chiangyang
     */
    ResizeEdge edgeAt(const QPoint &pos) const;

    /**
     * @brief 根据边缘类型获取对应光标
     * @param edge 边缘类型
     * @return 光标形状
     * @author chiangyang
     */
    Qt::CursorShape cursorForEdge(ResizeEdge edge) const;

    /**
     * @brief 设置 UI 布局
     * @author chiangyang
     */
    void setupUi();

    /**
     * @brief 复制文本到剪贴板（按当前视图模式复制对应内容）
     * @author chiangyang
     */
    void copyToClipboard();

    /**
     * @brief 触发翻译
     * @author chiangyang
     */
    void onTranslate();

    /**
     * @brief 根据当前视图模式更新文本显示
     * @author chiangyang
     */
    void updateView();

    /**
     * @brief 视图模式下拉框索引改变槽函数
     * @param index 下拉框索引
     * @author chiangyang
     */
    void onViewModeChanged(int index);

    /**
     * @brief 翻译完成槽函数
     * @param original 原文
     * @param translated 译文
     * @author chiangyang
     */
    void onTranslateFinished(const QString &original, const QString &translated);

    /**
     * @brief 翻译失败槽函数
     * @param code 错误分类码，据此显示本地化文案
     * @param detail 原始技术细节，显示在详细信息中
     * @author chiangyang
     */
    void onTranslateFailed(TranslateEngine::TranslateError code, const QString &detail);

    static constexpr int kEdgeMargin = 10;  ///< 边缘检测像素范围

    QTextEdit *m_textEdit;       ///< 文本显示区域
    QPushButton *m_copyButton;   ///< 复制按钮
    QPushButton *m_closeButton;  ///< 关闭按钮
    QPushButton *m_translateButton; ///< 翻译按钮
    QComboBox *m_viewModeCombo;     ///< 视图模式下拉框（原文/译文/对照）
    QString m_fullText;          ///< 完整的识别文本
    QString m_translatedText;    ///< 翻译后的文本
    ViewMode m_viewMode = ViewMode::Original; ///< 当前视图模式
    bool m_translating = false;  ///< 是否正在翻译中

    // 拖动相关
    bool m_isDragging = false;   ///< 是否正在拖动窗口
    QPoint m_dragStartPos;       ///< 拖动起始位置（全局坐标）
    QPoint m_widgetStartPos;    ///< 窗口起始位置（全局坐标）

    // 调整大小相关
    bool m_isResizing = false;   ///< 是否正在调整大小
    ResizeEdge m_resizeEdge = ResizeEdge::None; ///< 当前调整的边缘
    QRect m_startGeometry;       ///< 调整前的窗口几何
    QPoint m_resizeStartPos;     ///< 调整起始位置（全局坐标）
};

#endif // OCR_RESULT_DIALOG_H
