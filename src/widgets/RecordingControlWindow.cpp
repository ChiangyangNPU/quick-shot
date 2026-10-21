#include "RecordingControlWindow.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QSpacerItem>
#include "../core/ConfigManager.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../recording/ScreenRecorder.h"

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
RecordingControlWindow::RecordingControlWindow(QWidget *parent) : QWidget(parent) {
    LOG_INFO("RecordingControlWindow instance created");
    // 设置窗口标志，与主工具栏一致
    setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    // 圆角矩形需要透明背景，否则圆角外区域会被系统默认色填充成直角
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::ArrowCursor);
    setupUi();
    retranslateUi();
    updateButtonStyles();
}

// ============================================================
// 绘制事件
// ============================================================

/**
 * @brief 绘制圆角矩形背景
 *
 * 当窗口设置了 WA_TranslucentBackground 后，QSS 的 background-color 不会自动绘制，
 * 导致圆角矩形没有底色。这里用 QPainter 主动绘制圆角矩形背景，
 * 既保证圆角外区域透明（不露出直角底层），又保证圆角内有底色。
 *
 * 半径与 QSS 中的 border-radius: 0.24em 一致，按当前字体 em 高度换算。
 * @param event 绘制事件
 * @author chiangyang
 */
void RecordingControlWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(StyleManager::getRecordControlBgColor());
    int radius = qRound(fontMetrics().height() * 0.24);
    painter.drawRoundedRect(rect(), radius, radius);
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

    // 输出格式选择（MP4 / GIF），选择结果持久化到配置
    // 构建步骤与 BaseToolBar 的形状下拉框（矩形/椭圆/三角形）保持一致：
    // 建控件 → 填条目 → setCurrentIndex → 设样式 → 按文本算最小宽度 → 高亮弹窗 → 入布局
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("MP4", QStringLiteral("mp4"));
    if (ScreenRecorder::isGifSupported()) {
        m_formatCombo->addItem("GIF", QStringLiteral("gif"));
    }
    m_formatCombo->setCurrentIndex(0);
    m_formatCombo->setStyleSheet(StyleManager::getComboBoxStyle());
    updateFormatComboWidth();
    StyleManager::useHighlightOnlyPopup(m_formatCombo);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        ConfigManager::instance()->setValue("record/format", selectedFormat());
        LOG_INFO(QString("RecordingControlWindow: output format=%1").arg(selectedFormat()));
        updateAudioAvailability();
    });
    m_mainLayout->addWidget(m_formatCombo);
    setSelectedFormat(ConfigManager::instance()->value("record/format", "mp4").toString());

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
    if (m_formatCombo) {
        const int mp4Index = m_formatCombo->findData(QStringLiteral("mp4"));
        if (mp4Index >= 0) m_formatCombo->setItemText(mp4Index, tm->get("record.format.mp4", "MP4"));
        const int gifIndex = m_formatCombo->findData(QStringLiteral("gif"));
        if (gifIndex >= 0) m_formatCombo->setItemText(gifIndex, tm->get("record.format.gif", "GIF"));
    }
    if (chkSystemAudio) {
        chkSystemAudio->setText(tm->get("record.systemAudio", "System Audio"));
    }
    if (chkMicrophone) {
        chkMicrophone->setText(tm->get("record.microphone", "Microphone"));
    }
    // 条目文本变化后重新计算宽度，避免被省略为 "..."
    updateFormatComboWidth();
}

/**
 * @brief 按条目文本重算格式下拉框最小宽度
 *
 * 与 BaseToolBar 的 adjustShapeComboPopupWidth 保持一致：
 * 文本宽 + 3 个字符宽（QSS 内边距 + 下拉箭头区 + 边框），
 * 弹窗视图宽度同样不小于本体，否则弹窗条目也会被省略。
 * @author chiangyang
 */
void RecordingControlWindow::updateFormatComboWidth() {
    if (!m_formatCombo) return;
    int maxItemWidth = 0;
    const QFontMetrics fm(m_formatCombo->font());
    for (int i = 0; i < m_formatCombo->count(); ++i) {
        maxItemWidth = qMax(maxItemWidth, fm.horizontalAdvance(m_formatCombo->itemText(i)));
    }
    const int minComboWidth = maxItemWidth + qRound(fm.horizontalAdvance('M') * 3.0);
    m_formatCombo->setMinimumWidth(minComboWidth);
    if (QAbstractItemView *view = m_formatCombo->view()) {
        view->setMinimumWidth(qMax(maxItemWidth + qRound(fm.horizontalAdvance('M') * 2.0),
                                   minComboWidth));
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
    } else {
        btnStart->setEnabled(false);
        btnStop->setEnabled(true);

        if (isPaused) {
            btnPause->setEnabled(false);
            btnResume->setEnabled(true);
        } else {
            btnPause->setEnabled(true);
            btnResume->setEnabled(false);
        }
    }
    if (m_formatCombo) {
        m_formatCombo->setEnabled(!isRecording);
    }
    updateAudioAvailability();
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
 * @brief 获取当前选择的输出格式
 * @return "mp4" 或 "gif"
 * @author chiangyang
 */
QString RecordingControlWindow::selectedFormat() const {
    if (!m_formatCombo) {
        return QStringLiteral("mp4");
    }
    const QString value = m_formatCombo->currentData().toString();
    return value.isEmpty() ? QStringLiteral("mp4") : value;
}

/**
 * @brief 设置输出格式（同时写入配置）
 *
 * 若当前平台不支持 GIF，则回退到 MP4。
 * @param format "mp4" 或 "gif"
 * @author chiangyang
 */
void RecordingControlWindow::setSelectedFormat(const QString &format) {
    if (!m_formatCombo) {
        return;
    }
    int index = m_formatCombo->findData(format);
    if (index < 0) {
        index = m_formatCombo->findData(QStringLiteral("mp4"));
    }
    if (index < 0) {
        return;
    }
    m_formatCombo->setCurrentIndex(index);
    ConfigManager::instance()->setValue("record/format", selectedFormat());
    updateAudioAvailability();
}

/**
 * @brief 根据当前格式与录制状态更新音频选项可用性
 *
 * GIF 无音轨：选择 GIF 时取消勾选并禁用音频复选框。
 * @author chiangyang
 */
void RecordingControlWindow::updateAudioAvailability() {
    const bool gifSelected = (selectedFormat() == QStringLiteral("gif"));
    const bool canEdit = !m_isRecording && !gifSelected;
    if (chkSystemAudio) {
        if (gifSelected) {
            chkSystemAudio->setChecked(false);
        }
        chkSystemAudio->setEnabled(canEdit);
    }
    if (chkMicrophone) {
        if (gifSelected) {
            chkMicrophone->setChecked(false);
        }
        chkMicrophone->setEnabled(canEdit);
    }
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
    if (m_formatCombo) {
        m_formatCombo->setStyleSheet(StyleManager::getComboBoxStyle());
    }

    // 调整窗口大小
    layout()->activate();
    adjustSize();
}