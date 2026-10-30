#include "SettingsPetTab.h"
#include "../../core/TranslationManager.h"
#include "../../core/StyleManager.h"
#include "../../pet/DesktopPetManager.h"
#include "../../pet/PetWindow.h"
#include "../../pet/PetTheme.h"
#include "../../pet/PetUiScale.h"
#include "../FlowLayout.h"
#include "../MessageBox.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSpinBox>
#include <QFileDialog>

namespace {
// 自定义宠物按钮在互斥组中的 id 偏移（避免与 PetStyle 枚举值 0~15 冲突）
constexpr int kCustomIdBase = 1000;

// 样式名 i18n 键与英文兜底文案（下标对应 PetWindow::PetStyle 枚举值；
// 经典组 = 猫/熊猫/企鹅/青蛙，生肖组含全部 12 属相，兔/狗复用 Bunny/Shiba 造型）
constexpr const char *kStyleKeys[] = {
    "pet.style.cat", "pet.style.shiba", "pet.style.panda",
    "pet.style.bunny", "pet.style.penguin", "pet.style.frog",
    "pet.style.mouse", "pet.style.ox", "pet.style.tiger", "pet.style.dragon",
    "pet.style.snake", "pet.style.horse", "pet.style.goat",
    "pet.style.monkey", "pet.style.rooster", "pet.style.pig"
};
constexpr const char *kStyleFallbacks[] = {
    "Orange Cat", "Dog", "Panda", "Rabbit", "Penguin", "Frog",
    "Rat", "Ox", "Tiger", "Dragon", "Snake", "Horse",
    "Goat", "Monkey", "Rooster", "Pig"
};

// 分组归属（下标为 PetStyle 枚举值）。枚举值保持稳定保证 pet/style 持久化兼容，
// 分组只是 UI 摆放：柴犬/兔子造型归入生肖组，显示名改为"狗/兔"（i18n 键值）
constexpr int kClassicStyles[] = {
    0,  // 猫
    2,  // 熊猫
    4,  // 企鹅
    5   // 青蛙
};
// 十二生肖按传统顺序：鼠牛虎兔龙蛇马羊猴鸡狗猪（兔=Bunny、狗=Shiba 复用造型）
constexpr int kZodiacStyles[] = {
    6, 7, 8, 3, 9, 10, 11, 12, 13, 14, 1, 15
};
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
    QVBoxLayout *tabLayout = new QVBoxLayout(this);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    // 16 种样式按钮较高，选项卡套 QScrollArea（与历史/快捷键/样式页同构）：
    // 高度动画中间态/小屏下内容超出窗口时裁剪滚动，避免布局压缩导致控件重叠
    // 创建滚动内容容器
    // 注意：用 QPalette 设置背景色，不要用 setStyleSheet。
    // QScrollArea 的 viewport 不会自动继承 QTabWidget::pane 的背景色，
    // 需要显式设置。若用 setStyleSheet 会禁用子控件（如 QCheckBox）的
    // 原生主题绘制，导致复选框指示器空白、对勾不显示。
    QWidget *scrollContent = new QWidget();
    m_petScrollContent = scrollContent;
    scrollContent->setAutoFillBackground(true);
    QPalette scrollPal = scrollContent->palette();
    scrollPal.setColor(QPalette::Window, StyleManager::getTabWidgetBgColor());
    scrollContent->setPalette(scrollPal);
    QVBoxLayout *layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // ===== 基本组：宠物开关 =====
    m_basicGroup = new QGroupBox(tm->get("pet.basicGroup", "Basic"), scrollContent);
    m_basicGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *basicLayout = new QVBoxLayout(m_basicGroup);

    m_enablePetCheck = new QCheckBox(tm->get("pet.enable", "Enable Desktop Pet"), m_basicGroup);
    m_enablePetCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_enablePetCheck->setChecked(DesktopPetManager::instance()->isEnabled());
    connect(m_enablePetCheck, &QCheckBox::toggled, this, [](bool checked) {
        DesktopPetManager::instance()->setEnabled(checked);
    });
    basicLayout->addWidget(m_enablePetCheck);

    // ===== 宠物样式组：平铺图标按钮，点击即切换（图标为各样式预览图） =====
    m_styleGroupBox = new QGroupBox(tm->get("pet.styleLabel", "Pet Style"), scrollContent);
    m_styleGroupBox->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *styleLayout = new QVBoxLayout(m_styleGroupBox);

    m_styleGroup = new QButtonGroup(this);
    m_styleGroup->setExclusive(true);
    // 预览图按显示尺寸 2 倍渲染保证清晰，显示尺寸随 DPI 缩放（96 逻辑基准）
    const int iconSize = PetUi::scaled(48);
    for (int i = 0; i < PetWindow::petStyleCount(); ++i) {
        const auto style = static_cast<PetWindow::PetStyle>(i);
        auto *btn = new QToolButton(m_styleGroupBox);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(QIcon(PetWindow::renderStylePreview(style, iconSize * 2)));
        btn->setIconSize(QSize(iconSize, iconSize));
        btn->setText(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        btn->setToolTip(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        btn->setStyleSheet(StyleManager::getSettingsToolButtonStyle());
        m_styleButtons.append(btn);
        m_styleGroup->addButton(btn, i);
    }
    m_styleGroup->button(int(DesktopPetManager::instance()->petStyle()))->setChecked(true);
    connect(m_styleGroup, &QButtonGroup::idClicked, this, [this](int id) {
        // 自定义缩略图按钮 id 从 kCustomIdBase 起偏移，与内置样式共用互斥组
        if (id >= kCustomIdBase) {
            const int index = id - kCustomIdBase;
            if (index >= 0 && index < m_customButtons.size()) {
                DesktopPetManager::instance()->selectCustom(
                    m_customButtons[index]->property("customPetId").toString());
            }
            return;
        }
        DesktopPetManager::instance()->setPetStyle(static_cast<PetWindow::PetStyle>(id));
    });

    // 第一行：经典样式
    m_classicLabel = new QLabel(tm->get("pet.classicGroup", "Classic"), m_styleGroupBox);
    QHBoxLayout *styleRow = new QHBoxLayout();
    // 行布局只是分组容器，不应自带额外内边距：清零后与 FlowLayout（显式 0 边距）
    // 及同级控件的左基线一致
    styleRow->setContentsMargins(0, 0, 0, 0);
    styleRow->setSpacing(PetUi::scaled(6));
    for (int idx : kClassicStyles) {
        if (idx < m_styleButtons.size()) styleRow->addWidget(m_styleButtons[idx]);
    }
    styleRow->addStretch();

    // 第二行起：十二生肖（12 个按传统顺序；FlowLayout 自动换行）
    // 注意：FlowLayout 不能以 m_styleGroupBox 为父构造——QLayout 构造函数会设置
    // QObject 父子关系，导致 addLayout 拒绝挂载（布局成孤儿，按钮不显示），
    // 必须无父创建后交由 addLayout 收养
    m_zodiacLabel = new QLabel(tm->get("pet.zodiacGroup", "Zodiac"), m_styleGroupBox);
    auto *zodiacRow = new FlowLayout(nullptr, PetUi::scaled(6), PetUi::scaled(6));
    for (int idx : kZodiacStyles) {
        if (idx < m_styleButtons.size()) zodiacRow->addWidget(m_styleButtons[idx]);
    }

    styleLayout->addWidget(m_classicLabel);
    styleLayout->addLayout(styleRow);
    styleLayout->addWidget(m_zodiacLabel);
    styleLayout->addLayout(zodiacRow);

    // ===== 自定义宠物：缩略图平铺 + 添加/删除 =====
    // 自定义项与内置样式共用 m_styleGroup 保证选择互斥，素材入库到应用数据目录
    m_customGroup = new QGroupBox(tm->get("pet.customGroup", "Custom Pets"), scrollContent);
    m_customGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *customLayout = new QVBoxLayout(m_customGroup);

    // FlowLayout 同理必须无父创建，再由 addLayout 收养
    m_customFlow = new FlowLayout(nullptr, PetUi::scaled(6), PetUi::scaled(6));

    QHBoxLayout *customActions = new QHBoxLayout();
    customActions->setContentsMargins(0, 0, 0, 0);
    customActions->setSpacing(PetUi::scaled(6));
    m_btnAddCustom = new QPushButton(tm->get("pet.addCustom", "Add Image/GIF"), m_customGroup);
    m_btnAddCustom->setStyleSheet(StyleManager::getSettingsButtonStyle());
    m_btnAddCustom->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_btnAddCustom, &QPushButton::clicked, this, &SettingsPetTab::addCustomPetFromFile);

    m_btnRemoveCustom = new QPushButton(tm->get("pet.removeCustom", "Delete Selected"), m_customGroup);
    m_btnRemoveCustom->setStyleSheet(StyleManager::getSettingsButtonStyle());
    m_btnRemoveCustom->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_btnRemoveCustom->setEnabled(false);
    connect(m_btnRemoveCustom, &QPushButton::clicked, this, &SettingsPetTab::removeSelectedCustomPet);

    customActions->addWidget(m_btnAddCustom);
    customActions->addWidget(m_btnRemoveCustom);
    customActions->addStretch();

    m_customHint = new QLabel(
        tm->get("pet.customHint",
                "Transparent PNG or GIF works best; non-square images are scaled to fit."),
        m_customGroup);
    m_customHint->setWordWrap(true);

    customLayout->addLayout(customActions);
    customLayout->addLayout(m_customFlow);
    customLayout->addWidget(m_customHint);

    // 选中态变更（添加/删除/切换）时同步按钮与删除按钮可用性
    connect(DesktopPetManager::instance(), &DesktopPetManager::selectionChanged,
            this, [this](const QString &) { syncSelection(); });

    // ===== 行为组：晾衣绳打开方式 + 重置位置 + 功能说明 =====
    m_behaviorGroup = new QGroupBox(tm->get("pet.behaviorGroup", "Behavior"), scrollContent);
    m_behaviorGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *behaviorLayout = new QVBoxLayout(m_behaviorGroup);

    // 晾衣绳显示和消失方式：上边界 / 点击宠物 / 两者，userData 对应 ClotheslineTrigger 位标志
    m_triggerLabel = new QLabel(tm->get("pet.clotheslineTrigger", "Show/Hide Clothesline") + ":", m_behaviorGroup);
    m_triggerCombo = new QComboBox(m_behaviorGroup);
    StyleManager::useHighlightOnlyPopup(m_triggerCombo);
    m_triggerCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    m_triggerCombo->addItem(tm->get("pet.trigger.hotEdge", "Mouse to top edge of screen"),
                            int(DesktopPetManager::TriggerHotEdge));
    m_triggerCombo->addItem(tm->get("pet.trigger.click", "Click the pet"),
                            int(DesktopPetManager::TriggerPetClick));
    m_triggerCombo->addItem(tm->get("pet.trigger.both", "Both"),
                            int(DesktopPetManager::TriggerBoth));
    const int curTrigger = DesktopPetManager::instance()->clotheslineTrigger();
    m_triggerCombo->setCurrentIndex(qMax(0, m_triggerCombo->findData(curTrigger)));
    connect(m_triggerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        DesktopPetManager::instance()->setClotheslineTrigger(m_triggerCombo->itemData(index).toInt());
    });
    QHBoxLayout *triggerRow = new QHBoxLayout();
    triggerRow->setContentsMargins(0, 0, 0, 0);
    triggerRow->addWidget(m_triggerLabel);
    triggerRow->addWidget(m_triggerCombo);
    triggerRow->addStretch();

    // 待办提醒：有待办逾期/今天到期时宠物头顶显示红点角标
    m_todoReminderCheck = new QCheckBox(tm->get("pet.todoReminder", "Todo Reminder"), m_behaviorGroup);
    m_todoReminderCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_todoReminderCheck->setChecked(DesktopPetManager::instance()->todoReminderEnabled());
    connect(m_todoReminderCheck, &QCheckBox::toggled, this, [](bool checked) {
        DesktopPetManager::instance()->setTodoReminderEnabled(checked);
    });

    // 番茄钟：开关 + 工作/休息时长（分钟），时长下一轮生效
    m_pomodoroCheck = new QCheckBox(tm->get("pet.pomodoro", "Pomodoro Timer"), m_behaviorGroup);
    m_pomodoroCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_pomodoroCheck->setChecked(DesktopPetManager::instance()->pomodoroEnabled());
    m_pomodoroWorkLabel = new QLabel(tm->get("pet.pomodoro.work", "Work"), m_behaviorGroup);
    m_pomodoroWorkSpin = new QSpinBox(m_behaviorGroup);
    m_pomodoroWorkSpin->setRange(5, 180);
    m_pomodoroWorkSpin->setValue(DesktopPetManager::instance()->pomodoroWorkMinutes());
    m_pomodoroBreakLabel = new QLabel(tm->get("pet.pomodoro.break", "Break"), m_behaviorGroup);
    m_pomodoroBreakSpin = new QSpinBox(m_behaviorGroup);
    m_pomodoroBreakSpin->setRange(1, 120);
    m_pomodoroBreakSpin->setValue(DesktopPetManager::instance()->pomodoroBreakMinutes());
    const QString minuteSuffix = QStringLiteral(" ") + tm->get("pet.pomodoro.minute", "min");
    m_pomodoroWorkSpin->setSuffix(minuteSuffix);
    m_pomodoroBreakSpin->setSuffix(minuteSuffix);
    // SpinBox 样式与样式选项卡一致：复用输入框（ComboBox）主题样式
    m_pomodoroWorkSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    m_pomodoroBreakSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    const bool pomodoroOn = m_pomodoroCheck->isChecked();
    m_pomodoroWorkSpin->setEnabled(pomodoroOn);
    m_pomodoroBreakSpin->setEnabled(pomodoroOn);
    connect(m_pomodoroCheck, &QCheckBox::toggled, this, [this](bool checked) {
        DesktopPetManager::instance()->setPomodoroEnabled(checked);
        m_pomodoroWorkSpin->setEnabled(checked);
        m_pomodoroBreakSpin->setEnabled(checked);
    });
    connect(m_pomodoroWorkSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        DesktopPetManager::instance()->setPomodoroWorkMinutes(v);
    });
    connect(m_pomodoroBreakSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        DesktopPetManager::instance()->setPomodoroBreakMinutes(v);
    });
    // 待机打盹：开关 + 无互动分钟数（复用分钟后缀），关闭时立即唤醒
    m_idleSleepCheck = new QCheckBox(tm->get("pet.idleSleep", "Idle Doze"), m_behaviorGroup);
    m_idleSleepCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_idleSleepCheck->setChecked(DesktopPetManager::instance()->idleSleepEnabled());
    m_idleSleepAfterLabel = new QLabel(tm->get("pet.idleSleep.after", "After"), m_behaviorGroup);
    m_idleSleepSpin = new QSpinBox(m_behaviorGroup);
    m_idleSleepSpin->setRange(1, 120);
    m_idleSleepSpin->setValue(DesktopPetManager::instance()->idleSleepMinutes());
    m_idleSleepSpin->setSuffix(minuteSuffix);
    m_idleSleepSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    m_idleSleepSpin->setEnabled(m_idleSleepCheck->isChecked());
    connect(m_idleSleepCheck, &QCheckBox::toggled, this, [this](bool checked) {
        DesktopPetManager::instance()->setIdleSleepEnabled(checked);
        m_idleSleepSpin->setEnabled(checked);
    });
    connect(m_idleSleepSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
        DesktopPetManager::instance()->setIdleSleepMinutes(v);
    });
    // 系统状态：CPU/内存过载时宠物冒汗、低电量时显示电池角标（5s 轮询）
    m_sysMonitorCheck = new QCheckBox(tm->get("pet.sysMonitor", "System Status"), m_behaviorGroup);
    m_sysMonitorCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    m_sysMonitorCheck->setChecked(DesktopPetManager::instance()->sysMonitorEnabled());
    connect(m_sysMonitorCheck, &QCheckBox::toggled, this, [](bool checked) {
        DesktopPetManager::instance()->setSysMonitorEnabled(checked);
    });
    // 行为说明：小号灰字，缩进与复选框文字对齐（颜色在 refreshStyles 随主题刷新，
    // 全局 QSS 的 QLabel 颜色优先级高于 palette，只能 setStyleSheet 覆盖）
    m_sysMonitorHint = new QLabel(
        tm->get("pet.sysMonitorHint",
                "The pet sweats when CPU/memory usage is high, and shows a battery icon when battery is low."),
        m_behaviorGroup);
    m_sysMonitorHint->setWordWrap(true);
    m_sysMonitorHint->setContentsMargins(PetUi::scaled(24), 0, 0, 0);
    m_sysMonitorHint->setStyleSheet(
        QString("color: %1;").arg(PetTheme::mutedColor(PetTheme::isDark()).name()));

    m_btnResetPos = new QPushButton(tm->get("pet.resetPosition", "Reset Pet Position"), m_behaviorGroup);
    m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
    m_btnResetPos->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_btnResetPos, &QPushButton::clicked, this, []() {
        DesktopPetManager::instance()->resetPetPosition();
    });

    m_descLabel = new QLabel(tm->get("pet.description",
        "When enabled, screenshots fly into the pet's mouth after copy, pin, or taking a snapshot while recording, \nand copied or cut text flies in as well. \nClick the pet or move the mouse to the top edge of the screen to open or close the clothesline and view recent screenshots and copied text."), m_behaviorGroup);
    // 自动换行：各语言文案长度不一（英/日/韩首行远超可用宽度），关闭换行
    // 会横向截断。此前换行导致窗口底部空白，根因是 applyTabHeight 在窄
    // 宽度下估算 hfw（已修复：测量前按选项卡栈宽归位），换行后窗口高度
    // 会随行数正确适配
    m_descLabel->setWordWrap(true);

    // 四个开关行共用同一网格：直接加进 QVBoxLayout 的复选框在可用宽度
    // 不足时会按 Qt 溢出规则被左移 2px，与行内控件错开；纳入同一网格后
    // 所有复选框共用第 0 列左基线，横竖严格对齐
    auto *statusGrid = new QGridLayout();
    statusGrid->setContentsMargins(0, 0, 0, 0);
    statusGrid->setHorizontalSpacing(PetUi::scaled(10));
    statusGrid->addWidget(m_todoReminderCheck, 0, 0, 1, 6);
    statusGrid->addWidget(m_pomodoroCheck, 1, 0);
    statusGrid->addWidget(m_pomodoroWorkLabel, 1, 1);
    statusGrid->addWidget(m_pomodoroWorkSpin, 1, 2);
    statusGrid->addWidget(m_pomodoroBreakLabel, 1, 3);
    statusGrid->addWidget(m_pomodoroBreakSpin, 1, 4);
    statusGrid->addWidget(m_idleSleepCheck, 2, 0);
    statusGrid->addWidget(m_idleSleepAfterLabel, 2, 1);
    statusGrid->addWidget(m_idleSleepSpin, 2, 2);
    statusGrid->addWidget(m_sysMonitorCheck, 3, 0, 1, 6);
    statusGrid->setColumnStretch(5, 1);

    behaviorLayout->addLayout(triggerRow);
    behaviorLayout->addLayout(statusGrid);
    behaviorLayout->addWidget(m_sysMonitorHint);
    behaviorLayout->addWidget(m_btnResetPos);
    behaviorLayout->addWidget(m_descLabel);

    // 四个分组纵向排列：基本 / 宠物样式 / 自定义宠物 / 行为
    layout->addWidget(m_basicGroup);
    layout->addWidget(m_styleGroupBox);
    layout->addWidget(m_customGroup);
    layout->addWidget(m_behaviorGroup);
    layout->addStretch();

    // 创建滚动区域
    // 注意：不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供。
    // 若用 setStyleSheet 会禁用子控件（如 QCheckBox）的原生主题绘制，
    // 导致复选框指示器空白、对勾不显示。
    petScrollArea = new QScrollArea();
    petScrollArea->setWidget(scrollContent);
    petScrollArea->setWidgetResizable(true);
    petScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    petScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    petScrollArea->setFrameShape(QFrame::NoFrame);
    // 用 QPalette 设置 viewport 背景色，避免默认背景色与选项卡背景不一致
    QPalette viewportPal = petScrollArea->palette();
    viewportPal.setColor(QPalette::Window, StyleManager::getTabWidgetBgColor());
    petScrollArea->setPalette(viewportPal);
    petScrollArea->setAutoFillBackground(true);

    tabLayout->addWidget(petScrollArea);

    // 构建已导入的自定义宠物缩略图（空列表时只有添加按钮与提示）
    refreshCustomList();
}

// ============================================================
// 自定义宠物
// ============================================================

void SettingsPetTab::refreshCustomList() {
    // 先移除旧按钮，避免重复导入后出现遗留项
    for (QToolButton *btn : m_customButtons) {
        m_styleGroup->removeButton(btn);
        m_customFlow->removeWidget(btn);
        btn->deleteLater();
    }
    m_customButtons.clear();

    const int iconSize = PetUi::scaled(48);
    const QVector<CustomPet> pets = DesktopPetManager::instance()->customPets();
    for (const CustomPet &pet : pets) {
        auto *btn = new QToolButton(m_customGroup);
        btn->setCheckable(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        const QPixmap preview = PetWindow::renderCustomPreview(
            CustomPetStore::instance()->filePath(pet), iconSize * 2);
        if (!preview.isNull()) {
            btn->setIcon(QIcon(preview));
        }
        btn->setIconSize(QSize(iconSize, iconSize));
        btn->setText(pet.name);
        btn->setToolTip(pet.isGif ? pet.name + QStringLiteral(" (GIF)") : pet.name);
        btn->setStyleSheet(StyleManager::getSettingsToolButtonStyle());
        btn->setProperty("customPetId", pet.id);
        m_customButtons.append(btn);
        m_customFlow->addWidget(btn);
        m_styleGroup->addButton(btn, kCustomIdBase + m_customButtons.size() - 1);
    }

    syncSelection();
    m_customGroup->updateGeometry();
}

void SettingsPetTab::syncSelection() {
    const QString sel = DesktopPetManager::instance()->selection();
    const bool isCustom = CustomPetStore::isCustomSelection(sel);
    if (isCustom) {
        const QString id = CustomPetStore::customIdFromSelection(sel);
        for (QToolButton *btn : m_customButtons) {
            if (btn->property("customPetId").toString() == id) {
                btn->setChecked(true);
                break;
            }
        }
    } else if (QAbstractButton *btn = m_styleGroup->button(
                   int(DesktopPetManager::instance()->petStyle()))) {
        btn->setChecked(true);
    }
    // 删除按钮仅对自定义选择可用
    if (m_btnRemoveCustom) {
        m_btnRemoveCustom->setEnabled(isCustom);
    }
}

void SettingsPetTab::addCustomPetFromFile() {
    TranslationManager *tm = TranslationManager::instance();
    const QString path = QFileDialog::getOpenFileName(
        this, tm->get("pet.addCustomTitle", "Choose Pet Image"), QString(),
        tm->get("pet.customFilter", "Images (*.png *.jpg *.jpeg *.webp *.gif)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const CustomPet pet = DesktopPetManager::instance()->addCustomPet(path, &error);
    if (!pet.isValid()) {
        MessageBox::warning(
            this, tm->get("pet.addFailedTitle", "Add Failed"),
            tm->get("pet.addFailed", "Could not add this image: %1").arg(error));
        return;
    }
    refreshCustomList();
}

void SettingsPetTab::removeSelectedCustomPet() {
    const QString sel = DesktopPetManager::instance()->selection();
    if (!CustomPetStore::isCustomSelection(sel)) {
        return;
    }
    TranslationManager *tm = TranslationManager::instance();
    if (!MessageBox::question(
            this, tm->get("pet.removeCustomTitle", "Delete Custom Pet"),
            tm->get("pet.removeCustomConfirm",
                    "Delete this custom pet? The imported image will also be removed."))) {
        return;
    }
    DesktopPetManager::instance()->removeCustomPet(CustomPetStore::customIdFromSelection(sel));
    refreshCustomList();
}

// ============================================================
// 语言切换
// ============================================================

void SettingsPetTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();
    if (m_basicGroup) m_basicGroup->setTitle(tm->get("pet.basicGroup", "Basic"));
    if (m_enablePetCheck) m_enablePetCheck->setText(tm->get("pet.enable", "Enable Desktop Pet"));
    if (m_styleGroupBox) m_styleGroupBox->setTitle(tm->get("pet.styleLabel", "Pet Style"));
    if (m_behaviorGroup) m_behaviorGroup->setTitle(tm->get("pet.behaviorGroup", "Behavior"));
    if (m_classicLabel) m_classicLabel->setText(tm->get("pet.classicGroup", "Classic"));
    if (m_zodiacLabel) m_zodiacLabel->setText(tm->get("pet.zodiacGroup", "Zodiac"));
    for (int i = 0; i < m_styleButtons.size() && i < PetWindow::petStyleCount(); ++i) {
        m_styleButtons[i]->setText(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
        m_styleButtons[i]->setToolTip(tm->get(kStyleKeys[i], kStyleFallbacks[i]));
    }
    if (m_btnResetPos) m_btnResetPos->setText(tm->get("pet.resetPosition", "Reset Pet Position"));
    if (m_todoReminderCheck) m_todoReminderCheck->setText(tm->get("pet.todoReminder", "Todo Reminder"));
    if (m_pomodoroCheck) m_pomodoroCheck->setText(tm->get("pet.pomodoro", "Pomodoro Timer"));
    if (m_pomodoroWorkLabel) m_pomodoroWorkLabel->setText(tm->get("pet.pomodoro.work", "Work"));
    if (m_pomodoroBreakLabel) m_pomodoroBreakLabel->setText(tm->get("pet.pomodoro.break", "Break"));
    const QString minuteSuffix = QStringLiteral(" ") + tm->get("pet.pomodoro.minute", "min");
    if (m_pomodoroWorkSpin) m_pomodoroWorkSpin->setSuffix(minuteSuffix);
    if (m_pomodoroBreakSpin) m_pomodoroBreakSpin->setSuffix(minuteSuffix);
    if (m_idleSleepCheck) m_idleSleepCheck->setText(tm->get("pet.idleSleep", "Idle Doze"));
    if (m_idleSleepAfterLabel) m_idleSleepAfterLabel->setText(tm->get("pet.idleSleep.after", "After"));
    if (m_idleSleepSpin) m_idleSleepSpin->setSuffix(minuteSuffix);
    if (m_sysMonitorCheck) m_sysMonitorCheck->setText(tm->get("pet.sysMonitor", "System Status"));
    if (m_sysMonitorHint) m_sysMonitorHint->setText(
        tm->get("pet.sysMonitorHint",
                "The pet sweats when CPU/memory usage is high, and shows a battery icon when battery is low."));
    // 语言切换时重建下拉项文案并保持当前选择（userData 为触发方式位标志）
    if (m_triggerLabel) m_triggerLabel->setText(tm->get("pet.clotheslineTrigger", "Show/Hide Clothesline") + ":");
    if (m_triggerCombo) {
        const int cur = m_triggerCombo->currentData().toInt();
        QSignalBlocker blocker(m_triggerCombo);
        m_triggerCombo->clear();
        m_triggerCombo->addItem(tm->get("pet.trigger.hotEdge", "Mouse to top edge of screen"),
                                int(DesktopPetManager::TriggerHotEdge));
        m_triggerCombo->addItem(tm->get("pet.trigger.click", "Click the pet"),
                                int(DesktopPetManager::TriggerPetClick));
        m_triggerCombo->addItem(tm->get("pet.trigger.both", "Both"),
                                int(DesktopPetManager::TriggerBoth));
        m_triggerCombo->setCurrentIndex(qMax(0, m_triggerCombo->findData(cur)));
    }
    if (m_customGroup) m_customGroup->setTitle(tm->get("pet.customGroup", "Custom Pets"));
    if (m_btnAddCustom) m_btnAddCustom->setText(tm->get("pet.addCustom", "Add Image/GIF"));
    if (m_btnRemoveCustom) m_btnRemoveCustom->setText(tm->get("pet.removeCustom", "Delete Selected"));
    if (m_customHint) m_customHint->setText(
        tm->get("pet.customHint",
                "Transparent PNG or GIF works best; non-square images are scaled to fit."));
    if (m_descLabel) m_descLabel->setText(tm->get("pet.description",
        "When enabled, screenshots fly into the pet's mouth after copy, pin, or taking a snapshot while recording, \nand copied or cut text flies in as well. \nClick the pet or move the mouse to the top edge of the screen to open or close the clothesline and view recent screenshots and copied text."));
}

// ============================================================
// 样式与布局
// ============================================================

void SettingsPetTab::refreshStyles() {
    if (m_basicGroup) m_basicGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_styleGroupBox) m_styleGroupBox->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_behaviorGroup) m_behaviorGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_enablePetCheck) m_enablePetCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    if (m_todoReminderCheck) m_todoReminderCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    if (m_pomodoroCheck) m_pomodoroCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    if (m_idleSleepCheck) m_idleSleepCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    if (m_sysMonitorCheck) m_sysMonitorCheck->setStyleSheet(StyleManager::getSettingsCheckBoxStyle());
    // 说明标签：次要文字色（全局 QSS 的 QLabel 颜色需用 setStyleSheet 覆盖）
    if (m_sysMonitorHint) m_sysMonitorHint->setStyleSheet(
        QString("color: %1;").arg(PetTheme::mutedColor(PetTheme::isDark()).name()));
    // SpinBox 样式与样式选项卡一致：复用输入框（ComboBox）主题样式
    if (m_pomodoroWorkSpin) m_pomodoroWorkSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    if (m_pomodoroBreakSpin) m_pomodoroBreakSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    if (m_idleSleepSpin) m_idleSleepSpin->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    for (QToolButton *btn : m_styleButtons) {
        btn->setStyleSheet(StyleManager::getSettingsToolButtonStyle());
    }
    if (m_btnResetPos) m_btnResetPos->setStyleSheet(StyleManager::getSettingsButtonStyle());
    if (m_triggerCombo) m_triggerCombo->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    if (m_customGroup) m_customGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    if (m_btnAddCustom) m_btnAddCustom->setStyleSheet(StyleManager::getSettingsButtonStyle());
    if (m_btnRemoveCustom) m_btnRemoveCustom->setStyleSheet(StyleManager::getSettingsButtonStyle());
    for (QToolButton *btn : m_customButtons) {
        btn->setStyleSheet(StyleManager::getSettingsToolButtonStyle());
    }
}

void SettingsPetTab::applyTabBackground(const QColor &color) {
    // 根容器背景（#petTabBg 选择器）+ 滚动区两处 QPalette 背景同步刷新
    setStyleSheet(QString("#petTabBg { background-color: %1; }").arg(color.name()));
    if (m_petScrollContent) {
        QPalette pal = m_petScrollContent->palette();
        pal.setColor(QPalette::Window, color);
        m_petScrollContent->setPalette(pal);
    }
    if (petScrollArea) {
        QPalette vpPal = petScrollArea->palette();
        vpPal.setColor(QPalette::Window, color);
        petScrollArea->setPalette(vpPal);
    }
}
