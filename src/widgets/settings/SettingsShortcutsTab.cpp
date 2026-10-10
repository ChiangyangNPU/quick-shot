#include "SettingsShortcutsTab.h"
#include "../../core/ConfigManager.h"
#include "../../core/StyleManager.h"
#include "../../core/TranslationManager.h"
#include "../../shortcut/ShortcutTypes.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QWheelEvent>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsShortcutsTab::SettingsShortcutsTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    // 滚轮滚动事件过滤：选项卡自身 + 滚动区（行内编辑框在 addShortcutRow 中单独安装）
    installEventFilter(this);
    m_shortcutsScrollArea->installEventFilter(this);
}

void SettingsShortcutsTab::setupUi() {
    QWidget *shortcutsTab = new QWidget();
    shortcutsTab->setObjectName("shortcutsTabBg");
    shortcutsTab->setStyleSheet(QString("#shortcutsTabBg { background-color: %1; }")
                                .arg(StyleManager::getTabWidgetBgColor().name()));
    // 布局必须创建在 shortcutsTab 上，作为该选项卡根容器的布局；
    // 此前误写为 m_shortcutGroupGlobal（此时为 nullptr，且要到下方才 new），
    // 导致 shortcutsTab 无布局、滚动区无法挂载，整个选项卡空白不可见。
    QVBoxLayout *shortcutsTabLayout = new QVBoxLayout(shortcutsTab);
    shortcutsTabLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsTabLayout->setSpacing(0);

    // 滚动容器：承载三个分类分组（参考样式/历史记录选项卡的 QScrollArea 模式）
    QWidget *scrollContainer = new QWidget();
    scrollContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContainer);
    scrollLayout->setContentsMargins(10, 10, 10, 10);
    scrollLayout->setSpacing(10);

    // ===== 分类一：全局热键（可配置）=====
    m_shortcutGroupGlobal = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.global", "Global Hotkeys (Configurable)"), scrollContainer);
    m_shortcutGroupGlobal->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *globalLayout = new QGridLayout(m_shortcutGroupGlobal);
    globalLayout->setContentsMargins(10, 10, 10, 10);
    globalLayout->setSpacing(10);

    // 可配置全局热键（行 0-8）：configKey/默认键/标签全部来自 kShortcutConfigs 数据表，
    // 新增快捷键只需在表中加一行，此处自动生成（消除与数据表手抄同步的维护负担）
    for (int i = 0; i < kShortcutConfigCount; ++i) {
        const ShortcutConfigItem &cfg = kShortcutConfigs[i];
        addShortcutRow(globalLayout, i,
                       shortcutTypeToString(cfg.type),
                       cfg.configKey, cfg.defaultValue,
                       cfg.settingsLabelKey, cfg.settingsLabelFallback);
    }

    // ===== 分类二：标注工具（1-8 切换工具，固定）=====
    m_shortcutGroupTools = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.tools", "Annotation Tools (Fixed)"), scrollContainer);
    m_shortcutGroupTools->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *toolsLayout = new QGridLayout(m_shortcutGroupTools);
    toolsLayout->setContentsMargins(10, 10, 10, 10);
    toolsLayout->setSpacing(8);
    addFixedShortcutRow(toolsLayout, 0, "shortcut.fixed.switchMode",      "Screenshot/Recording Switch", "`");
    addFixedShortcutRow(toolsLayout, 1, "shortcut.fixed.rect",            "Rectangle",              "1");
    addFixedShortcutRow(toolsLayout, 2, "shortcut.fixed.ellipse",         "Ellipse",                "2");
    addFixedShortcutRow(toolsLayout, 3, "shortcut.fixed.arrow",           "Arrow",                  "3");
    addFixedShortcutRow(toolsLayout, 4, "shortcut.fixed.pen",             "Pen",                    "4");
    addFixedShortcutRow(toolsLayout, 5, "shortcut.fixed.line",            "Line",                   "5");
    addFixedShortcutRow(toolsLayout, 6, "shortcut.fixed.text",            "Text",                   "6");
    addFixedShortcutRow(toolsLayout, 7, "shortcut.fixed.mosaic",          "Mosaic",                 "7");
    addFixedShortcutRow(toolsLayout, 8, "shortcut.fixed.eraser",          "Eraser",                 "8");

    // ===== 分类三：标注操作（复制/撤销/保存/约束等，固定）=====
    m_shortcutGroupAnnotation = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.annotation", "Annotation Operations (Fixed)"), scrollContainer);
    m_shortcutGroupAnnotation->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *annotationLayout = new QGridLayout(m_shortcutGroupAnnotation);
    annotationLayout->setContentsMargins(10, 10, 10, 10);
    annotationLayout->setSpacing(8);
    addFixedShortcutRow(annotationLayout, 0,  "shortcut.fixed.copyExit",        "Copy & Exit",            "Enter / Ctrl+C");
    addFixedShortcutRow(annotationLayout, 1,  "shortcut.fixed.cancel",          "Cancel",                 "Esc");
    addFixedShortcutRow(annotationLayout, 2,  "shortcut.fixed.save",            "Save to File",           "Ctrl+S");
    addFixedShortcutRow(annotationLayout, 3,  "shortcut.fixed.undo",            "Undo",                   "Ctrl+Z");
    addFixedShortcutRow(annotationLayout, 4,  "shortcut.fixed.redo",            "Redo",                   "Ctrl+Y / Ctrl+Shift+Z");
    addFixedShortcutRow(annotationLayout, 5,  "shortcut.fixed.refresh",         "Refresh",                "F5");
    addFixedShortcutRow(annotationLayout, 6,  "shortcut.fixed.penWidthDec",     "Pen Width -",            "[");
    addFixedShortcutRow(annotationLayout, 7,  "shortcut.fixed.penWidthInc",     "Pen Width +",            "]");
    addFixedShortcutRow(annotationLayout, 8,  "shortcut.fixed.cycleColor",      "Cycle Color",            "Tab");
    addFixedShortcutRow(annotationLayout, 9,  "shortcut.fixed.clear",           "Clear All",              "Delete / Backspace");
    addFixedShortcutRow(annotationLayout, 10, "shortcut.fixed.shiftConstraint",  "Shift Constraint (hold)","Shift");
    addFixedShortcutRow(annotationLayout, 11, "shortcut.fixed.altCenter",       "Center Draw (hold)",     "Alt");

    // ===== 分类四：PinWindow 快捷键（固定，不可配置）=====
    m_shortcutGroupPinWindow = new QGroupBox(
        TranslationManager::instance()->get("shortcut.section.pinwindow", "Pin Shortcuts (Fixed)"), scrollContainer);
    m_shortcutGroupPinWindow->setStyleSheet(StyleManager::getGroupBoxStyle());
    QGridLayout *pinWindowLayout = new QGridLayout(m_shortcutGroupPinWindow);
    pinWindowLayout->setContentsMargins(10, 10, 10, 10);
    pinWindowLayout->setSpacing(8);
    addFixedShortcutRow(pinWindowLayout, 0, "shortcut.fixed.pinCopy",     "Copy Image",          "Ctrl+C");
    addFixedShortcutRow(pinWindowLayout, 1, "shortcut.fixed.pinSave",     "Save Image",          "Ctrl+S");
    addFixedShortcutRow(pinWindowLayout, 2, "shortcut.fixed.pinUndo",     "Undo",                "Ctrl+Z");
    addFixedShortcutRow(pinWindowLayout, 3, "shortcut.fixed.pinRedo",     "Redo",                "Ctrl+Y / Ctrl+Shift+Z");
    addFixedShortcutRow(pinWindowLayout, 4, "shortcut.fixed.pinTool",     "Switch Tool",         "1-8");
    addFixedShortcutRow(pinWindowLayout, 5, "shortcut.fixed.cycleColor",  "Cycle Color",         "Tab");
    addFixedShortcutRow(pinWindowLayout, 6, "shortcut.fixed.penWidthDec", "Pen Width -",         "[");
    addFixedShortcutRow(pinWindowLayout, 7, "shortcut.fixed.penWidthInc", "Pen Width +",         "]");
    addFixedShortcutRow(pinWindowLayout, 8, "shortcut.fixed.clear",       "Clear All",           "Delete / Backspace");
    addFixedShortcutRow(pinWindowLayout, 9, "shortcut.fixed.pinMove",     "Move Window",         "Arrow Keys");
    addFixedShortcutRow(pinWindowLayout, 10, "shortcut.fixed.pinMoveFast", "Move Fast",           "Ctrl+Arrow Keys");
    addFixedShortcutRow(pinWindowLayout, 11, "shortcut.fixed.pinEsc",      "Exit Annotate/Close", "Esc");

    // 组装滚动区域：四个分组纵向排列，末尾拉伸吸收多余空间
    scrollLayout->addWidget(m_shortcutGroupGlobal);
    scrollLayout->addWidget(m_shortcutGroupTools);
    scrollLayout->addWidget(m_shortcutGroupAnnotation);
    scrollLayout->addWidget(m_shortcutGroupPinWindow);
    scrollLayout->addStretch();

    m_shortcutsScrollArea = new QScrollArea();
    m_shortcutsScrollArea->setWidget(scrollContainer);
    m_shortcutsScrollArea->setWidgetResizable(true);
    m_shortcutsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_shortcutsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_shortcutsScrollArea->setFrameShape(QFrame::NoFrame);
    // 不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供
    // （与历史记录选项卡保持一致，避免 setStyleSheet 影响子控件原生渲染）

    shortcutsTabLayout->addWidget(m_shortcutsScrollArea);

}

// ============================================================
// 行生成（数据驱动）
// ============================================================

void SettingsShortcutsTab::addShortcutRow(QGridLayout *layout, int row, const QString &action,
                                    const QString &configKey, const QString &defaultKey,
                                    const QString &trKey, const QString &fallback) {
    TranslationManager *tm = TranslationManager::instance();
    QWidget *parent = layout->parentWidget();

    ShortcutRow r;
    r.action = action;
    r.configKey = configKey;
    r.defaultKey = defaultKey;
    r.trKey = trKey;
    r.fallback = fallback;

    r.label = new QLabel(tm->get(trKey, fallback) + ":", parent);
    r.edit = new QKeySequenceEdit(parent);
    r.edit->setMaximumSequenceLength(1);
    r.edit->setStyleSheet(StyleManager::getKeySequenceEditStyle());
    r.edit->installEventFilter(this);

    QString buttonStyle = StyleManager::getSettingsButtonStyle();
    r.btnCancel = new QPushButton(tm->get("cancel", "Cancel"), parent);
    r.btnOk = new QPushButton(tm->get("ok", "OK"), parent);
    r.btnReset = new QPushButton(tm->get("reset", "Reset"), parent);
    r.btnCancel->setStyleSheet(buttonStyle);
    r.btnOk->setStyleSheet(buttonStyle);
    r.btnReset->setStyleSheet(buttonStyle);

    // 与现有行保持一致的固定高度
    int inputHeight = r.edit->sizeHint().height();
    r.edit->setFixedHeight(inputHeight);
    r.label->setFixedHeight(inputHeight);
    r.btnCancel->setFixedHeight(inputHeight);
    r.btnOk->setFixedHeight(inputHeight);
    r.btnReset->setFixedHeight(inputHeight);

    // 加载当前配置值（统一通过 ShortcutTypes::getShortcutSequence 读取，
    // 确保 history 等旧键名向后兼容；未知类型回退到直接读取 configKey）
    QString currentKey = defaultKey;
    auto type = shortcutTypeFromString(action);
    if (type.has_value()) {
        const ShortcutConfigItem *cfg = getShortcutConfig(*type);
        if (cfg) {
            currentKey = getShortcutSequence(*cfg).toString();
        }
    } else {
        currentKey = ConfigManager::instance()->getSettings()->value(configKey, defaultKey).toString();
    }
    r.edit->setKeySequence(QKeySequence(currentKey));

    // 布局：标签 | 输入框 | 取消 | 确定 | 恢复
    layout->addWidget(r.label, row, 0);
    layout->addWidget(r.edit, row, 1);
    layout->addWidget(r.btnCancel, row, 2);
    layout->addWidget(r.btnOk, row, 3);
    layout->addWidget(r.btnReset, row, 4);

    // 确定：保存配置并通知外部更新热键
    connect(r.btnOk, &QPushButton::clicked, this, [this, r]() {
        QKeySequence seq = r.edit->keySequence();
        ConfigManager::instance()->getSettings()->setValue(r.configKey, seq.toString());
        ConfigManager::instance()->getSettings()->sync();
        emit shortcutChanged(r.action, seq);
    });
    // 取消：从配置读回当前值
    connect(r.btnCancel, &QPushButton::clicked, this, [this, r]() {
        QString key = ConfigManager::instance()->getSettings()->value(r.configKey, r.defaultKey).toString();
        r.edit->setKeySequence(QKeySequence(key));
    });
    // 恢复：重置为默认值并通知
    connect(r.btnReset, &QPushButton::clicked, this, [this, r]() {
        r.edit->setKeySequence(QKeySequence(r.defaultKey));
        ConfigManager::instance()->getSettings()->setValue(r.configKey, r.defaultKey);
        ConfigManager::instance()->getSettings()->sync();
        emit shortcutChanged(r.action, QKeySequence(r.defaultKey));
    });

    m_extraShortcutRows.append(r);
}
void SettingsShortcutsTab::addFixedShortcutRow(QGridLayout *layout, int row,
        const QString &trKey, const QString &fallback, const QString &keys) {
    TranslationManager *tm = TranslationManager::instance();
    QWidget *parent = layout->parentWidget();

    FixedShortcutRow r;
    r.trKey = trKey;
    r.fallback = fallback;
    r.keys = keys;

    r.label = new QLabel(tm->get(trKey, fallback) + ":", parent);
    r.edit = new QLineEdit(keys, parent);
    r.edit->setReadOnly(true);
    r.edit->setEnabled(false);  // 置灰，明确不可编辑
    r.edit->setStyleSheet(StyleManager::getKeySequenceEditStyle());
    r.edit->setAlignment(Qt::AlignCenter);

    // 固定高度与可配置行一致（复用 QLineEdit 的 sizeHint 高度）
    int inputHeight = r.edit->sizeHint().height();
    r.label->setFixedHeight(inputHeight);
    r.edit->setFixedHeight(inputHeight);

    // 布局：功能名 | 键位（跨 4 列填满，与可配置行的 输入框+按钮 列对齐，视觉整齐）
    layout->addWidget(r.label, row, 0);
    layout->addWidget(r.edit, row, 1, 1, 4);

    m_fixedShortcutRows.append(r);
}

// ============================================================
// 语言切换 / 样式 / 背景
// ============================================================

void SettingsShortcutsTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    // 可配置全局热键行的文案（截图/录屏/历史/贴图等全部由 m_extraShortcutRows 统一管理）
    for (const ShortcutRow &r : m_extraShortcutRows) {
        if (r.label) r.label->setText(tm->get(r.trKey, r.fallback) + ":");
        if (r.btnOk) r.btnOk->setText(tm->get("ok", "OK"));
        if (r.btnCancel) r.btnCancel->setText(tm->get("cancel", "Cancel"));
        if (r.btnReset) r.btnReset->setText(tm->get("reset", "Reset"));
    }

    // 快捷键选项卡分类标题
    if (m_shortcutGroupGlobal) m_shortcutGroupGlobal->setTitle(tm->get("shortcut.section.global", "Global Hotkeys (Configurable)"));
    if (m_shortcutGroupTools) m_shortcutGroupTools->setTitle(tm->get("shortcut.section.tools", "Annotation Tools (Fixed)"));
    if (m_shortcutGroupAnnotation) m_shortcutGroupAnnotation->setTitle(tm->get("shortcut.section.annotation", "Annotation Operations (Fixed)"));
    if (m_shortcutGroupPinWindow) m_shortcutGroupPinWindow->setTitle(tm->get("shortcut.section.pinwindow", "Pin Shortcuts (Fixed)"));
    // 固定快捷键行功能名文案（键位文本 keys 硬编码无需翻译）
    for (const FixedShortcutRow &fr : m_fixedShortcutRows) {
        if (fr.label) fr.label->setText(tm->get(fr.trKey, fr.fallback) + ":");
    }
}

void SettingsShortcutsTab::refreshStyles() {
    const QString style = StyleManager::getGroupBoxStyle();
    if (m_shortcutGroupGlobal) m_shortcutGroupGlobal->setStyleSheet(style);
    if (m_shortcutGroupTools) m_shortcutGroupTools->setStyleSheet(style);
    if (m_shortcutGroupAnnotation) m_shortcutGroupAnnotation->setStyleSheet(style);
    if (m_shortcutGroupPinWindow) m_shortcutGroupPinWindow->setStyleSheet(style);
}

void SettingsShortcutsTab::applyTabBackground(const QColor &color) {
    QString style = QString("background-color: %1;").arg(color.name());
    setStyleSheet(QString("#shortcutsTabBg { %1 }").arg(style));
    if (m_shortcutsScrollArea) {
        // m_shortcutsScrollArea 的父窗口即为 shortcutsTab（通过 addWidget 挂载）
        QWidget *shortcutsTab = m_shortcutsScrollArea->parentWidget();
        if (shortcutsTab && shortcutsTab->objectName() == "shortcutsTabBg") {
            shortcutsTab->setStyleSheet(QString("#shortcutsTabBg { %1 }").arg(style));
        }
        if (m_shortcutsScrollArea->widget()) {
            m_shortcutsScrollArea->widget()->setStyleSheet(style);
        }
    }
}

// ============================================================
// 事件过滤：快捷键选项卡内的滚轮滚动
// ============================================================

bool SettingsShortcutsTab::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        QWidget *focusWidget = this->focusWidget();

        // 焦点在输入类控件时不滚动，避免与控件自身交互冲突
        if (focusWidget && (focusWidget->parent() == this || qobject_cast<QComboBox *>(focusWidget) ||
            qobject_cast<QLineEdit *>(focusWidget) || qobject_cast<QKeySequenceEdit *>(focusWidget))) {
            return QWidget::eventFilter(obj, event);
        }

        if (m_shortcutsScrollArea) {
            QScrollBar *vScrollBar = m_shortcutsScrollArea->verticalScrollBar();
            if (vScrollBar) {
                int delta = wheelEvent->angleDelta().y();
                int scrollAmount = qAbs(delta) / 8;
                scrollAmount = qMax(scrollAmount, 3);
                scrollAmount = qMin(scrollAmount, 50);

                if (delta > 0) {
                    vScrollBar->setValue(vScrollBar->value() - scrollAmount);
                } else if (delta < 0) {
                    vScrollBar->setValue(vScrollBar->value() + scrollAmount);
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}
