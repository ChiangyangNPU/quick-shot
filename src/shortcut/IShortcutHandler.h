#ifndef ISHORTCUTHANDLER_H
#define ISHORTCUTHANDLER_H

/**
 * @file IShortcutHandler.h
 * @brief 标注快捷键处理接口（策略模式 + 命令模式）
 *
 * 定义"做什么"的语义动作集合，不关心"怎么触发"（QShortcut vs keyPressEvent）。
 * SnipScreen 和 PinWindow 各自实现此接口，AnnotationShortcutController
 * 通过接口回调，消除两处重复实现，行为完全一致。
 *
 * 设计要点：
 * - canAnnotate() 用于控制器统一做状态前置检查，避免每个动作重复判断
 * - onRefresh() 提供默认空实现，仅 SnipScreen 需要（F5 重新抓取屏幕），
 *   PinWindow 不支持也无需 override
 *
 * @author chiangyang
 */

/**
 * @brief 标注快捷键处理策略接口
 *
 * 从控制器视角是策略模式（可替换 SnipScreen / PinWindow），
 * 从单个方法视角是命令模式（每个方法封装一个独立动作）。
 *
 * 实现类需保证所有方法在被调用时处于有效状态（canAnnotate() == true），
 * 控制器会在调用前统一做 canAnnotate() 前置检查。
 *
 * @author chiangyang
 */
class IShortcutHandler {
public:
    /**
     * @brief 虚析构函数，确保通过基类指针正确释放派生类
     * @author chiangyang
     */
    virtual ~IShortcutHandler() = default;

    /**
     * @brief 判断当前是否可进行标注操作
     * @return true=可标注，false=当前状态不允许标注
     *
     * 控制器在调用具体动作前先检查此状态，避免在无效状态下误触发：
     * - SnipScreen: 选区状态为 Captured 或 Locked 时返回 true
     * - PinWindow: 处于标注模式时返回 true
     *
     * @note 此方法必须为 const 且无副作用，控制器可能在任意时刻调用
     * @author chiangyang
     */
    virtual bool canAnnotate() const = 0;

    /**
     * @brief 切换标注工具
     * @param toolId 工具 ID（0-7，对应 AnnotationType 枚举值）
     *
     * 工具映射：0=矩形 1=椭圆 2=箭头 3=画笔 4=直线 5=文本 6=马赛克 7=橡皮擦
     * 实现类内部应将 toolId 转发给工具栏的 selectAnnotationTool。
     * @author chiangyang
     */
    virtual void onToolSwitch(int toolId) = 0;

    /**
     * @brief 复制到剪贴板
     *
     * SnipScreen 复制后退出截图模式；PinWindow 仅复制不关闭窗口。
     * 语义差异由各实现类自行处理，接口语义统一为"复制当前内容"。
     * @author chiangyang
     */
    virtual void onCopy() = 0;

    /**
     * @brief 保存到文件
     *
     * 弹出文件保存对话框，将当前截图/贴图（含标注）保存到磁盘。
     * @author chiangyang
     */
    virtual void onSave() = 0;

    /**
     * @brief 撤销上一步标注
     * @author chiangyang
     */
    virtual void onUndo() = 0;

    /**
     * @brief 重做最近撤销的标注
     * @author chiangyang
     */
    virtual void onRedo() = 0;

    /**
     * @brief 清除所有标注
     * @author chiangyang
     */
    virtual void onClear() = 0;

    /**
     * @brief 调整画笔宽度
     * @param delta 宽度增量（+1 增加，-1 减少）
     *
     * 实现类应将结果钳制到 [kMinPenWidth, kMaxPenWidth] 范围。
     * 范围常量定义在 AnnotationShortcutController 中，实现类可引用。
     * @author chiangyang
     */
    virtual void onPenWidthChange(int delta) = 0;

    /**
     * @brief 循环切换标注颜色
     *
     * 按工具栏颜色面板顺序切换，末尾回绕到首个颜色。
     * @author chiangyang
     */
    virtual void onCycleColor() = 0;

    /**
     * @brief 取消当前操作（ESC 键）
     *
     * 语义为"取消当前模式或退出"：
     * - SnipScreen: 取消文本编辑 → 取消录制 → 退出截图（按优先级）
     * - PinWindow: 退出标注模式（若在标注模式），否则关闭窗口
     * @author chiangyang
     */
    virtual void onCancel() = 0;

    /**
     * @brief 刷新截图（仅 SnipScreen 实现，F5 重新抓取屏幕保留选区）
     *
     * PinWindow 不需要此功能，使用此处默认空实现，无需 override。
     * 控制器注册 F5 时仍会调用此方法，PinWindow 调用后无效果。
     * @author chiangyang
     */
    virtual void onRefresh() {}

    /**
     * @brief 切换截图/录屏模式（`·` 键触发，仅 SnipScreen 实现）
     *
     * SnipScreen 在截图模式与录屏模式间互相切换；录屏正在录制时禁止切换。
     * PinWindow 不涉及录屏，使用此处默认空实现，无需 override。
     * @author chiangyang
     */
    virtual void onSwitchMode() {}
};

#endif // ISHORTCUTHANDLER_H
