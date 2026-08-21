#include "RecordingControlWindow.h"
#include <QApplication>
#include <QSpacerItem>
#include "../core/ConfigManager.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
RecordingControlWindow::RecordingControlWindow(QWidget *parent) : QWidget(parent) {
    LOG_INFO("RecordingControlWindow instance created");
    // 设置窗口标志，与主工具栏一致
    setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::ArrowCursor);
    setupUi();
    retranslateUi();
    updateButtonStyles();
}

/**
 * @brief 设置 UI 布局
 * @author chiangyang
 */
void RecordingControlWindow::setupUi() {
    // 设置窗口样式
    setStyleSheet(StyleManager::getWindowStyle());
    setAttribute(Qt::WA_StyledBackground);

    // 创建主布局
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(5, 5, 5, 5);
    m_mainLayout->setSpacing(5);

    // 创建控制按钮
    btnStart = new QPushButton(this);
    btnPause = new QPushButton(this);
    btnResume = new QPushButton(this);
    btnStop = new QPushButton(this);

    // 设置按钮样式
    StyleManager::applyNormalButtonStyle(btnStart);
    StyleManager::applyNormalButtonStyle(btnPause);
    StyleManager::applyNormalButtonStyle(btnResume);
    StyleManager::applyNormalButtonStyle(btnStop);

    // 连接信号
    connect(btnStart, &QPushButton::clicked, this, &RecordingControlWindow::startRequested);
    connect(btnPause, &QPushButton::clicked, this, &RecordingControlWindow::pauseRequested);
    connect(btnResume, &QPushButton::clicked, this, &RecordingControlWindow::resumeRequested);
    connect(btnStop, &QPushButton::clicked, this, &RecordingControlWindow::stopRequested);

    // 将按钮添加到布局
    m_mainLayout->addWidget(btnStart);
    m_mainLayout->addWidget(btnPause);
    m_mainLayout->addWidget(btnResume);
    m_mainLayout->addWidget(btnStop);

    // 音频录制选项
    chkSystemAudio = new QCheckBox(this);
    chkMicrophone = new QCheckBox(this);
    bool sysAudioEnabled = ConfigManager::instance()->value("record/systemAudio", false).toBool();
    bool micEnabled = ConfigManager::instance()->value("record/microphone", false).toBool();
    chkSystemAudio->setChecked(sysAudioEnabled);
    chkMicrophone->setChecked(micEnabled);

    // 复选框样式
    chkSystemAudio->setStyleSheet(StyleManager::getCheckBoxStyle());
    chkMicrophone->setStyleSheet(StyleManager::getCheckBoxStyle());

    connect(chkSystemAudio, &QCheckBox::toggled, this, [this](bool checked) {
        LOG_INFO(QString("RecordingControlWindow: system audio toggled=%1").arg(checked));
        emit systemAudioToggled(checked);
    });
    connect(chkMicrophone, &QCheckBox::toggled, this, [this](bool checked) {
        LOG_INFO(QString("RecordingControlWindow: microphone toggled=%1").arg(checked));
        emit microphoneToggled(checked);
    });

    m_mainLayout->addWidget(chkSystemAudio);
    m_mainLayout->addWidget(chkMicrophone);

    // 添加分隔符
    m_mainLayout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Minimum));

    // 分辨率选择已移除，输出分辨率自动匹配选区大小

    // 添加分隔符
    m_mainLayout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Minimum));



    // 初始状态
    updateButtonStates(false, false);
}

/**
 * @brief 重新翻译用户界面
 * @author chiangyang
 */
void RecordingControlWindow::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();
    btnStart->setText(tm->get("record.start", "Start"));
    btnPause->setText(tm->get("record.pause", "Pause"));
    btnResume->setText(tm->get("record.resume", "Resume"));
    btnStop->setText(tm->get("record.stop", "Stop"));
    if (chkSystemAudio) {
        chkSystemAudio->setText(tm->get("record.systemAudio", "System Audio"));
    }
    if (chkMicrophone) {
        chkMicrophone->setText(tm->get("record.microphone", "Microphone"));
    }
}

/**
 * @brief 根据录制状态更新按钮启用/禁用
 * @param isRecording 是否正在录制
 * @param isPaused 是否暂停
 * @author chiangyang
 */
void RecordingControlWindow::updateButtonStates(bool isRecording, bool isPaused) {
    if (!isRecording) {
        btnStart->setEnabled(true);
        btnPause->setEnabled(false);
        btnResume->setEnabled(false);
        btnStop->setEnabled(false);
        if (chkSystemAudio) chkSystemAudio->setEnabled(true);
        if (chkMicrophone) chkMicrophone->setEnabled(true);
    } else {
        btnStart->setEnabled(false);
        btnStop->setEnabled(true);
        if (chkSystemAudio) chkSystemAudio->setEnabled(false);
        if (chkMicrophone) chkMicrophone->setEnabled(false);

        if (isPaused) {
            btnPause->setEnabled(false);
            btnResume->setEnabled(true);
        } else {
            btnPause->setEnabled(true);
            btnResume->setEnabled(false);
        }
    }
}

/**
 * @brief 设置录制状态
 * @param recording 是否正在录制
 * @author chiangyang
 */
void RecordingControlWindow::setRecording(bool recording) {
    LOG_INFO(QString("RecordingControlWindow: setRecording=%1").arg(recording));
    m_isRecording = recording;
    updateButtonStates(recording, m_isPaused);
}

/**
 * @brief 设置暂停状态
 * @param paused 是否暂停
 * @author chiangyang
 */
void RecordingControlWindow::setPaused(bool paused) {
    LOG_INFO(QString("RecordingControlWindow: setPaused=%1").arg(paused));
    m_isPaused = paused;
    updateButtonStates(m_isRecording, paused);
}

/**
 * @brief 更新按钮显示样式
 * @author chiangyang
 */
void RecordingControlWindow::updateBackgroundStyle() {
    setStyleSheet(StyleManager::getWindowStyle());
    update();
}

/**
 * @brief 更新按钮显示样式（图标/文字模式）
 * @author chiangyang
 */
void RecordingControlWindow::updateButtonStyles() {
    QString buttonStyle = StyleManager::getToolbarButtonStyle();
    TranslationManager *tm = TranslationManager::instance();
    
    // 更新控制按钮
    if (buttonStyle == "icon") {
        // 图标模式
        btnStart->setText("");
        btnStart->setIcon(StyleManager::loadSvgIcon(":/icons/start.svg"));
        btnStart->setToolTip(tm->get("record.start", "Start"));
        
        btnPause->setText("");
        btnPause->setIcon(StyleManager::loadSvgIcon(":/icons/pause.svg"));
        btnPause->setToolTip(tm->get("record.pause", "Pause"));
        
        btnResume->setText("");
        btnResume->setIcon(StyleManager::loadSvgIcon(":/icons/resume.svg"));
        btnResume->setToolTip(tm->get("record.resume", "Resume"));
        
        btnStop->setText("");
        btnStop->setIcon(StyleManager::loadSvgIcon(":/icons/stop.svg"));
        btnStop->setToolTip(tm->get("record.stop", "Stop"));
    } else {
        // 文字模式
        btnStart->setText(tm->get("record.start", "Start"));
        btnStart->setIcon(QIcon());
        btnStart->setToolTip("");
        btnPause->setText(tm->get("record.pause", "Pause"));
        btnPause->setIcon(QIcon());
        btnPause->setToolTip("");
        btnResume->setText(tm->get("record.resume", "Resume"));
        btnResume->setIcon(QIcon());
        btnResume->setToolTip("");
        btnStop->setText(tm->get("record.stop", "Stop"));
        btnStop->setIcon(QIcon());
        btnStop->setToolTip("");
    }
    
    // 重新应用样式
    StyleManager::applyNormalButtonStyle(btnStart);
    StyleManager::applyNormalButtonStyle(btnPause);
    StyleManager::applyNormalButtonStyle(btnResume);
    StyleManager::applyNormalButtonStyle(btnStop);

    // 强制更新按钮几何，确保切换文字/图标模式后尺寸正确
    btnStart->updateGeometry();
    btnPause->updateGeometry();
    btnResume->updateGeometry();
    btnStop->updateGeometry();

    // 更新复选框样式
    if (chkSystemAudio) {
        chkSystemAudio->setStyleSheet(StyleManager::getCheckBoxStyle());
    }
    if (chkMicrophone) {
        chkMicrophone->setStyleSheet(StyleManager::getCheckBoxStyle());
    }

    // 调整窗口大小
    layout()->activate();
    adjustSize();
}