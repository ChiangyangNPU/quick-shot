#include "SettingsPetTab.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../pet/DesktopPetManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsPetTab::SettingsPetTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsPetTab::setupUi() {
    TranslationManager *tm = TranslationManager::instance();

    // 本控件即选项卡根容器（#objectName 选择器只匹配自身，不影响子控件原生渲染）
    setObjectName("petTabBg");
    setStyleSheet(QString("#petTabBg { background-color: %1; }").arg(StyleManager::getTabWidgetBgColor().name()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // ===== 桌面宠物组 =====
    m_petGroup = new QGroupBox(tm->get("pet.group", "Desktop Pet"), this);
    m_petGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *petLayout = new QVBoxLayout(m_petGroup);

    m_enablePetCheck = new QCheckBox(tm->get("pet.enable", "Enable Desktop Pet"), m_petGroup);
    m_enablePetCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_enablePetCheck->setChecked(DesktopPetManager::instance()->isEnabled());
    connect(m_enablePetCheck, &QCheckBox::toggled, this, [](bool checked) {
        DesktopPetManager::instance()->setEnabled(checked);
    });

    m_btnResetPos = new QPushButton(tm->get("pet.resetPosition", "Reset Pet Position"), m_petGroup);
    m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(m_btnResetPos, &QPushButton::clicked, this, []() {
        DesktopPetManager::instance()->resetPetPosition();
    });

    m_descLabel = new QLabel(tm->get("pet.description",
        "When enabled, screenshots fly into the pet after copy/pin. Click the pet to view the clothesline of recent screenshots and copied text."), m_petGroup);
    m_descLabel->setWordWrap(true);

    petLayout->addWidget(m_enablePetCheck);
    petLayout->addWidget(m_btnResetPos);
    petLayout->addWidget(m_descLabel);

    layout->addWidget(m_petGroup);
    layout->addStretch();
}

// ============================================================
// 语言切换
// ============================================================

void SettingsPetTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();
    if (m_petGroup) m_petGroup->setTitle(tm->get("pet.group", "Desktop Pet"));
    if (m_enablePetCheck) m_enablePetCheck->setText(tm->get("pet.enable", "Enable Desktop Pet"));
    if (m_btnResetPos) m_btnResetPos->setText(tm->get("pet.resetPosition", "Reset Pet Position"));
    if (m_descLabel) m_descLabel->setText(tm->get("pet.description",
        "When enabled, screenshots fly into the pet after copy/pin. Click the pet to view the clothesline of recent screenshots and copied text."));
}

// ============================================================
// 样式与布局
// ============================================================

void SettingsPetTab::refreshStyles() {
    if (m_petGroup) m_petGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_enablePetCheck) m_enablePetCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    if (m_btnResetPos) m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
}

void SettingsPetTab::applyTabBackground(const QColor &color) {
    setStyleSheet(QString("#petTabBg { background-color: %1; }").arg(color.name()));
}
