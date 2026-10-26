#include "DesktopPetManager.h"
#include "PetWindow.h"
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
    m_petWindow->setStyle(petStyle());

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
 * @brief 获取当前宠物外观样式（持久化于 pet/style）
 * @author chiangyang
 */
PetWindow::PetStyle DesktopPetManager::petStyle() const {
    const int v = ConfigManager::instance()->value("pet/style", 0).toInt();
    return static_cast<PetWindow::PetStyle>(qBound(0, v, PetWindow::petStyleCount() - 1));
}

/**
 * @brief 设置宠物外观样式（即时生效并持久化）
 * @author chiangyang
 */
void DesktopPetManager::setPetStyle(PetWindow::PetStyle style) {
    ConfigManager::instance()->setValue("pet/style", int(style));
    ConfigManager::instance()->sync();

    if (m_petWindow) {
        m_petWindow->setStyle(style);
    }
    LOG_INFO(QString("DesktopPetManager: pet style set to %1").arg(int(style)));
}
