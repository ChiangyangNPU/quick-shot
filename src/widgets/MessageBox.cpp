#include "MessageBox.h"

#include <QCheckBox>
#include <QRect>
#include <QShowEvent>
#include <QTimer>

#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"

/**
 * @brief 构造函数，自动应用项目消息框样式与标题栏图标
 * @param parent 父窗口
 * @author chiangyang
 */
MessageBox::MessageBox(QWidget *parent)
    : QMessageBox(parent)
{
    applyProjectStyle();
}

// ==================== 实例便捷 API ====================

/**
 * @brief 设置消息框图标类型
 * @param icon 图标类型
 * @author chiangyang
 */
void MessageBox::setType(QMessageBox::Icon icon) {
    setIcon(icon);
}

/**
 * @brief 同时设置窗口标题与正文文本
 * @param title 窗口标题
 * @param text 正文文本
 * @author chiangyang
 */
void MessageBox::setContent(const QString &title, const QString &text) {
    setWindowTitle(title);
    setText(text);
}

/**
 * @brief 添加「确定」按钮（自动翻译）
 * @return 按钮指针
 * @author chiangyang
 */
QPushButton* MessageBox::addOkButton() {
    TranslationManager *tm = TranslationManager::instance();
    return addButton(tm->get("ok", "OK"), QMessageBox::AcceptRole);
}

/**
 * @brief 添加「是」按钮（自动翻译）
 * @return 按钮指针
 * @author chiangyang
 */
QPushButton* MessageBox::addYesButton() {
    TranslationManager *tm = TranslationManager::instance();
    return addButton(tm->get("yes", "Yes"), QMessageBox::YesRole);
}

/**
 * @brief 添加「否」按钮（自动翻译）
 * @return 按钮指针
 * @author chiangyang
 */
QPushButton* MessageBox::addNoButton() {
    TranslationManager *tm = TranslationManager::instance();
    return addButton(tm->get("no", "No"), QMessageBox::NoRole);
}

/**
 * @brief 同时添加「是」「否」按钮并设置默认按钮
 * @param yesBtn 输出「是」按钮指针，可为 nullptr
 * @param noBtn 输出「否」按钮指针，可为 nullptr
 * @param defaultNo 是否将「否」设为默认按钮
 * @author chiangyang
 */
void MessageBox::addYesNoButtons(QPushButton **yesBtn, QPushButton **noBtn, bool defaultNo) {
    QPushButton *yes = addYesButton();
    QPushButton *no = addNoButton();
    if (yesBtn) *yesBtn = yes;
    if (noBtn) *noBtn = no;
    setDefaultButton(defaultNo ? no : yes);
}

/**
 * @brief 添加自定义文本按钮
 * @param text 按钮文本
 * @param role 按钮角色
 * @return 按钮指针
 * @author chiangyang
 */
QPushButton* MessageBox::addCustomButton(const QString &text, QMessageBox::ButtonRole role) {
    return addButton(text, role);
}

/**
 * @brief 将弹窗居中定位到指定矩形
 *
 * 仅记录目标矩形，实际移动在 showEvent 中执行（此时弹窗尺寸已确定）
 * @param rect 目标矩形（屏幕坐标），无效则不定位
 * @author chiangyang
 */
void MessageBox::centerOn(const QRect &rect) {
    m_centerRect = rect;
}

// ==================== 静态便捷方法 ====================

/**
 * @brief 显示信息提示框
 * @param parent 父窗口
 * @param title 窗口标题
 * @param text 正文文本
 * @author chiangyang
 */
void MessageBox::information(QWidget *parent, const QString &title, const QString &text) {
    MessageBox box(parent);
    box.setType(QMessageBox::Information);
    box.setContent(title, text);
    box.addOkButton();
    box.exec();
}

/**
 * @brief 显示警告提示框
 * @param parent 父窗口
 * @param title 窗口标题
 * @param text 正文文本
 * @author chiangyang
 */
void MessageBox::warning(QWidget *parent, const QString &title, const QString &text) {
    MessageBox box(parent);
    box.setType(QMessageBox::Warning);
    box.setContent(title, text);
    box.addOkButton();
    box.exec();
}

/**
 * @brief 显示错误提示框
 * @param parent 父窗口
 * @param title 窗口标题
 * @param text 正文文本
 * @author chiangyang
 */
void MessageBox::critical(QWidget *parent, const QString &title, const QString &text) {
    MessageBox box(parent);
    box.setType(QMessageBox::Critical);
    box.setContent(title, text);
    box.addOkButton();
    box.exec();
}

/**
 * @brief 显示 Yes/No 询问框，返回是否点击「是」
 * @param parent 父窗口
 * @param title 窗口标题
 * @param text 正文文本
 * @param defaultNo 是否将「否」设为默认按钮
 * @return 用户是否点击了「是」
 * @author chiangyang
 */
bool MessageBox::question(QWidget *parent, const QString &title, const QString &text,
                          bool defaultNo) {
    MessageBox box(parent);
    box.setType(QMessageBox::Question);
    box.setContent(title, text);
    QPushButton *yesBtn = nullptr;
    QPushButton *noBtn = nullptr;
    box.addYesNoButtons(&yesBtn, &noBtn, defaultNo);
    box.exec();
    return box.clickedButton() == yesBtn;
}

// ==================== 内部实现 ====================

/**
 * @brief 应用项目一致样式与标题栏图标
 *
 * 集中样式表与标题栏图标的设置，避免各业务代码重复书写
 * @author chiangyang
 */
void MessageBox::applyProjectStyle() {
    setStyleSheet(StyleManager::getMessageBoxStyle());
    setWindowIcon(StyleManager::loadAppIcon());
}

/**
 * @brief 显示事件处理
 * @param event 显示事件
 * @author chiangyang
 */
void MessageBox::showEvent(QShowEvent *event) {
    QMessageBox::showEvent(event);
    if (m_centerRect.isValid()) {
        // 延迟到布局完成后再定位，确保弹窗尺寸已确定
        QTimer::singleShot(0, this, [this]() {
            move(m_centerRect.x() + (m_centerRect.width() - width()) / 2,
                 m_centerRect.y() + (m_centerRect.height() - height()) / 2);
        });
    }
}
