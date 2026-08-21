#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H

// Windows 头文件中的 MessageBox 宏会覆盖类名，需要先取消定义
#ifdef _WIN32
#undef MessageBox
#endif

#include <QMessageBox>
#include <QPushButton>
#include <QRect>

/**
 * @brief 自定义消息框控件
 *
 * 继承自 QMessageBox，集中封装项目消息框的「一致性」配置：构造时自动应用
 * StyleManager::getMessageBoxStyle() 样式与 StyleManager::loadAppIcon() 标题栏图标，
 * 避免在各业务代码中重复书写样式表、图标与按钮翻译样板。
 *
 * 提供两层 API：
 * - 静态便捷方法 information/warning/critical/question，覆盖最常见的提示与确认场景，
 *   一行调用即可完成「加翻译按钮 + exec」流程；
 * - 实例 API（addOkButton/addYesNoButtons/addCustomButton/centerOn/setType/setContent），
 *   覆盖复杂场景（复选框、自定义按钮、居中定位、嵌套弹窗等）。
 *
 * 继承得到的 exec()/clickedButton()/setCheckBox()/setIcon()/setText()/setWindowTitle()
 * 等原生方法均可用，保证与现有 QMessageBox 代码完全兼容。
 * @author chiangyang
 */
class MessageBox : public QMessageBox {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     *
     * 自动应用项目消息框样式表与应用图标
     * @param parent 父窗口，允许为 nullptr（无父窗口场景）
     * @author chiangyang
     */
    explicit MessageBox(QWidget *parent = nullptr);

    // ------------------------------------------------------------------
    // 实例便捷 API（复杂场景使用）
    // ------------------------------------------------------------------

    /**
     * @brief 设置消息框图标类型（对 setIcon 的语义化包装）
     * @param icon 图标类型（Information/Warning/Critical/Question/NoIcon）
     * @author chiangyang
     */
    void setType(QMessageBox::Icon icon);

    /**
     * @brief 同时设置窗口标题与正文文本
     * @param title 窗口标题
     * @param text 正文文本
     * @author chiangyang
     */
    void setContent(const QString &title, const QString &text);

    /**
     * @brief 添加「确定」按钮（自动翻译为 tm->get("ok", "OK")）
     * @return 按钮指针，供 exec() 后用 clickedButton() 判断
     * @author chiangyang
     */
    QPushButton* addOkButton();

    /**
     * @brief 添加「是」按钮（自动翻译为 tm->get("yes", "Yes")）
     * @return 按钮指针，供 exec() 后用 clickedButton() 判断
     * @author chiangyang
     */
    QPushButton* addYesButton();

    /**
     * @brief 添加「否」按钮（自动翻译为 tm->get("no", "No")）
     * @return 按钮指针，供 exec() 后用 clickedButton() 判断
     * @author chiangyang
     */
    QPushButton* addNoButton();

    /**
     * @brief 同时添加「是」「否」按钮（自动翻译），并设置默认按钮
     * @param yesBtn 输出参数，接收「是」按钮指针，可为 nullptr
     * @param noBtn 输出参数，接收「否」按钮指针，可为 nullptr
     * @param defaultNo 是否将「否」设为默认按钮（默认 true，符合破坏性操作需二次确认的惯例）
     * @author chiangyang
     */
    void addYesNoButtons(QPushButton **yesBtn = nullptr,
                         QPushButton **noBtn = nullptr,
                         bool defaultNo = true);

    /**
     * @brief 添加自定义文本按钮（如「详细信息」ActionRole 按钮）
     * @param text 按钮文本
     * @param role 按钮角色
     * @return 按钮指针，供 exec() 后用 clickedButton() 判断
     * @author chiangyang
     */
    QPushButton* addCustomButton(const QString &text, QMessageBox::ButtonRole role);

    /**
     * @brief 将弹窗居中定位到指定矩形（如截图选区、父窗口几何）
     *
     * 内部用 QTimer::singleShot(0) 延迟到事件循环开始、布局完成后再移动，
     * 确保弹窗尺寸已确定
     * @param rect 目标矩形（屏幕坐标），无效矩形则不做定位
     * @author chiangyang
     */
    void centerOn(const QRect &rect);

    // ------------------------------------------------------------------
    // 静态便捷方法（最常见场景，一行调用）
    // ------------------------------------------------------------------

    /**
     * @brief 显示信息提示框（Information 图标 + 「确定」按钮 + exec）
     * @param parent 父窗口，可为 nullptr
     * @param title 窗口标题
     * @param text 正文文本
     * @author chiangyang
     */
    static void information(QWidget *parent, const QString &title, const QString &text);

    /**
     * @brief 显示警告提示框（Warning 图标 + 「确定」按钮 + exec）
     * @param parent 父窗口，可为 nullptr
     * @param title 窗口标题
     * @param text 正文文本
     * @author chiangyang
     */
    static void warning(QWidget *parent, const QString &title, const QString &text);

    /**
     * @brief 显示错误提示框（Critical 图标 + 「确定」按钮 + exec）
     * @param parent 父窗口，可为 nullptr
     * @param title 窗口标题
     * @param text 正文文本
     * @author chiangyang
     */
    static void critical(QWidget *parent, const QString &title, const QString &text);

    /**
     * @brief 显示 Yes/No 询问框（Question 图标 + 「是/否」按钮 + exec）
     *
     * 「否」为默认按钮，点击「是」返回 true，其余（点「否」或关闭）返回 false
     * @param parent 父窗口，可为 nullptr
     * @param title 窗口标题
     * @param text 正文文本
     * @param defaultNo 是否将「否」设为默认按钮（默认 true）
     * @return 用户是否点击了「是」
     * @author chiangyang
     */
    static bool question(QWidget *parent, const QString &title, const QString &text,
                         bool defaultNo = true);

private:
    /**
     * @brief 应用项目一致样式与图标
     *
     * 由构造函数调用，集中样式表与标题栏图标的设置
     * @author chiangyang
     */
    void applyProjectStyle();

protected:
    /**
     * @brief 显示事件处理
     *
     * 在弹窗首次显示时，若设置了有效的居中目标矩形，则将弹窗移动到该矩形中心
     * @param event 显示事件
     * @author chiangyang
     */
    void showEvent(QShowEvent *event) override;

private:
    QRect m_centerRect; ///< 居中目标矩形（屏幕坐标），无效则不定位
};

#endif // MESSAGEBOX_H
