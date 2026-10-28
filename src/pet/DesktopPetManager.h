#ifndef DESKTOPPETMANAGER_H
#define DESKTOPPETMANAGER_H

#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QPointer>
#include <QVector>
#include "PetWindow.h"
#include "CustomPetStore.h"

class ClotheslineWindow;
class TodoWindow;
class QTimer;

/**
 * @brief 桌面宠物管理器（单例）
 *
 * 负责桌面宠物的生命周期管理：
 * - 开关状态持久化（pet/enabled）与启动时恢复
 * - 宠物窗口位置持久化（pet/posX、pet/posY）
 * - 宠物外观样式切换与持久化（pet/style）
 * - 截图复制/贴图后的"投喂"入口：触发飞行动画与宠物进食动作
 * - 文字复制后的"投喂"入口：文字渲染成小卡片从鼠标位置飞入宠物
 * - 宠物点击后晾衣绳历史窗口的显隐切换
 * - 主屏顶边热区：宠物开启期间，鼠标移到屏幕上边界可切换晾衣绳显隐
 *   （宠物点击与热区触发均带下滑挂入/上滑收起的整体滑动动画）
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

    /**
     * @brief 获取当前宠物外观样式（持久化于 pet/style）
     * @return 样式枚举值
     * @author chiangyang
     */
    PetWindow::PetStyle petStyle() const;

    /**
     * @brief 设置宠物外观样式（即时生效并持久化）
     * @param style 样式枚举值
     * @author chiangyang
     */
    void setPetStyle(PetWindow::PetStyle style);

    /**
     * @brief 获取当前选择标识（"builtin:<n>" 或 "custom:<id>"）
     * @return 选择标识
     * @author chiangyang
     */
    QString selection() const;

    /**
     * @brief 选择内置造型（切换后清除自定义素材）
     * @param style 样式枚举值
     * @author chiangyang
     */
    void selectBuiltin(PetWindow::PetStyle style);

    /**
     * @brief 选择自定义宠物（立即生效并持久化）
     * @param customPetId 自定义素材 id
     * @author chiangyang
     */
    void selectCustom(const QString &customPetId);

    /**
     * @brief 获取已导入的自定义宠物列表
     * @return 记录列表
     * @author chiangyang
     */
    QVector<CustomPet> customPets() const;

    /**
     * @brief 导入自定义宠物素材（成功后自动选中）
     * @param sourcePath 用户选择的原图路径
     * @param errorMessage 失败原因输出（可为 nullptr）
     * @return 导入成功返回记录，失败返回无效记录
     * @author chiangyang
     */
    CustomPet addCustomPet(const QString &sourcePath, QString *errorMessage = nullptr);

    /**
     * @brief 删除自定义宠物素材（删除当前选中项时回落到默认造型）
     * @param customPetId 自定义素材 id
     * @return 是否删除成功
     * @author chiangyang
     */
    bool removeCustomPet(const QString &customPetId);

    /**
     * @brief 显示待办清单窗口（右击宠物触发）
     *
     * 窗口在首次打开时创建并定位到宠物附近，之后复用同一实例
     * （已打开则提到前台）。
     * @author chiangyang
     */
    void showTodoWindow();

    /**
     * @brief 按当前主题刷新待办清单窗口（主题切换时由主窗口调用）
     * @author chiangyang
     */
    void refreshTodoWindowStyle();

signals:
    /**
     * @brief 开关状态变更信号
     * @param enabled 新的开关状态
     * @author chiangyang
     */
    void enabledChanged(bool enabled);

    /**
     * @brief 宠物外观选择变更信号（内置样式或自定义素材）
     * @param selection 新的选择标识
     * @author chiangyang
     */
    void selectionChanged(const QString &selection);

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
     * @brief 按当前选择刷新宠物窗口外观（内置造型或自定义素材）
     * @author chiangyang
     */
    void applySelection();

    /**
     * @brief 计算宠物默认位置（主屏幕右下角内收边距）
     * @return 默认左上角全局坐标
     * @author chiangyang
     */
    QPoint defaultPetPosition() const;

    /**
     * @brief 切换晾衣绳窗口显隐（宠物点击/顶边热区共用槽）
     *
     * 可见时上滑收起（异步，结束后窗口自毁、QPointer 自动清空），
     * 不可见时创建并以下滑挂入动画显示。
     * @author chiangyang
     */
    void toggleClothesline();

    /**
     * @brief 启动主屏顶边热区轮询（宠物开启期间运行）
     * @author chiangyang
     */
    void startHotEdgeWatch();

    /**
     * @brief 停止主屏顶边热区轮询
     * @author chiangyang
     */
    void stopHotEdgeWatch();

    /**
     * @brief 顶边热区轮询：光标进入主屏上边界热区时切换晾衣绳显隐
     *
     * 边沿触发：一次进入热区只切换一次，离开热区后重新武装，
     * 避免在热区内悬停时反复弹收。
     * @author chiangyang
     */
    void pollHotEdge();

    /**
     * @brief 将文字渲染成圆角小卡片图像（文字投喂的飞行内容）
     * @param text 文字内容
     * @return 渲染的卡片图像
     * @author chiangyang
     */
    QPixmap renderTextCard(const QString &text) const;

    /**
     * @brief 将待办清单窗口摆到宠物窗口左侧（空间不足时夹到屏幕可用区域内）
     * @author chiangyang
     */
    void positionTodoWindow();

    static DesktopPetManager* s_instance;   ///< 单例实例
    bool m_enabled = false;                 ///< 是否开启
    QPointer<PetWindow> m_petWindow;        ///< 宠物窗口（未开启时为 nullptr）
    QPointer<ClotheslineWindow> m_clothesline; ///< 晾衣绳窗口（未显示时为 nullptr）
    QPointer<TodoWindow> m_todoWindow;      ///< 待办清单窗口（未打开时为 nullptr）
    QTimer *m_hotEdgeTimer = nullptr;       ///< 主屏顶边热区轮询定时器（宠物开启期间运行）
    bool m_hotEdgeArmed = true;             ///< 光标离开热区后重新武装（边沿触发防反复弹收）
};

#endif // DESKTOPPETMANAGER_H
