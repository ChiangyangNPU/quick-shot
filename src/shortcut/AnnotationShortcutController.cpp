#include "AnnotationShortcutController.h"

#include <QShortcut>
#include <QWidget>
#include <QKeySequence>

#include "../log/Logger.h"

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @param handler 动作处理器
 *
 * 校验 parent 和 handler 非空，初始化成员后立即调用 registerAll()
 * 注册全部标注快捷键。
 * @author chiangyang
 */
AnnotationShortcutController::AnnotationShortcutController(QWidget* parent,
                                                           IShortcutHandler* handler)
    : m_parent(parent)
    , m_handler(handler) {
    Q_ASSERT(parent);
    Q_ASSERT(handler);
    registerAll();
    LOG_INFO("[AnnotationShortcutController] Created and registered all shortcuts");
}

/**
 * @brief 析构函数
 *
 * 调用 unregisterAll() 注销所有快捷键，避免父窗口销毁后快捷键残留。
 * @author chiangyang
 */
AnnotationShortcutController::~AnnotationShortcutController() {
    unregisterAll();
    LOG_INFO("[AnnotationShortcutController] Destroyed");
}

/**
 * @brief 注册单个快捷键
 * @param key 快捷键序列
 * @param slot 回调方法
 * @param isBareKey 是否为裸键
 * @param checkCanAnnotate 是否做 canAnnotate 前置检查
 * @return 创建的 QShortcut 指针
 *
 * 创建 QShortcut 并设置 context 为 Qt::WindowShortcut（仅父窗口激活时生效），
 * activated 信号内部根据 checkCanAnnotate 决定是否做 canAnnotate() 前置检查，
 * 通过后调用 slot。创建的快捷键同时加入 m_allShortcuts，裸键另加入 m_bareKeyShortcuts。
 * @author chiangyang
 */
QShortcut* AnnotationShortcutController::registerShortcut(const QKeySequence& key,
                                                          std::function<void()> slot,
                                                          bool isBareKey,
                                                          bool checkCanAnnotate) {
    QShortcut* sc = new QShortcut(key, m_parent);
    sc->setContext(Qt::WindowShortcut);  // 仅在父窗口激活时生效
    QObject::connect(sc, &QShortcut::activated, m_parent, [this, slot, checkCanAnnotate]() {
        // 统一前置状态检查：canAnnotate 由实现类提供语义
        if (checkCanAnnotate && !m_handler->canAnnotate()) {
            return;
        }
        slot();
    });
    m_allShortcuts.append(sc);
    if (isBareKey) {
        m_bareKeyShortcuts.append(sc);
    }
    return sc;
}

/**
 * @brief 注册所有标注快捷键（模板方法）
 *
 * 注册顺序固定，便于维护和排查：
 * 1. 操作类快捷键（带修饰键 + Esc + F5）
 * 2. 工具切换快捷键（数字键 1-8，裸键）
 * 3. 工具调整快捷键（[/]/Tab/Delete/Backspace，裸键）
 *
 * 设计要点：
 * - Ctrl+C 作为裸键（文本编辑时禁用，让 OverlayTextEdit 处理文本复制）
 * - Esc/F5 不做 canAnnotate 检查（任意状态可触发取消/刷新）
 * - 其余标注动作做 canAnnotate 检查（仅选区完成/标注模式生效）
 * @author chiangyang
 */
void AnnotationShortcutController::registerAll() {
    // 1. 操作类快捷键（带修饰键 + Esc + F5）

    // Ctrl+C：复制（裸键：文本编辑时让给 OverlayTextEdit；做 canAnnotate 检查）
    registerShortcut(QKeySequence(QKeySequence::Copy),
                     [this]() { m_handler->onCopy(); },
                     /*isBareKey=*/true);

    // Ctrl+S：保存到文件
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_S),
                     [this]() { m_handler->onSave(); });

    // Ctrl+Z：撤销标注
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z),
                     [this]() { m_handler->onUndo(); });

    // Ctrl+Y：重做标注
    registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y),
                     [this]() { m_handler->onRedo(); });

    // Ctrl+Shift+Z：重做标注（与 Ctrl+Y 并存）
    registerShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                     [this]() { m_handler->onRedo(); });

    // Esc：取消（任意状态可触发：取消文本编辑 / 取消录制 / 退出截图 / 退出标注模式 / 关闭窗口）
    registerShortcut(QKeySequence(Qt::Key_Escape),
                     [this]() { m_handler->onCancel(); },
                     /*isBareKey=*/false,
                     /*checkCanAnnotate=*/false);

    // F5：刷新截图（仅 SnipScreen 有效，PinWindow 调用空实现；不做 canAnnotate 检查）
    registerShortcut(QKeySequence(Qt::Key_F5),
                     [this]() { m_handler->onRefresh(); },
                     /*isBareKey=*/false,
                     /*checkCanAnnotate=*/false);

    // `·` 键（反引号，数字 1 左侧）：切换截图/录屏模式
    // 仅 SnipScreen 实现有意义，PinWindow 调用空实现；不做 canAnnotate 检查，
    // 确保选区前后均可切换；作为裸键纳入冲突管理，文本编辑时禁用让给文本框输入
    registerShortcut(QKeySequence(Qt::Key_QuoteLeft),
                     [this]() { m_handler->onSwitchMode(); },
                     /*isBareKey=*/true,
                     /*checkCanAnnotate=*/false);

    // 2. 工具切换快捷键（数字键 1-8，裸键，需纳入冲突管理）
    // 工具 ID 与 AnnotationType 枚举值一一对应（0-7）：
    // 1=矩形 2=椭圆 3=箭头 4=画笔 5=直线 6=文本 7=马赛克 8=橡皮擦
    for (int i = 0; i < 8; ++i) {
        registerShortcut(QKeySequence(static_cast<Qt::Key>(Qt::Key_1 + i)),
                         [this, i]() { m_handler->onToolSwitch(i); },
                         /*isBareKey=*/true);
    }

    // 3. 工具调整快捷键（裸键，需纳入冲突管理）

    // [：画笔宽度 -1（范围 1-20）
    registerShortcut(QKeySequence(Qt::Key_BracketLeft),
                     [this]() { m_handler->onPenWidthChange(-1); },
                     /*isBareKey=*/true);

    // ]：画笔宽度 +1（范围 1-20）
    registerShortcut(QKeySequence(Qt::Key_BracketRight),
                     [this]() { m_handler->onPenWidthChange(+1); },
                     /*isBareKey=*/true);

    // Tab：循环切换颜色（按工具栏颜色面板顺序，末尾回绕）
    registerShortcut(QKeySequence(Qt::Key_Tab),
                     [this]() { m_handler->onCycleColor(); },
                     /*isBareKey=*/true);

    // Delete：清除所有标注
    registerShortcut(QKeySequence(Qt::Key_Delete),
                     [this]() { m_handler->onClear(); },
                     /*isBareKey=*/true);

    // Backspace：清除所有标注（与 Delete 同效）
    registerShortcut(QKeySequence(Qt::Key_Backspace),
                     [this]() { m_handler->onClear(); },
                     /*isBareKey=*/true);
}

/**
 * @brief 启用/禁用裸键快捷键
 * @param enabled true=启用标注快捷键，false=禁用（交给文本框）
 *
 * 遍历 m_bareKeyShortcuts 逐个 setEnabled。
 * 文本编辑框获得焦点时调用 false 禁用，使按键事件传递给 OverlayTextEdit；
 * 编辑框关闭时调用 true 恢复标注快捷键。
 * @author chiangyang
 */
void AnnotationShortcutController::setBareKeysEnabled(bool enabled) {
    for (QShortcut* sc : m_bareKeyShortcuts) {
        if (sc) {
            sc->setEnabled(enabled);
        }
    }
}

/**
 * @brief 注销所有快捷键
 *
 * 遍历 m_allShortcuts 逐个 setEnabled(false) 后 delete，
 * 避免销毁过程中误触发；最后清空两个列表。
 * @author chiangyang
 */
void AnnotationShortcutController::unregisterAll() {
    for (QShortcut* sc : m_allShortcuts) {
        if (sc) {
            sc->setEnabled(false);
            delete sc;
        }
    }
    m_allShortcuts.clear();
    m_bareKeyShortcuts.clear();
}
