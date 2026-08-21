#ifndef ANNOTATIONSHORTCUTCONTROLLER_H
#define ANNOTATIONSHORTCUTCONTROLLER_H

#include <QList>
#include <QKeySequence>
#include <functional>

#include "IShortcutHandler.h"

class QWidget;
class QShortcut;

/**
 * @file AnnotationShortcutController.h
 * @brief 标注快捷键控制器（策略模式 + 模板方法 + 统一 QShortcut 实现）
 *
 * 统一管理 SnipScreen 和 PinWindow 的标注快捷键，消除两处重复实现，
 * 并修复 PinWindow 使用 keyPressEvent 时的焦点时序 bug。
 *
 * 核心改进：
 * - SnipScreen 和 PinWindow 均实现 IShortcutHandler 接口
 * - PinWindow 放弃脆弱的 keyPressEvent 方案，统一用 QShortcut
 * - 文本编辑冲突通过 m_bareKeyShortcuts 列表统一禁用/启用
 * - 画笔宽度范围常量化（kMinPenWidth=1, kMaxPenWidth=20）
 * - canAnnotate() 前置检查统一在控制器完成，实现类无需在每个动作中重复判断
 *
 * 生命周期：
 * - SnipScreen: 构造时创建，析构时销毁（成员变量）
 * - PinWindow: enterAnnotationMode 时创建，exitAnnotationMode 时销毁（堆对象）
 *
 * @author chiangyang
 */

/**
 * @brief 标注快捷键控制器
 *
 * 持有一个 IShortcutHandler 策略引用（SnipScreen 或 PinWindow），
 * 在指定父窗口上注册全部标注快捷键，并统一处理文本编辑冲突。
 *
 * 典型用法：
 * @code
 *   // SnipScreen 构造时
 *   m_annotationController = new AnnotationShortcutController(this, this);
 *
 *   // PinWindow 进入标注模式时
 *   m_annotationController = new AnnotationShortcutController(this, this);
 *   // 退出标注模式时
 *   delete m_annotationController;
 *     @endcode
 *
 * @author chiangyang
 */
class AnnotationShortcutController {
public:
    /// 画笔宽度最小值（与子工具栏滑块范围一致）
    static constexpr int kMinPenWidth = 1;
    /// 画笔宽度最大值（与子工具栏滑块范围一致）
    static constexpr int kMaxPenWidth = 20;

    /**
     * @brief 构造函数
     * @param parent 父窗口（QShortcut 的 parent，决定快捷键作用域为父窗口激活时）
     * @param handler 快捷键动作处理器（SnipScreen 或 PinWindow，实现 IShortcutHandler）
     *
     * 构造时立即调用 registerAll() 注册全部标注快捷键。
     * parent 和 handler 必须非空（内部 Q_ASSERT 校验）。
     * @author chiangyang
     */
    AnnotationShortcutController(QWidget* parent, IShortcutHandler* handler);

    /**
     * @brief 禁用析构函数（控制器不可拷贝）
     *
     * 内部持有 QShortcut 列表和裸指针，拷贝会导致双重释放。
     * @author chiangyang
     */
    AnnotationShortcutController(const AnnotationShortcutController&) = delete;

    /**
     * @brief 禁用赋值（控制器不可拷贝）
     * @author chiangyang
     */
    AnnotationShortcutController& operator=(const AnnotationShortcutController&) = delete;

    /**
     * @brief 析构函数
     *
     * 调用 unregisterAll() 注销所有快捷键。
     * QShortcut 以 parent 为父对象理论上随父窗口销毁，
     * 但显式 unregisterAll 可保证在父窗口销毁前释放资源，顺序更可控。
     * @author chiangyang
     */
    ~AnnotationShortcutController();

    /**
     * @brief 启用/禁用裸键快捷键（文本编辑冲突处理）
     * @param enabled true=启用标注快捷键，false=禁用（交给文本框）
     *
     * 文本编辑框获得焦点时调用 setBareKeysEnabled(false)，禁用数字键/[/]/Tab/Delete/Backspace/Ctrl+C，
     * 使按键事件能正常传递给 OverlayTextEdit 进行文本输入和复制。
     * 编辑框关闭时调用 setBareKeysEnabled(true) 恢复标注快捷键。
     * @note 使用位置: OverlayTextEdit focusIn/focusOut、文本编辑 ESC 取消后恢复
     * @author chiangyang
     */
    void setBareKeysEnabled(bool enabled);

    /**
     * @brief 注销所有快捷键
     *
     * 遍历 m_allShortcuts 逐个 delete，并清空两个列表。
     * PinWindow 退出标注模式时调用，避免快捷键在非标注模式下误触发。
     * @author chiangyang
     */
    void unregisterAll();

private:
    /**
     * @brief 注册单个快捷键
     * @param key 快捷键序列
     * @param slot 回调方法
     * @param isBareKey 是否为裸键（需纳入冲突管理列表），默认 false
     * @param checkCanAnnotate 是否在触发前做 canAnnotate() 前置检查，默认 true
     * @return 创建的 QShortcut 指针
     *
     * 创建 QShortcut 并设置 context 为 Qt::WindowShortcut（仅在父窗口激活时生效）。
     * activated 信号内部根据 checkCanAnnotate 决定是否做 canAnnotate() 前置检查，
     * 通过后再调用 slot。
     * @note Esc/F5 等需在任意状态触发的快捷键应传 checkCanAnnotate=false
     * @author chiangyang
     */
    QShortcut* registerShortcut(const QKeySequence& key,
                                std::function<void()> slot,
                                bool isBareKey = false,
                                bool checkCanAnnotate = true);

    /**
     * @brief 注册所有标注快捷键（模板方法：固定注册顺序骨架）
     *
     * 固定注册顺序便于维护和排查：
     * 1. 操作类（带修饰键 + Esc + F5）
     *    - Ctrl+C 复制（裸键：文本编辑时让给 OverlayTextEdit；做 canAnnotate 检查）
     *    - Ctrl+S 保存 / Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z 撤销重做（做 canAnnotate 检查）
     *    - Esc 取消（不做 canAnnotate 检查，任意状态可触发）
     *    - F5 刷新（不做 canAnnotate 检查，仅 SnipScreen 有效）
     * 2. 工具切换类（数字键 1-8，裸键，做 canAnnotate 检查）
     * 3. 工具调整类（[/]/Tab/Delete/Backspace，裸键，做 canAnnotate 检查）
     *
     * @note 未来若需扩展（如自定义快捷键），可在子类 override 此方法
     * @author chiangyang
     */
    void registerAll();

    QWidget* m_parent;                       ///< 父窗口（QShortcut 的 parent）
    IShortcutHandler* m_handler;             ///< 动作处理器（策略引用）
    QList<QShortcut*> m_allShortcuts;        ///< 所有快捷键（用于 unregisterAll）
    QList<QShortcut*> m_bareKeyShortcuts;    ///< 裸键快捷键子集（文本编辑时禁用）
};

#endif // ANNOTATIONSHORTCUTCONTROLLER_H
