#include "SettingsPetTab.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../pet/DesktopPetManager.h"
#include "../../pet/PetWindow.h"
#include "../../pet/PetUiScale.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
// 样式名 i18n 键与英文兜底文案（下标对应 PetWindow::PetStyle 枚举值）
constexpr const char *kStyleKeys[] = {
    "pet.style.cat", "pet.style.shiba", "pet.style.panda",
    "pet.style.bunny", "pet.style.penguin", "pet.style.frog"
};
constexpr const char *kStyleFallbacks[] = {
    "Orange Cat", "Shiba", "Panda", "Bunny", "Penguin", "Frog"
};

/**
 * @brief 样式图标按钮的样式表（文字色随主题切换，选中态用主题强调色描边）
 * @author chiangyang
 */
QString styleButtonStyle() {
    // 文字色与 QCheckBox/QGroupBox 同款主题分支：按钮文字不指定色时会回落到
    // Qt 默认调色板（跟随系统主题而非应用内主题），深色主题下黑字不可见
    const bool dark = (StyleManager::themeMode() == StyleManager::ThemeMode::Dark);
    return QString(
        "QToolButton { border: 2px solid transparent; border-radius: 0.4em;"
        "  padding: 0.2em; background: transparent; color: %1; }"
        "QToolButton:hover { background: rgba(127, 127, 127, 60); }"
        "QToolButton:checked { border-color: %2; }")
        .arg(dark ? QStringLiteral("#f2f2f2") : QStringLiteral("#000"),
             StyleManager::getToolbarButtonCheckedColor().name());
}
}

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

    // ===== 外观样式：平铺图标按钮，点击即切换（图标为各样式预览图） =====
    m_styleLabel = new QLabel(tm->get("pet.styleLabel", "Pet Style"), m_petGroup);

    m_styleGroup = new QButtonGroup(this);
    m_styleGroup->setExclusive(true);
    // 预览图按显示尺寸 2 倍渲染保证清晰，显示尺寸随 DPI 缩放（96 逻辑基准）
    const int iconSize = PetUi::scaled(48);
    for (int i = 0; i < PetWindow::petStyleCount(); ++i) {
        const auto style = static_cast<PetWindow::PetStyle>(i);
        auto *btn = new QToolButton(m_petGroup);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(QIcon(PetWindow::renderStylePreview(style, iconSize * 2)));
        btn->setIconSize(QSize(iconSize, iconSize));
        btn->setText(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        btn->setToolTip(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        btn->setStyleSheet(styleButtonStyle());
        m_styleButtons.append(btn);
        m_styleGroup->addButton(btn, i);
    }
    m_styleGroup->button(int(DesktopPetManager::instance()->petStyle()))->setChecked(true);
    connect(m_styleGroup, &QButtonGroup::idClicked, this, [](int id) {
        DesktopPetManager::instance()->setPetStyle(static_cast<PetWindow::PetStyle>(id));
    });

    QHBoxLayout *styleRow = new QHBoxLayout();
    styleRow->setSpacing(PetUi::scaled(6));
    for (QToolButton *btn : m_styleButtons) {
        styleRow->addWidget(btn);
    }
    styleRow->addStretch();

    m_btnResetPos = new QPushButton(tm->get("pet.resetPosition", "Reset Pet Position"), m_petGroup);
    m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
    m_btnResetPos->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_btnResetPos, &QPushButton::clicked, this, []() {
        DesktopPetManager::instance()->resetPetPosition();
    });

    m_descLabel = new QLabel(tm->get("pet.description",
        "When enabled, screenshots fly into the pet's mouth after copy, pin, or taking a snapshot while recording, and copied text flies in as well. \nClick the pet to open the clothesline and view recent screenshots and copied text."), m_petGroup);
    m_descLabel->setWordWrap(false);

    petLayout->addWidget(m_enablePetCheck);
    petLayout->addWidget(m_styleLabel);
    petLayout->addLayout(styleRow);
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
    if (m_styleLabel) m_styleLabel->setText(tm->get("pet.styleLabel", "Pet Style"));
    for (int i = 0; i < m_styleButtons.size() && i < PetWindow::petStyleCount(); ++i) {
        m_styleButtons[i]->setText(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        m_styleButtons[i]->setToolTip(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
    }
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
    for (QToolButton *btn : m_styleButtons) {
        btn->setStyleSheet(styleButtonStyle());
    }
    if (m_btnResetPos) m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
}

void SettingsPetTab::applyTabBackground(const QColor &color) {
    setStyleSheet(QString("#petTabBg { background-color: %1; }").arg(color.name()));
}
