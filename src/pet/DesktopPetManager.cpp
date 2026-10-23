#include "DesktopPetManager.h"
#include "PetWindow.h"
#include "FlyAnimationWidget.h"
#include "ClotheslineWindow.h"
#include "../core/ConfigManager.h"
#include "../log/Logger.h"

#include <QGuiApplication>
#include <QScreen>

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
    } else {
        if (m_clothesline) {
            m_clothesline->close();
            m_clothesline = nullptr;
        }
        if (m_petWindow) {
            m_petWindow->close();
            m_petWindow = nullptr;
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

    // 位置恢复：配置中有持久化位置则使用，否则落在默认右下角
    QSettings *settings = ConfigManager::instance()->getSettings();
    const QPoint pos = settings->value("pet/pos", defaultPetPosition()).toPoint();
    // 位置兜底：避免配置文件里的旧坐标在多屏变化后落到可见区域外
    const QRect virtualGeo = QGuiApplication::primaryScreen()->virtualGeometry();
    const QPoint safePos = virtualGeo.contains(pos) ? pos : defaultPetPosition();
    m_petWindow->move(safePos);

    // 拖动结束持久化位置
    connect(m_petWindow, &PetWindow::dragFinished, this, [](const QPoint &newPos) {
        ConfigManager::instance()->setValue("pet/pos", newPos);
        ConfigManager::instance()->sync();
    });

    // 点击切换晾衣绳
    connect(m_petWindow, &PetWindow::clicked, this, &DesktopPetManager::toggleClothesline);

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

    // 宠物提前张嘴等待食物
    m_petWindow->openMouth();

    // 飞行动画：从截图区域缩小飞到宠物嘴边，到达后宠物咀嚼
    FlyAnimationWidget *fly = new FlyAnimationWidget(pixmap, sourceGlobalRect,
                                                     m_petWindow->mouthCenterGlobal());
    connect(fly, &FlyAnimationWidget::arrived, m_petWindow, &PetWindow::chew);
    fly->start();
}

/**
 * @brief 切换晾衣绳窗口显隐（宠物点击槽）
 * @author chiangyang
 */
void DesktopPetManager::toggleClothesline() {
    if (m_clothesline && m_clothesline->isVisible()) {
        m_clothesline->close();
        m_clothesline = nullptr;
        return;
    }

    if (!m_petWindow) {
        return;
    }
    m_clothesline = new ClotheslineWindow();
    m_clothesline->showNear(m_petWindow);
}

/**
 * @brief 将宠物移回默认位置并清除持久化位置
 * @author chiangyang
 */
void DesktopPetManager::resetPetPosition() {
    QSettings *settings = ConfigManager::instance()->getSettings();
    settings->remove("pet/pos");
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
