#include "DesktopPetManager.h"
#include "PetWindow.h"
#include "TodoWindow.h"
#include "PetReminder.h"
#include "PetBadgeWidget.h"
#include "PomodoroCard.h"
#include "PetStateMachine.h"
#include "PetSystemMonitor.h"
#include "FlyAnimationWidget.h"
#include "ClotheslineWindow.h"
#include "PetUiScale.h"
#include "../core/ConfigManager.h"
#include "../history/HistoryManager.h"
#include "../history/HistoryItem.h"
#include "../log/Logger.h"

#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QDateTime>
#include <QPainter>
#include <QFontMetrics>
#include <QTimer>

namespace {
// 主屏顶边热区：光标进入上边界以内该逻辑像素深度视为"移到屏幕上边界"
constexpr int kHotEdgeTriggerPx = 5;
// 热区轮询间隔
constexpr int kHotEdgePollMs = 60;
}

DesktopPetManager* DesktopPetManager::s_instance = nullptr;

/**
 * @brief 获取单例实例
 * @author chiangyang
 */
DesktopPetManager* DesktopPetManager::instance() {
    if (!s_instance) {
        s_instance = new DesktopPetManager();
    }
    return s_instance;
}

/**
 * @brief 构造函数（私有，单例模式）
 * @author chiangyang
 */
DesktopPetManager::DesktopPetManager(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 从持久化配置初始化（应用启动时调用一次）
 * @author chiangyang
 */
void DesktopPetManager::initFromConfig() {
    const bool enabled = ConfigManager::instance()->value("pet/enabled", false).toBool();
    if (enabled) {
        setEnabled(true);
    }

    // 文字复制投喂：新增文本历史记录时让宠物吃掉（图片由截图/贴图/快照流程显式投喂）
    connect(HistoryManager::instance(), &HistoryManager::itemAdded,
            this, [this](const HistoryItem &item) {
        if (!item.isScreenshot()) {
            feedText(item.content);
        }
    });

    LOG_INFO(QString("DesktopPetManager: initFromConfig, enabled=%1").arg(enabled));
}

/**
 * @brief 设置桌面宠物开关（即时显隐并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    ConfigManager::instance()->setValue("pet/enabled", enabled);
    ConfigManager::instance()->sync();

    if (enabled) {
        createPetWindow();
        startHotEdgeWatch();
    } else {
        stopHotEdgeWatch();
        // 三个窗口均为用时新建（createPetWindow/showClothesline/番茄钟触发处），
        // 关闭后统一 deleteLater 销毁：PetWindow 自带永不停止的眨眼定时器链与
        // QMovie 全帧缓存，只 close 不销毁会在反复开关中累积泄漏
        if (m_clothesline) {
            m_clothesline->close();
            m_clothesline->deleteLater();
            m_clothesline = nullptr;
        }
        if (m_petWindow) {
            m_petWindow->close();
            m_petWindow->deleteLater();
            m_petWindow = nullptr;
        }
        // 待办清单随宠物一起关闭（宠物关闭后无法再次打开它）
        if (m_todoWindow) {
            m_todoWindow->close();
            m_todoWindow->deleteLater();
            m_todoWindow = nullptr;
        }
        // 提醒角标与轮询随宠物一起停止
        if (m_reminder) {
            m_reminder->stop();
        }
        if (m_todoBadge) {
            m_todoBadge->close();
            m_todoBadge->deleteLater();
            m_todoBadge = nullptr;
        }
        // 番茄钟提示卡随宠物一起收起（计时已由 m_reminder->stop() 停止）；
        // hide 不发 close 信号、不打断 dismissing 流程，销毁走 deleteLater
        if (m_pomodoroCard) {
            m_pomodoroCard->hide();
            m_pomodoroCard->deleteLater();
            m_pomodoroCard = nullptr;
        }
        // 待机打盹检测随宠物一起停止（stop 会先唤醒打盹中的宠物）
        if (m_stateMachine) {
            m_stateMachine->stop();
        }
        // 系统状态采集随宠物一起停止
        if (m_sysMonitor) {
            m_sysMonitor->stop();
        }
        // 节日装饰判定随宠物一起停止
        if (m_festivalTimer) {
            m_festivalTimer->stop();
        }
    }
    LOG_INFO(QString("DesktopPetManager: pet %1").arg(enabled ? "enabled" : "disabled"));
    emit enabledChanged(enabled);
}

/**
 * @brief 创建宠物窗口并接线
 * @author chiangyang
 */
void DesktopPetManager::createPetWindow() {
    if (m_petWindow) {
        m_petWindow->show();
        m_petWindow->raise();
        return;
    }

    m_petWindow = new PetWindow();
    // 按持久化选择应用外观（内置造型或自定义素材）
    applySelection();
    // 装饰：节日装饰优先于手动装饰，并排定下一次节日判定
    applyAccessory();
    scheduleFestivalCheck();

    // 位置恢复：pet/posX/posY 均有效（>=0）则使用，否则落在默认右下角
    QSettings *settings = ConfigManager::instance()->getSettings();
    const int savedX = settings->value("pet/posX", -1).toInt();
    const int savedY = settings->value("pet/posY", -1).toInt();
    const QPoint pos = (savedX >= 0 && savedY >= 0) ? QPoint(savedX, savedY)
                                                    : defaultPetPosition();
    // 位置兜底：避免配置文件里的旧坐标在多屏变化后落到可见区域外
    const QRect virtualGeo = QGuiApplication::primaryScreen()->virtualGeometry();
    const QPoint safePos = virtualGeo.contains(pos) ? pos : defaultPetPosition();
    m_petWindow->move(safePos);

    // 拖动结束持久化位置
    connect(m_petWindow, &PetWindow::dragFinished, this, [](const QPoint &newPos) {
        ConfigManager::instance()->setValue("pet/posX", newPos.x());
        ConfigManager::instance()->setValue("pet/posY", newPos.y());
        ConfigManager::instance()->sync();
    });

    // 点击切换晾衣绳（设置里可关闭点击触发）
    connect(m_petWindow, &PetWindow::clicked, this, [this]() {
        if (clotheslineTrigger() & TriggerPetClick) {
            toggleClothesline();
        }
    });

    // 右键打开待办清单
    connect(m_petWindow, &PetWindow::contextMenuRequested,
            this, &DesktopPetManager::showTodoWindow);

    // 待办提醒：轮询逾期/今天到期数量，变化时刷新宠物头顶红点角标
    if (!m_reminder) {
        m_reminder = new PetReminder(this);
        connect(m_reminder, &PetReminder::pomodoroBreakDue,
                this, [this]() { showPomodoroCard(true); });
        connect(m_reminder, &PetReminder::pomodoroBreakFinished,
                this, [this]() { showPomodoroCard(false); });
        // reminderChanged 必须挂在 m_reminder 守卫内（进程内仅创建一次）：
        // 放在下方 todoBadge 守卫里会随角标销毁重建而重复 connect，
        // N 次开关宠物后一次提醒触发 N 次角标刷新
        connect(m_reminder, &PetReminder::reminderChanged,
                this, [this](int, int) { updateTodoBadge(); });
    }
    if (!m_todoBadge) {
        m_todoBadge = new PetBadgeWidget();
        connect(m_todoBadge, &PetBadgeWidget::clicked,
                this, &DesktopPetManager::showTodoWindow);
        connect(m_petWindow, &PetWindow::moved,
                this, [this](const QPoint &) { positionTodoBadge(); });
    }
    m_reminder->start();
    updateTodoBadge();
    // 番茄钟开关已开时恢复计时（宠物关闭期间停止，重新开启后从工作阶段开始）
    if (pomodoroEnabled()) {
        m_reminder->startPomodoro();
    }

    // 趣味陪伴：待机打盹状态机
    if (!m_stateMachine) {
        m_stateMachine = new PetStateMachine(this);
    }
    m_stateMachine->setPet(m_petWindow);
    m_stateMachine->setIdleEnabled(idleSleepEnabled());
    m_stateMachine->setIdleMinutes(idleSleepMinutes());
    // 趣味陪伴：待机打盹状态机。按下/点击/双击/拖动/右击/悬停均视为互动（唤醒并重置计时）；
    // pressed 必须接：悬停唤醒在 enterEvent 已消费时失效，按下是唯一可靠的拖动前唤醒点，
    // 且要赶在下一个待机重查（15s）之前重置计时，防止拖动中被再次催眠
    connect(m_petWindow, &PetWindow::pressed,
            m_stateMachine, &PetStateMachine::noteInteraction);
    connect(m_petWindow, &PetWindow::clicked,
            m_stateMachine, &PetStateMachine::noteInteraction);
    connect(m_petWindow, &PetWindow::dragFinished,
            m_stateMachine, [this](const QPoint &) { m_stateMachine->noteInteraction(); });
    connect(m_petWindow, &PetWindow::contextMenuRequested,
            m_stateMachine, &PetStateMachine::noteInteraction);
    connect(m_petWindow, &PetWindow::hoverEntered,
            m_stateMachine, &PetStateMachine::noteInteraction);
    // 双击摸头：唤醒并原地跳一下
    connect(m_petWindow, &PetWindow::doubleClicked, this, [this]() {
        m_stateMachine->noteInteraction();
        if (m_petWindow) {
            m_petWindow->playJump();
        }
    });
    m_stateMachine->start();

    // 系统状态：CPU/内存过载时宠物冒汗、低电量时显示电池角标（5s 轮询）
    if (!m_sysMonitor) {
        m_sysMonitor = new PetSystemMonitor(this);
        connect(m_sysMonitor, &PetSystemMonitor::alertChanged, this,
                [this](PetSystemMonitor::Alert alert) {
            if (!m_petWindow) {
                return;
            }
            switch (alert) {
            case PetSystemMonitor::Alert::Overload:
                m_petWindow->setSysAlert(PetWindow::SysAlert::Overload);
                break;
            case PetSystemMonitor::Alert::LowBattery:
                m_petWindow->setSysAlert(PetWindow::SysAlert::LowBattery);
                break;
            default:
                m_petWindow->setSysAlert(PetWindow::SysAlert::None);
                break;
            }
        });
    }
    if (sysMonitorEnabled()) {
        m_sysMonitor->start();
    }

    m_petWindow->show();
    m_petWindow->raise();
}

/**
 * @brief 计算宠物默认位置（主屏幕右下角内收边距）
 * @author chiangyang
 */
QPoint DesktopPetManager::defaultPetPosition() const {
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry()
                                   : QRect(0, 0, 1920, 1080);
    // 距右边 40px、距底边 80px（避开任务栏）
    return QPoint(available.right() - PetWindow::petSize() - 40,
                  available.bottom() - PetWindow::petSize() - 80);
}

/**
 * @brief 投喂宠物：截图从 sourceGlobalRect 飞入宠物并被吃掉
 * @author chiangyang
 */
void DesktopPetManager::feed(const QPixmap &pixmap, const QRect &sourceGlobalRect) {
    if (!m_enabled || !m_petWindow || pixmap.isNull()) {
        return;
    }

    // 投喂也是互动：唤醒打盹中的宠物并重置待机计时
    if (m_stateMachine) {
        m_stateMachine->noteInteraction();
    }

    // 宠物提前张嘴等待食物
    m_petWindow->openMouth();

    // 飞行动画：从截图区域缩小飞到宠物嘴边，到达后宠物咀嚼
    FlyAnimationWidget *fly = new FlyAnimationWidget(pixmap, sourceGlobalRect,
                                                     m_petWindow->mouthCenterGlobal());
    connect(fly, &FlyAnimationWidget::arrived, m_petWindow, &PetWindow::chew);
    fly->start();
}

/**
 * @brief 投喂宠物文字：文字渲染成小卡片后从鼠标位置飞入宠物并被吃掉
 * @author chiangyang
 */
void DesktopPetManager::feedText(const QString &text) {
    if (!m_enabled || !m_petWindow || text.isEmpty()) {
        return;
    }

    const QPixmap card = renderTextCard(text);
    if (card.isNull()) {
        return;
    }
    // 以鼠标位置为动画起点（复制动作发生时光标即"食物来源"）
    const QPoint cursor = QCursor::pos();
    const QRect sourceRect(cursor - QPoint(card.width() / 2, card.height() / 2), card.size());
    feed(card, sourceRect);
}

/**
 * @brief 将文字渲染成圆角小卡片图像
 * @author chiangyang
 */
QPixmap DesktopPetManager::renderTextCard(const QString &text) const {
    const int kMaxW = 240;  // 文字换行宽度
    const int kMaxH = 160;  // 卡片内容最大高度
    QFontMetrics fm(QFont{});
    QRect br = fm.boundingRect(QRect(0, 0, kMaxW, 10000), Qt::TextWordWrap, text);
    br.setHeight(qMin(br.height(), kMaxH));
    const QSize size = br.size() + QSize(20, 12);

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(pix.rect().adjusted(0, 0, -1, -1), 8, 8);
    p.setPen(QColor(60, 60, 60));
    p.drawText(pix.rect().adjusted(10, 6, -10, -6),
               Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, text);
    return pix;
}

/**
 * @brief 切换晾衣绳窗口显隐（宠物点击/顶边热区共用槽）
 * @author chiangyang
 */
void DesktopPetManager::toggleClothesline() {
    if (m_clothesline && m_clothesline->isVisible()) {
        // 上滑收起是异步的：滑出屏幕后窗口走 close()/deleteLater 自毁，
        // m_clothesline（QPointer）随之自动清空
        m_clothesline->hideWithSlide();
        return;
    }

    if (!m_petWindow) {
        return;
    }
    m_clothesline = new ClotheslineWindow();
    m_clothesline->showFullWidth();
}

/**
 * @brief 启动主屏顶边热区轮询
 * @author chiangyang
 */
void DesktopPetManager::startHotEdgeWatch() {
    if (!m_hotEdgeTimer) {
        m_hotEdgeTimer = new QTimer(this);
        m_hotEdgeTimer->setInterval(kHotEdgePollMs);
        connect(m_hotEdgeTimer, &QTimer::timeout, this, &DesktopPetManager::pollHotEdge);
    }
    m_hotEdgeArmed = true;
    m_hotEdgeTimer->start();
}

/**
 * @brief 停止主屏顶边热区轮询
 * @author chiangyang
 */
void DesktopPetManager::stopHotEdgeWatch() {
    if (m_hotEdgeTimer) {
        m_hotEdgeTimer->stop();
    }
}

/**
 * @brief 顶边热区轮询：光标进入主屏上边界热区时切换晾衣绳显隐
 * @author chiangyang
 */
void DesktopPetManager::pollHotEdge() {
    // 设置里未启用"上边界触发"时不响应热区
    if (!(clotheslineTrigger() & TriggerHotEdge)) {
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }
    const int triggerY = screen->geometry().top() + PetUi::scaled(kHotEdgeTriggerPx);
    if (QCursor::pos().y() <= triggerY) {
        if (m_hotEdgeArmed) {
            m_hotEdgeArmed = false; // 一次进入只切换一次，离开热区后重新武装
            toggleClothesline();
        }
    } else {
        m_hotEdgeArmed = true;
    }
}

/**
 * @brief 将宠物移回默认位置并清除持久化位置
 * @author chiangyang
 */
void DesktopPetManager::resetPetPosition() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->remove("pet/posX");
    settings->remove("pet/posY");
    settings->sync();

    if (m_petWindow) {
        m_petWindow->move(defaultPetPosition());
    }
    LOG_INFO("DesktopPetManager: pet position reset to default");
}

/**
 * @brief 获取宠物窗口指针
 * @author chiangyang
 */
PetWindow* DesktopPetManager::petWindow() const {
    return m_petWindow;
}

/**
 * @brief 获取当前宠物外观样式（由 pet/selection 解析，兼容旧 pet/style）
 * @author chiangyang
 */
PetWindow::PetStyle DesktopPetManager::petStyle() const {
    const int v = CustomPetStore::builtinStyleFromSelection(selection(), 0);
    return static_cast<PetWindow::PetStyle>(qBound(0, v, PetWindow::petStyleCount() - 1));
}

/**
 * @brief 设置宠物外观样式（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setPetStyle(PetWindow::PetStyle style) {
    selectBuiltin(style);
}

/**
 * @brief 获取当前选择标识
 * @author chiangyang
 */
QString DesktopPetManager::selection() const {
    return CustomPetStore::instance()->selection();
}

/**
 * @brief 选择内置造型（切换后清除自定义素材）
 * @author chiangyang
 */
void DesktopPetManager::selectBuiltin(PetWindow::PetStyle style) {
    const QString sel = CustomPetStore::builtinSelection(int(style));
    CustomPetStore::instance()->setSelection(sel);
    ConfigManager::instance()->sync();

    if (m_petWindow) {
        m_petWindow->clearCustomPet();
        m_petWindow->setStyle(style);
    }
    LOG_INFO(QString("DesktopPetManager: pet style set to %1").arg(int(style)));
    emit selectionChanged(sel);
}

/**
 * @brief 选择自定义宠物（立即生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::selectCustom(const QString &customPetId) {
    const CustomPet pet = CustomPetStore::instance()->find(customPetId);
    if (!pet.isValid()) {
        LOG_WARNING(QString("DesktopPetManager: custom pet not found: %1").arg(customPetId));
        return;
    }
    const QString sel = CustomPetStore::customSelection(customPetId);
    CustomPetStore::instance()->setSelection(sel);
    ConfigManager::instance()->sync();

    if (m_petWindow) {
        m_petWindow->setCustomPet(CustomPetStore::instance()->filePath(pet));
    }
    LOG_INFO(QString("DesktopPetManager: custom pet selected: %1").arg(customPetId));
    emit selectionChanged(sel);
}

/**
 * @brief 获取已导入的自定义宠物列表
 * @author chiangyang
 */
QVector<CustomPet> DesktopPetManager::customPets() const {
    return CustomPetStore::instance()->list();
}

/**
 * @brief 导入自定义宠物素材（成功后自动选中）
 * @author chiangyang
 */
CustomPet DesktopPetManager::addCustomPet(const QString &sourcePath, QString *errorMessage) {
    const CustomPet pet = CustomPetStore::instance()->add(sourcePath, errorMessage);
    if (pet.isValid()) {
        selectCustom(pet.id);
    }
    return pet;
}

/**
 * @brief 删除自定义宠物素材（删除当前选中项时回落到默认造型）
 * @author chiangyang
 */
bool DesktopPetManager::removeCustomPet(const QString &customPetId) {
    const QString current = selection();
    const bool wasCurrent = CustomPetStore::isCustomSelection(current)
                            && CustomPetStore::customIdFromSelection(current) == customPetId;
    if (!CustomPetStore::instance()->remove(customPetId)) {
        return false;
    }
    // Store 已把选中项回落到默认内置造型，这里同步刷新窗口
    if (wasCurrent) {
        applySelection();
    }
    emit selectionChanged(selection());
    return true;
}

/**
 * @brief 按当前选择刷新宠物窗口外观
 * @author chiangyang
 */
void DesktopPetManager::applySelection() {
    if (!m_petWindow) {
        return;
    }
    const QString sel = selection();
    if (CustomPetStore::isCustomSelection(sel)) {
        const CustomPet pet = CustomPetStore::instance()->find(
            CustomPetStore::customIdFromSelection(sel));
        if (pet.isValid()) {
            m_petWindow->setCustomPet(CustomPetStore::instance()->filePath(pet));
            return;
        }
        // 素材文件丢失：回落到默认内置造型，避免宠物窗口空白
        LOG_WARNING(QString("DesktopPetManager: custom pet missing, fallback to builtin: %1").arg(sel));
        CustomPetStore::instance()->setSelection(
            CustomPetStore::builtinSelection(int(PetWindow::PetStyle::Cat)));
    }
    m_petWindow->clearCustomPet();
    m_petWindow->setStyle(petStyle());
}

/**
 * @brief 显示/收起待办清单气泡（右击宠物触发）
 * @author chiangyang
 */
void DesktopPetManager::showTodoWindow() {
    if (!m_enabled) {
        return;
    }
    // 已展开时再次右击即收起
    if (m_todoWindow && m_todoWindow->isVisible()) {
        m_todoWindow->hide();
        return;
    }
    if (!m_todoWindow) {
        m_todoWindow = new TodoWindow();
        // 气泡高度随任务数量变化，尺寸变化后重新贴合宠物
        connect(m_todoWindow, &TodoWindow::bubbleSizeChanged, this, [this]() {
            if (m_todoWindow && m_todoWindow->isVisible()) {
                positionTodoWindow();
            }
        });
        // 待办增删改后立即重估提醒角标（不等下一轮轮询）
        connect(m_todoWindow, &TodoWindow::itemsChanged, this, [this]() {
            if (m_reminder) {
                m_reminder->evaluate();
            }
        });
    }
    m_todoWindow->show();
    // 先显示（showEvent 会重算内容高度），再按最终尺寸定位
    positionTodoWindow();
    m_todoWindow->raise();
    m_todoWindow->activateWindow();
}

/**
 * @brief 将待办气泡摆到宠物上方（空间不足时移到下方），小三角指向宠物
 * @author chiangyang
 */
void DesktopPetManager::positionTodoWindow() {
    if (!m_todoWindow || !m_petWindow) {
        return;
    }

    QScreen *screen = m_petWindow->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    const int margin = PetUi::scaled(8);
    const int gap = PetUi::scaled(6);
    const QSize size = m_todoWindow->size();
    const QRect pet = m_petWindow->geometry();
    const int petCenterX = pet.center().x();

    // 默认悬在宠物上方，气泡的小三角朝下
    int x = petCenterX - size.width() / 2;
    int y = pet.top() - size.height() - gap;
    bool arrowBelow = true;

    // 上方放不下就翻到宠物下方，小三角改为朝上
    if (y < available.top() + margin) {
        y = pet.bottom() + gap;
        arrowBelow = false;
    }

    // 贴边兜底：夹到当前屏幕可用区域内，并保证三角始终指向宠物中心
    x = qBound(available.left() + margin, x, available.right() - size.width() - margin);
    y = qBound(available.top() + margin, y, available.bottom() - size.height() - margin);

    m_todoWindow->setBubblePlacement(arrowBelow, petCenterX - x);
    m_todoWindow->move(x, y);
}

/**
 * @brief 按当前主题刷新待办清单窗口
 * @author chiangyang
 */
void DesktopPetManager::refreshTodoWindowStyle() {
    if (m_todoWindow) {
        m_todoWindow->updateWindowColors();
    }
}

/**
 * @brief 主题切换后重绘宠物本体与待办角标
 * @author chiangyang
 */
void DesktopPetManager::refreshTheme() {
    if (m_petWindow) {
        m_petWindow->update();
    }
    if (m_todoBadge) {
        m_todoBadge->update();
    }
}

/**
 * @brief 获取晾衣绳触发方式
 * @author chiangyang
 */
int DesktopPetManager::clotheslineTrigger() const {
    const int v = ConfigManager::instance()->value("pet/clotheslineTrigger", int(TriggerPetClick)).toInt();
    return qBound(int(TriggerHotEdge), v, int(TriggerBoth));
}

/**
 * @brief 设置晾衣绳触发方式（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setClotheslineTrigger(int trigger) {
    const int clamped = qBound(int(TriggerHotEdge), trigger, int(TriggerBoth));
    if (clotheslineTrigger() == clamped) {
        return;
    }
    ConfigManager::instance()->setValue("pet/clotheslineTrigger", clamped);
    ConfigManager::instance()->sync();
    LOG_INFO(QString("DesktopPetManager: clothesline trigger set to %1").arg(clamped));
}

/**
 * @brief 获取待办红点提醒是否开启（持久化于 pet/todoReminder，默认开）
 * @author chiangyang
 */
bool DesktopPetManager::todoReminderEnabled() const {
    return ConfigManager::instance()->value("pet/todoReminder", true).toBool();
}

/**
 * @brief 设置待办红点提醒开关（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setTodoReminderEnabled(bool enabled) {
    if (todoReminderEnabled() == enabled) {
        return;
    }
    ConfigManager::instance()->setValue("pet/todoReminder", enabled);
    ConfigManager::instance()->sync();
    updateTodoBadge();
    LOG_INFO(QString("DesktopPetManager: todo reminder %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 按提醒数量与开关刷新头顶角标的显示
 * @author chiangyang
 */
void DesktopPetManager::updateTodoBadge() {
    if (!m_todoBadge) {
        return;
    }
    m_todoBadge->setReminder(m_reminder ? m_reminder->overdueCount() : 0,
                             m_reminder ? m_reminder->dueTodayCount() : 0);
    if (!m_enabled || !m_petWindow || !todoReminderEnabled()) {
        m_todoBadge->hide();
        return;
    }
    positionTodoBadge();
}

/**
 * @brief 将提醒角标贴合到宠物右上角（宠物移动时跟随）
 * @author chiangyang
 */
void DesktopPetManager::positionTodoBadge() {
    if (!m_todoBadge || !m_todoBadge->isVisible() || !m_petWindow) {
        return;
    }
    const QRect pet = m_petWindow->geometry();
    const int d = m_todoBadge->width();
    // 右上角并略微外探，圆形压住宠物轮廓边缘
    m_todoBadge->move(pet.right() - d + PetUi::scaled(8),
                      pet.top() - PetUi::scaled(6));
}

/**
 * @brief 获取番茄钟是否开启
 * @author chiangyang
 */
bool DesktopPetManager::pomodoroEnabled() const {
    return ConfigManager::instance()->value("pet/pomodoroEnabled", false).toBool();
}

/**
 * @brief 设置番茄钟开关（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setPomodoroEnabled(bool enabled) {
    if (pomodoroEnabled() == enabled) {
        return;
    }
    ConfigManager::instance()->setValue("pet/pomodoroEnabled", enabled);
    ConfigManager::instance()->sync();
    if (m_reminder && m_enabled) {
        if (enabled) {
            m_reminder->startPomodoro();
        } else {
            m_reminder->stopPomodoro();
            if (m_pomodoroCard) {
                m_pomodoroCard->hide();
            }
        }
    }
    LOG_INFO(QString("DesktopPetManager: pomodoro %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 获取番茄钟工作时长（分钟）
 * @author chiangyang
 */
int DesktopPetManager::pomodoroWorkMinutes() const {
    return qBound(5, ConfigManager::instance()->value("pet/pomodoroWorkMin", 25).toInt(), 180);
}

/**
 * @brief 设置番茄钟工作时长（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setPomodoroWorkMinutes(int minutes) {
    const int clamped = qBound(5, minutes, 180);
    ConfigManager::instance()->setValue("pet/pomodoroWorkMin", clamped);
    ConfigManager::instance()->sync();
}

/**
 * @brief 获取番茄钟休息时长（分钟）
 * @author chiangyang
 */
int DesktopPetManager::pomodoroBreakMinutes() const {
    return qBound(1, ConfigManager::instance()->value("pet/pomodoroBreakMin", 5).toInt(), 120);
}

/**
 * @brief 设置番茄钟休息时长（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setPomodoroBreakMinutes(int minutes) {
    const int clamped = qBound(1, minutes, 120);
    ConfigManager::instance()->setValue("pet/pomodoroBreakMin", clamped);
    ConfigManager::instance()->sync();
}

/**
 * @brief 获取待机打盹是否开启
 * @author chiangyang
 */
bool DesktopPetManager::idleSleepEnabled() const {
    return ConfigManager::instance()->value("pet/idleSleepEnabled", true).toBool();
}

/**
 * @brief 设置待机打盹开关（即时生效并持久化，关闭时立即唤醒）
 * @author chiangyang
 */
void DesktopPetManager::setIdleSleepEnabled(bool enabled) {
    if (idleSleepEnabled() == enabled) {
        return;
    }
    ConfigManager::instance()->setValue("pet/idleSleepEnabled", enabled);
    ConfigManager::instance()->sync();
    if (m_stateMachine) {
        m_stateMachine->setIdleEnabled(enabled);
    }
    LOG_INFO(QString("DesktopPetManager: idle sleep %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 获取无互动进入打盹的分钟数
 * @author chiangyang
 */
int DesktopPetManager::idleSleepMinutes() const {
    return qBound(1, ConfigManager::instance()->value("pet/idleSleepMin", 5).toInt(), 120);
}

/**
 * @brief 设置无互动进入打盹的分钟数（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setIdleSleepMinutes(int minutes) {
    const int clamped = qBound(1, minutes, 120);
    ConfigManager::instance()->setValue("pet/idleSleepMin", clamped);
    ConfigManager::instance()->sync();
    if (m_stateMachine) {
        m_stateMachine->setIdleMinutes(clamped);
    }
}

/**
 * @brief 获取系统状态提醒是否开启
 * @author chiangyang
 */
bool DesktopPetManager::sysMonitorEnabled() const {
    return ConfigManager::instance()->value("pet/sysMonitorEnabled", false).toBool();
}

/**
 * @brief 设置系统状态提醒开关（即时生效并持久化，关闭时清除宠物角标）
 * @author chiangyang
 */
void DesktopPetManager::setSysMonitorEnabled(bool enabled) {
    if (sysMonitorEnabled() == enabled) {
        return;
    }
    ConfigManager::instance()->setValue("pet/sysMonitorEnabled", enabled);
    ConfigManager::instance()->sync();
    // stop 会在告警中先发出 None 让角标清除；无需单独处理
    if (m_sysMonitor) {
        if (enabled && m_enabled) {
            m_sysMonitor->start();
        } else {
            m_sysMonitor->stop();
        }
    }
    LOG_INFO(QString("DesktopPetManager: sys monitor %1").arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 获取手动选择的宠物装饰
 * @author chiangyang
 */
PetWindow::Accessory DesktopPetManager::accessory() const {
    const int v = ConfigManager::instance()->value("pet/accessory", 0).toInt();
    return static_cast<PetWindow::Accessory>(qBound(0, v, PetWindow::accessoryCount() - 1));
}

/**
 * @brief 设置宠物装饰（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setAccessory(PetWindow::Accessory accessory) {
    ConfigManager::instance()->setValue("pet/accessory", int(accessory));
    ConfigManager::instance()->sync();
    applyAccessory();
}

/**
 * @brief 获取节日彩蛋是否开启
 * @author chiangyang
 */
bool DesktopPetManager::festivalEnabled() const {
    return ConfigManager::instance()->value("pet/festivalEnabled", true).toBool();
}

/**
 * @brief 设置节日彩蛋开关（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setFestivalEnabled(bool enabled) {
    if (festivalEnabled() == enabled) {
        return;
    }
    ConfigManager::instance()->setValue("pet/festivalEnabled", enabled);
    ConfigManager::instance()->sync();
    applyAccessory();
    scheduleFestivalCheck();
    LOG_INFO(QString("DesktopPetManager: festival surprise %1")
                 .arg(enabled ? "enabled" : "disabled"));
}

/**
 * @brief 获取宠物生日（MM-dd）
 * @author chiangyang
 */
QString DesktopPetManager::birthday() const {
    return ConfigManager::instance()->value("pet/birthday", QString()).toString();
}

/**
 * @brief 设置宠物生日（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setBirthday(const QString &monthDay) {
    if (birthday() == monthDay) {
        return;
    }
    ConfigManager::instance()->setValue("pet/birthday", monthDay);
    ConfigManager::instance()->sync();
    applyAccessory();
}

/**
 * @brief 将生效装饰应用到宠物窗口（节日装饰优先于手动装饰）
 * @author chiangyang
 */
void DesktopPetManager::applyAccessory() {
    if (!m_petWindow) {
        return;
    }
    const PetWindow::Accessory festival = festivalAccessory();
    m_petWindow->setAccessory(festival != PetWindow::Accessory::None ? festival : accessory());
}

/**
 * @brief 按当前日期判定应自动佩戴的节日装饰
 *
 * 区间按公历固定：圣诞 12-24~12-25、万圣 10-31~11-01、元旦 12-31~01-01、
 * 生日（pet/birthday 的 MM-dd）当天。命中即覆盖手动装饰，区间结束自然回落。
 * @author chiangyang
 */
PetWindow::Accessory DesktopPetManager::festivalAccessory() const {
    if (!festivalEnabled()) {
        return PetWindow::Accessory::None;
    }
    const QDate today = QDate::currentDate();
    const int month = today.month();
    const int day = today.day();
    if (month == 12 && (day == 24 || day == 25)) {
        return PetWindow::Accessory::SantaHat;
    }
    if ((month == 10 && day == 31) || (month == 11 && day == 1)) {
        return PetWindow::Accessory::PumpkinHat;
    }
    if ((month == 12 && day == 31) || (month == 1 && day == 1)) {
        return PetWindow::Accessory::PartyHat;
    }
    const QString birth = birthday();
    if (!birth.isEmpty()) {
        const QString todayKey = QString("%1-%2")
                                     .arg(month, 2, 10, QChar('0'))
                                     .arg(day, 2, 10, QChar('0'));
        if (birth == todayKey) {
            return PetWindow::Accessory::PartyHat;
        }
    }
    return PetWindow::Accessory::None;
}

/**
 * @brief 排定下一次节日判定（单次定时器排到次日 0 点后重排）
 * @author chiangyang
 */
void DesktopPetManager::scheduleFestivalCheck() {
    if (!m_festivalTimer) {
        m_festivalTimer = new QTimer(this);
        m_festivalTimer->setSingleShot(true);
        connect(m_festivalTimer, &QTimer::timeout, this, [this]() {
            applyAccessory();
            scheduleFestivalCheck();
        });
    }
    if (!festivalEnabled()) {
        m_festivalTimer->stop();
        return;
    }
    // 排到次日 0 点后 1s，确保系统日期已完成切换
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime midnight(QDate::currentDate().addDays(1), QTime(0, 0));
    const qint64 ms = qBound<qint64>(qint64(1000), now.msecsTo(midnight) + 1000,
                                     qint64(24) * 3600 * 1000);
    m_festivalTimer->start(int(ms));
}

/**
 * @brief 弹出番茄钟提示卡（宠物跳跃 + 卡片贴合宠物上方）
 * @author chiangyang
 */
void DesktopPetManager::showPomodoroCard(bool breakDue) {
    if (!m_enabled || !m_petWindow) {
        return;
    }
    if (!m_pomodoroCard) {
        m_pomodoroCard = new PomodoroCard();
        connect(m_pomodoroCard, &PomodoroCard::breakTaken, this, [this]() {
            m_pomodoroCard->hide();
            if (m_reminder) {
                m_reminder->takePomodoroBreak();
            }
        });
        connect(m_pomodoroCard, &PomodoroCard::skipped, this, [this]() {
            m_pomodoroCard->hide();
            if (m_reminder) {
                m_reminder->skipPomodoroBreak();
            }
        });
        connect(m_pomodoroCard, &PomodoroCard::resumed, this, [this]() {
            m_pomodoroCard->hide();
            if (m_reminder) {
                m_reminder->resumePomodoroWork();
            }
        });
    }
    // 到点提醒先唤醒打盹中的宠物，保证跳跃可见
    if (m_stateMachine) {
        m_stateMachine->noteInteraction();
    }
    m_petWindow->playJump();
    if (breakDue) {
        m_pomodoroCard->showBreakDue(pomodoroBreakMinutes());
    } else {
        m_pomodoroCard->showWorkDue();
    }
    positionPomodoroCard();
}

/**
 * @brief 将番茄钟提示卡摆到宠物上方（空间不足时移到下方）
 * @author chiangyang
 */
void DesktopPetManager::positionPomodoroCard() {
    if (!m_pomodoroCard || !m_pomodoroCard->isVisible() || !m_petWindow) {
        return;
    }
    QScreen *screen = m_petWindow->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry()
                                   : QRect(0, 0, 1920, 1080);
    const int margin = PetUi::scaled(8);
    const int gap = PetUi::scaled(6);
    const QSize size = m_pomodoroCard->size();
    const QRect pet = m_petWindow->geometry();
    const int petCenterX = pet.center().x();

    int x = petCenterX - size.width() / 2;
    int y = pet.top() - size.height() - gap;
    bool arrowBelow = true;
    if (y < available.top() + margin) {
        // 上方放不下时翻到宠物下方，小三角朝上
        y = pet.bottom() + gap;
        arrowBelow = false;
    }
    x = qBound(available.left() + margin, x, available.right() - size.width() - margin);
    y = qBound(available.top() + margin, y, available.bottom() - size.height() - margin);
    m_pomodoroCard->setBubblePlacement(arrowBelow, petCenterX - x);
    m_pomodoroCard->move(x, y);
}
