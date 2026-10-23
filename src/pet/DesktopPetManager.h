#ifndef DESKTOPPETMANAGER_H
#define DESKTOPPETMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QPointer>

class PetWindow;
class ClotheslineWindow;

/**
 * @brief 桌面宠物管理器（单例）
 *
 * 负责桌面宠物的生命周期管理：
 * - 开关状态持久化（pet/enabled）与启动时恢复
 * - 宠物窗口位置持久化（pet/posX、pet/posY）
 * - 截图复制/贴图后的"投喂"入口：触发飞行动画与宠物进食动作
 * - 文字复制后的"投喂"入口：文字渲染成小卡片从鼠标位置飞入宠物
 * - 宠物点击后晾衣绳历史窗口的显隐切换
 * @author chiangyang
 */
class DesktopPetManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return DesktopPetManager 实例指针
     * @author chiangyang
     */
    static DesktopPetManager* instance();

    /**
     * @brief 从持久化配置初始化（应用启动时调用一次）
     *
     * 读取 pet/enabled，若开启则创建并显示桌面宠物。
     * @author chiangyang
     */
    void initFromConfig();

    /**
     * @brief 设置桌面宠物开关（即时显隐并持久化）
     * @param enabled 是否开启
     * @author chiangyang
     */
    void setEnabled(bool enabled);

    /**
     * @brief 获取桌面宠物是否开启
     * @return 是否开启
     * @author chiangyang
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief 投喂宠物：截图从 sourceGlobalRect 飞入宠物并被吃掉
     *
     * 宠物未开启或图像为空时直接返回，调用方无需判断。
     * @param pixmap 截图图像
     * @param sourceGlobalRect 截图区域的全局屏幕矩形（动画起点）
     * @author chiangyang
     */
    void feed(const QPixmap &pixmap, const QRect &sourceGlobalRect);

    /**
     * @brief 投喂宠物文字：文字渲染成小卡片后从鼠标位置飞入宠物并被吃掉
     *
     * 宠物未开启或文字为空时直接返回，调用方无需判断。
     * @param text 复制的文字内容
     * @author chiangyang
     */
    void feedText(const QString &text);

    /**
     * @brief 将宠物移回默认位置（屏幕右下角）并清除持久化位置
     * @author chiangyang
     */
    void resetPetPosition();

    /**
     * @brief 获取宠物窗口指针（晾衣绳等协同窗口定位/事件过滤用）
     * @return 宠物窗口指针，未开启时为 nullptr
     * @author chiangyang
     */
    PetWindow* petWindow() const;

signals:
    /**
     * @brief 开关状态变更信号
     * @param enabled 新的开关状态
     * @author chiangyang
     */
    void enabledChanged(bool enabled);

private:
    /**
     * @brief 构造函数（私有，单例模式）
     * @param parent 父对象
     * @author chiangyang
     */
    explicit DesktopPetManager(QObject *parent = nullptr);

    /**
     * @brief 创建宠物窗口并接线（拖动位置持久化、点击切换晾衣绳）
     * @author chiangyang
     */
    void createPetWindow();

    /**
     * @brief 计算宠物默认位置（主屏幕右下角内收边距）
     * @return 默认左上角全局坐标
     * @author chiangyang
     */
    QPoint defaultPetPosition() const;

    /**
     * @brief 切换晾衣绳窗口显隐（宠物点击槽）
     * @author chiangyang
     */
    void toggleClothesline();

    /**
     * @brief 将文字渲染成圆角小卡片图像（文字投喂的飞行内容）
     * @param text 文字内容
     * @return 渲染的卡片图像
     * @author chiangyang
     */
    QPixmap renderTextCard(const QString &text) const;

    static DesktopPetManager* s_instance;   ///< 单例实例
    bool m_enabled = false;                 ///< 是否开启
    QPointer<PetWindow> m_petWindow;        ///< 宠物窗口（未开启时为 nullptr）
    QPointer<ClotheslineWindow> m_clothesline; ///< 晾衣绳窗口（未显示时为 nullptr）
};

#endif // DESKTOPPETMANAGER_H
