#include "PetStateMachine.h"
#include "PetWindow.h"

#include <QTimer>

namespace {
// 待机检测周期：15s。打盹不是精确到秒的需求，低频检查足够
constexpr int kIdleCheckMs = 15 * 1000;
}

/**
 * @brief 构造函数
 * @author chiangyang
 */
PetStateMachine::PetStateMachine(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kIdleCheckMs);
    connect(m_timer, &QTimer::timeout, this, &PetStateMachine::checkIdle);
}

/**
 * @brief 设置管理的宠物窗口
 * @author chiangyang
 */
void PetStateMachine::setPet(PetWindow *pet) {
    m_pet = pet;
}

/**
 * @brief 启动待机检测
 * @author chiangyang
 */
void PetStateMachine::start() {
    m_sinceInteract.start();
    m_timer->start();
}

/**
 * @brief 停止检测并唤醒
 * @author chiangyang
 */
void PetStateMachine::stop() {
    m_timer->stop();
    if (m_pet && m_pet->isSleeping()) {
        m_pet->setSleeping(false);
    }
}

/**
 * @brief 记录一次互动：唤醒并重置待机计时
 * @author chiangyang
 */
void PetStateMachine::noteInteraction() {
    m_sinceInteract.restart();
    if (m_pet && m_pet->isSleeping()) {
        m_pet->setSleeping(false);
    }
}

/**
 * @brief 设置待机打盹开关（关闭时立即唤醒并不再进入打盹）
 * @author chiangyang
 */
void PetStateMachine::setIdleEnabled(bool enabled) {
    m_idleEnabled = enabled;
    if (!enabled && m_pet && m_pet->isSleeping()) {
        m_pet->setSleeping(false);
    }
}

/**
 * @brief 设置无互动进入打盹的分钟数
 * @author chiangyang
 */
void PetStateMachine::setIdleMinutes(int minutes) {
    m_idleMinutes = qMax(1, minutes);
}

/**
 * @brief 周期检查：距上次互动超过阈值则进入打盹
 * @author chiangyang
 */
void PetStateMachine::checkIdle() {
    if (!m_idleEnabled || !m_pet || m_pet->isSleeping()) {
        return;
    }
    if (m_sinceInteract.elapsed() >= qint64(m_idleMinutes) * 60 * 1000) {
        m_pet->setSleeping(true);
    }
}
