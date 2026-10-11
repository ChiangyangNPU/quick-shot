#include "SettingsStyleTab.h"
#include "../MessageBox.h"
#include "../../core/ConfigManager.h"
#include "../../log/Logger.h"
#include "../../core/TranslationManager.h"

#include <QColorDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>

// ============================================================
// 构造与 UI 构建
// ============================================================

SettingsStyleTab::SettingsStyleTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsStyleTab::setupUi() {
    QWidget *styleTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(styleTab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    // 边框颜色设置
    auto *tm = TranslationManager::instance();
    borderGroup = new QGroupBox(tm->get("style.borderColor", "Border Colors"), styleTab);
    borderGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *borderLayout = new QVBoxLayout(borderGroup);
    borderLayout->setContentsMargins(10, 10, 10, 10);
    borderLayout->setSpacing(8);

    // 工具栏样式设置
    toolbarGroup = new QGroupBox(TranslationManager::instance()->get("style.toolbarStyle", "Toolbar Style"), styleTab);
    toolbarGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *toolbarLayout = new QVBoxLayout(toolbarGroup);
    toolbarLayout->setContentsMargins(10, 10, 10, 10);
    toolbarLayout->setSpacing(8);

    // 选项卡样式设置
    tabGroup = new QGroupBox(TranslationManager::instance()->get("style.tabStyle", "Tab Style"), styleTab);
    tabGroup->setObjectName("tabGroup");
    tabGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *tabLayout = new QVBoxLayout(tabGroup);
    tabLayout->setContentsMargins(10, 10, 10, 10);
    tabLayout->setSpacing(8);

    // 通过数据驱动表循环创建所有颜色行（元数据表归 StyleManager 所有，此处只消费）
    const auto& table = StyleManager::colorSettingTable();
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& s = table[i];

        // 根据分类决定加入哪个 GroupBox
        QGroupBox* group = nullptr;
        switch (s.category) {
            case StyleManager::StyleColorCategory::Border:     group = borderGroup;  break;
            case StyleManager::StyleColorCategory::Toolbar:    group = toolbarGroup; break;
            case StyleManager::StyleColorCategory::TabButton:
            case StyleManager::StyleColorCategory::TabWidgetBg: group = tabGroup;    break;
        }
        if (!group || s.defaultText[0] == '\0') {
            continue;  // defaultText 为空的行（SettingButton* / 角手柄）无 UI
        }

        QPushButton* btn = nullptr;
        QLabel* lbl = nullptr;
        QHBoxLayout* row = createColorRow(
            group, tm->get(s.translationKey, s.defaultText),
            s.getter(), btn, lbl);

        m_colorButtons[i] = btn;
        m_colorLabels[i] = lbl;

        // 用 lambda 捕获 StyleColorId 调用 applyColorChange
        connect(btn, &QPushButton::clicked, this, [this, id = s.id]() {
            applyColorChange(id);
        });

        QVBoxLayout* grpLayout = qobject_cast<QVBoxLayout*>(group->layout());
        if (grpLayout) {
            grpLayout->addLayout(row);
        }
    }

    // 工具栏按钮样式（非颜色行，单独处理）
    QHBoxLayout *toolbarButtonStyleLayout = new QHBoxLayout();
    lblToolbarButtonStyle = new QLabel(TranslationManager::instance()->get("style.toolbarButtonStyle", "Button Style:"), toolbarGroup);
    cmbToolbarButtonStyle = new QComboBox(toolbarGroup);
    cmbToolbarButtonStyle->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    cmbToolbarButtonStyle->addItem(TranslationManager::instance()->get("style.buttonStyle.text", "Text"), "text");
    cmbToolbarButtonStyle->addItem(TranslationManager::instance()->get("style.buttonStyle.icon", "Icon"), "icon");
    connect(cmbToolbarButtonStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsStyleTab::onToolbarButtonStyleChanged);
    toolbarButtonStyleLayout->addWidget(lblToolbarButtonStyle);
    toolbarButtonStyleLayout->addStretch();
    toolbarButtonStyleLayout->addWidget(cmbToolbarButtonStyle);
    toolbarLayout->addLayout(toolbarButtonStyleLayout);

    // 标注工具默认值设置
    m_annotationDefaultsGroup = new QGroupBox(tm->get("style.annotationDefaults", "Annotation Defaults"), styleTab);
    m_annotationDefaultsGroup->setStyleSheet(StyleManager::getGroupBoxStyle());
    QVBoxLayout *annotationDefaultsLayout = new QVBoxLayout(m_annotationDefaultsGroup);
    annotationDefaultsLayout->setContentsMargins(10, 10, 10, 10);
    annotationDefaultsLayout->setSpacing(8);

    // 画笔默认粗细
    QHBoxLayout *penWidthLayout = new QHBoxLayout();
    lblDefaultPenWidth = new QLabel(tm->get("style.defaultPenWidth", "Default Pen Width:"), m_annotationDefaultsGroup);
    spnDefaultPenWidth = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultPenWidth->setRange(1, 20);
    spnDefaultPenWidth->setSuffix(" px");
    spnDefaultPenWidth->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
    penWidthLayout->addWidget(lblDefaultPenWidth);
    penWidthLayout->addStretch();
    penWidthLayout->addWidget(spnDefaultPenWidth);
    annotationDefaultsLayout->addLayout(penWidthLayout);

    // 文本默认字号
    QHBoxLayout *fontSizeLayout = new QHBoxLayout();
    lblDefaultFontSize = new QLabel(tm->get("style.defaultFontSize", "Default Font Size:"), m_annotationDefaultsGroup);
    spnDefaultFontSize = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultFontSize->setRange(8, 48);
    spnDefaultFontSize->setSuffix(" px");
    spnDefaultFontSize->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
    fontSizeLayout->addWidget(lblDefaultFontSize);
    fontSizeLayout->addStretch();
    fontSizeLayout->addWidget(spnDefaultFontSize);
    annotationDefaultsLayout->addLayout(fontSizeLayout);

    // 橡皮擦默认粗细
    QHBoxLayout *eraserWidthLayout = new QHBoxLayout();
    lblDefaultEraserWidth = new QLabel(tm->get("style.defaultEraserWidth", "Default Eraser Width:"), m_annotationDefaultsGroup);
    spnDefaultEraserWidth = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultEraserWidth->setRange(1, 20);
    spnDefaultEraserWidth->setSuffix(" px");
    spnDefaultEraserWidth->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
    eraserWidthLayout->addWidget(lblDefaultEraserWidth);
    eraserWidthLayout->addStretch();
    eraserWidthLayout->addWidget(spnDefaultEraserWidth);
    annotationDefaultsLayout->addLayout(eraserWidthLayout);

    // 马赛克默认大小
    QHBoxLayout *mosaicSizeLayout = new QHBoxLayout();
    lblDefaultMosaicSize = new QLabel(tm->get("style.defaultMosaicSize", "Default Mosaic Size:"), m_annotationDefaultsGroup);
    spnDefaultMosaicSize = new QSpinBox(m_annotationDefaultsGroup);
    spnDefaultMosaicSize->setRange(1, 20);
    spnDefaultMosaicSize->setSuffix(" px");
    spnDefaultMosaicSize->setStyleSheet(StyleManager::getSettingsComboBoxStyle());
    spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
    mosaicSizeLayout->addWidget(lblDefaultMosaicSize);
    mosaicSizeLayout->addStretch();
    mosaicSizeLayout->addWidget(spnDefaultMosaicSize);
    annotationDefaultsLayout->addLayout(mosaicSizeLayout);

    // 连接信号
    connect(spnDefaultPenWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultPenWidth", value);
        StyleManager::setDefaultPenWidth(value);
        emit defaultPenWidthChanged(value);
    });
    connect(spnDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultFontSize", value);
        StyleManager::setDefaultFontSize(value);
        emit defaultFontSizeChanged(value);
    });
    connect(spnDefaultEraserWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultEraserWidth", value);
        StyleManager::setDefaultEraserWidth(value);
        emit defaultEraserWidthChanged(value);
    });
    connect(spnDefaultMosaicSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        ConfigManager::instance()->setValue("style/defaultMosaicSize", value);
        StyleManager::setDefaultMosaicSize(value);
        emit defaultMosaicSizeChanged(value);
    });

    layout->addWidget(tabGroup);
    layout->addStretch();
    
    // 创建样式选项卡容器（包含滚动区域 + 按钮）
    // 容器以子控件形式挂在选项卡根布局下（保持朴素 QWidget 形态，
    // 使 plain background-color 样式与子控件各自样式互不干扰）
    styleTabContainer = new QWidget(this);
    styleTabContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *styleTabLayout = new QVBoxLayout(styleTabContainer);
    QVBoxLayout *styleRootLayout = new QVBoxLayout(this);
    styleRootLayout->setContentsMargins(0, 0, 0, 0);
    styleRootLayout->setSpacing(0);
    styleRootLayout->addWidget(styleTabContainer);
    styleTabLayout->setContentsMargins(0, 0, 0, 10);
    styleTabLayout->setSpacing(0);
    
    // 创建一个容器来包含需要滚动的内容
    scrollContainer = new QWidget();
    scrollContainer->setStyleSheet(QString("background-color: %1;").arg(StyleManager::getTabWidgetBgColor().name()));
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContainer);
    scrollLayout->setContentsMargins(10, 10, 10, 10);
    scrollLayout->setSpacing(10);
    scrollLayout->addWidget(tabGroup);
    scrollLayout->addWidget(m_annotationDefaultsGroup);
    scrollLayout->addWidget(borderGroup);
    scrollLayout->addWidget(toolbarGroup);
    scrollLayout->addStretch();
    
    // 创建滚动区域
    styleScrollArea = new QScrollArea();
    styleScrollArea->setWidget(scrollContainer);
    styleScrollArea->setWidgetResizable(true);
    styleScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    styleScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    styleScrollArea->setFrameShape(QFrame::NoFrame);
    // 不调用 setStyleSheet 设置滚动条样式，滚动条样式由全局 QSS 提供
    // （与历史记录选项卡保持一致，避免 setStyleSheet 影响子控件原生渲染）
    
    // 重置按钮 - 放在滚动区域下面，固定显示
    btnResetStyle = new QPushButton(TranslationManager::instance()->get("style.resetStyle", "Reset to Default"), styleTabContainer);
    btnResetStyle->setObjectName("primaryButton");
    btnResetStyle->setStyleSheet(StyleManager::getSettingsButtonStyle());
    connect(btnResetStyle, &QPushButton::clicked, this, &SettingsStyleTab::onResetStyle);
    
    // 按钮居中
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(btnResetStyle);
    buttonLayout->addStretch();
    
    // 添加到容器布局
    styleTabLayout->addWidget(styleScrollArea);
    styleTabLayout->addLayout(buttonLayout);
    
}

/**
 * @brief 通用颜色变更处理
 *
 * 统一替代原先 17 个 onXxxColorChanged 槽函数的重复逻辑：
 * 1. 弹出颜色对话框
 * 2. 若用户选择了新颜色：更新 StyleManager、刷新按钮显示、保存设置
 * 3. 执行分类对应的样式联动
 * 4. 发射信号（若有：仅 tabWidgetBgColorChanged 有订阅者）
 * @param id 颜色 ID（StyleManager::StyleColorId，兼作按钮数组索引）
 * @author chiangyang
 */

// ============================================================
// 颜色变更
// ============================================================

void SettingsStyleTab::applyColorChange(StyleManager::StyleColorId id) {
    const auto& s = StyleManager::colorSettingTable()[static_cast<size_t>(id)];

    QColor currentColor = s.getter();
    QColor newColor = showColorDialog(currentColor);
    if (newColor == currentColor) {
        return;
    }

    s.setter(newColor);
    QPushButton* btn = m_colorButtons[static_cast<size_t>(id)];
    if (btn) {
        updateColorButton(btn, newColor);
    }
    saveStyleSettings();

    applyColorPostUpdate(s.category, newColor);
    // 发射信号（若有：仅 tabWidgetBgColorChanged 有订阅者，通知 HistoryWindow）
    if (s.signalId == StyleManager::StyleColorSignal::TabWidgetBg) {
        emit tabWidgetBgColorChanged();
    }
}

/**
 * @brief 颜色变更后的样式联动
 *
 * 不同分类的颜色变更会触发不同的样式刷新：
 * - Border/Toolbar：无后处理
 * - TabButton：仅刷新 tabWidget 样式表
 * - TabWidgetBg：刷新 tabWidget + 容器 + GroupBox + 三个 tab 的背景 + 历史 tab Palette
 *
 * @param category 颜色分类（StyleManager::StyleColorCategory）
 * @param newColor 新颜色（仅 TabWidgetBg 类用到）
 * @author chiangyang
 */

void SettingsStyleTab::applyColorPostUpdate(StyleManager::StyleColorCategory category, const QColor& newColor) {
    switch (category) {
        case StyleManager::StyleColorCategory::Border:
        case StyleManager::StyleColorCategory::Toolbar:
            break;
        case StyleManager::StyleColorCategory::TabButton:
            emit tabWidgetStyleChanged();
            break;
        case StyleManager::StyleColorCategory::TabWidgetBg:
            if (styleTabContainer) {
                styleTabContainer->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
            }
            if (scrollContainer) {
                scrollContainer->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
            }
            emit tabWidgetStyleChanged();
            emit tabWidgetBgChanged(newColor);
            break;
    }
}

/**
 * @brief 显示颜色选择器
 * @param currentColor 当前颜色
 * @return 选择的颜色
 * @author chiangyang
 */

QColor SettingsStyleTab::showColorDialog(const QColor &currentColor) {
    QColorDialog colorDialog(currentColor, this);
    colorDialog.setWindowTitle(TranslationManager::instance()->get("style.selectColor", "Select Color"));
    if (colorDialog.exec() == QColorDialog::Accepted) {
        return colorDialog.currentColor();
    }
    return currentColor;
}

/**
 * @brief 创建一行颜色选择控件（标签 + 弹性间距 + 色块按钮）
 *
 * 统一封装样式选项卡中重复的颜色行创建逻辑。色块按钮使用 #settingColorButton
 * 对象名，尺寸/边框/圆角由全局 qss 管理，此处只设置 background-color。
 * @param parent 父控件（通常为所属 QGroupBox）
 * @param labelText 标签文本（已翻译）
 * @param color 初始颜色
 * @param outBtn 输出：创建的色块按钮指针（供调用方保存为成员变量）
 * @param outLabel 输出：创建的标签指针（供 retranslateUi 更新文本）
 * @return 包含标签和按钮的水平布局（已含 addStretch）
 * @author chiangyang
 */

QHBoxLayout *SettingsStyleTab::createColorRow(QWidget *parent, const QString &labelText,
                                             const QColor &color, QPushButton *&outBtn, QLabel *&outLabel) {
    QLabel *label = new QLabel(labelText, parent);
    QPushButton *btn = new QPushButton(parent);
    btn->setObjectName("settingColorButton");
    updateColorButton(btn, color);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(label);
    layout->addStretch();
    layout->addWidget(btn);

    outBtn = btn;
    outLabel = label;
    return layout;
}

/**
 * @brief 更新颜色按钮的显示颜色
 *
 * 仅设置 background-color，尺寸/边框/圆角仍由全局 qss 的 #settingColorButton 管理，
 * 保持 DPI 自适应缩放。
 * @param btn 颜色按钮
 * @param color 新颜色
 * @author chiangyang
 */

void SettingsStyleTab::updateColorButton(QPushButton *btn, const QColor &color) {
    if (btn) {
        btn->setStyleSheet(QString("background-color: %1;").arg(color.name()));
    }
}

/**
 * @brief 重置样式槽函数
 * @author chiangyang
 */

// ============================================================
// 重置 / 工具栏按钮样式 / 保存 / 加载
// ============================================================

void SettingsStyleTab::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    if (borderGroup) borderGroup->setTitle(tm->get("style.borderColor", "Border Colors"));
    if (toolbarGroup) toolbarGroup->setTitle(tm->get("style.toolbarStyle", "Toolbar Style"));
    // Tab Style Group
    QGroupBox *tabGroup = findChild<QGroupBox*>("tabGroup");
    if (tabGroup) tabGroup->setTitle(tm->get("style.tabStyle", "Tab Style"));

    // 通过数据驱动表循环更新所有颜色标签的翻译文本（表归 StyleManager 所有）
    {
        const auto& table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            if (m_colorLabels[i] && table[i].defaultText[0] != '\0') {
                m_colorLabels[i]->setText(tm->get(table[i].translationKey, table[i].defaultText));
            }
        }
    }

    if (btnResetStyle) btnResetStyle->setText(tm->get("style.resetStyle", "Reset to Default"));
    // 标注工具默认值组
    if (m_annotationDefaultsGroup) m_annotationDefaultsGroup->setTitle(tm->get("style.annotationDefaults", "Annotation Defaults"));
    if (lblDefaultPenWidth) lblDefaultPenWidth->setText(tm->get("style.defaultPenWidth", "Default Pen Width:"));
    if (lblDefaultFontSize) lblDefaultFontSize->setText(tm->get("style.defaultFontSize", "Default Font Size:"));
    if (lblDefaultEraserWidth) lblDefaultEraserWidth->setText(tm->get("style.defaultEraserWidth", "Default Eraser Width:"));
    if (lblDefaultMosaicSize) lblDefaultMosaicSize->setText(tm->get("style.defaultMosaicSize", "Default Mosaic Size:"));
    if (lblToolbarButtonStyle) lblToolbarButtonStyle->setText(tm->get("style.toolbarButtonStyle", "Button Style:"));
    if (cmbToolbarButtonStyle) {
        int currentIndex = cmbToolbarButtonStyle->currentIndex();
        cmbToolbarButtonStyle->clear();
        cmbToolbarButtonStyle->addItem(tm->get("style.buttonStyle.text", "Text"), "text");
        cmbToolbarButtonStyle->addItem(tm->get("style.buttonStyle.icon", "Icon"), "icon");
        cmbToolbarButtonStyle->setCurrentIndex(currentIndex);
    }
}

void SettingsStyleTab::refreshStyles() {
    const QString style = StyleManager::getGroupBoxStyle();
    if (borderGroup) borderGroup->setStyleSheet(style);
    if (toolbarGroup) toolbarGroup->setStyleSheet(style);
    if (tabGroup) tabGroup->setStyleSheet(style);
    if (m_annotationDefaultsGroup) m_annotationDefaultsGroup->setStyleSheet(style);
}

void SettingsStyleTab::onResetStyle() {
    if (MessageBox::question(this,
            TranslationManager::instance()->get("style.resetStyle", "Reset to Default"),
            TranslationManager::instance()->get("style.confirmReset", "Are you sure you want to reset all styles to default values?"))) {
        // 重置为默认值
        StyleManager::resetToDefaults();

        // 通过数据驱动表循环刷新所有颜色按钮（表归 StyleManager 所有）
        const auto& table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            if (m_colorButtons[i]) {
                updateColorButton(m_colorButtons[i], table[i].getter());
            }
        }

        // 重置标注工具默认值的 SpinBox
        if (spnDefaultPenWidth) {
            spnDefaultPenWidth->blockSignals(true);
            spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
            spnDefaultPenWidth->blockSignals(false);
        }
        if (spnDefaultFontSize) {
            spnDefaultFontSize->blockSignals(true);
            spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
            spnDefaultFontSize->blockSignals(false);
        }

        // 重置橡皮擦和马赛克默认值的 SpinBox
        if (spnDefaultEraserWidth) {
            spnDefaultEraserWidth->blockSignals(true);
            spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
            spnDefaultEraserWidth->blockSignals(false);
        }
        if (spnDefaultMosaicSize) {
            spnDefaultMosaicSize->blockSignals(true);
            spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
            spnDefaultMosaicSize->blockSignals(false);
        }

        // 选项卡背景相关的样式联动
        QColor defaultTabWidgetBgColor = StyleManager::getTabWidgetBgColor();
        applyColorPostUpdate(StyleManager::StyleColorCategory::TabWidgetBg, defaultTabWidgetBgColor);

        // 保存默认值到QSettings
        saveStyleSettings();

        // 发送颜色变更信号（仅选项卡背景有订阅者：HistoryWindow）
        emit tabWidgetBgColorChanged();

        // 发送标注工具默认值变更信号（通知工具栏更新滑块）
        emit defaultPenWidthChanged(StyleManager::getDefaultPenWidth());
        emit defaultFontSizeChanged(StyleManager::getDefaultFontSize());
        emit defaultEraserWidthChanged(StyleManager::getDefaultEraserWidth());
        emit defaultMosaicSizeChanged(StyleManager::getDefaultMosaicSize());

        // 重新应用样式到所有UI组件
        retranslateUi();
    }
}

/**
 * @brief 工具栏画笔粗细变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节画笔粗细时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param width 新的画笔粗细值
 * @author chiangyang
 */

void SettingsStyleTab::onToolbarButtonStyleChanged(int index) {
    QSettings *settings = ConfigManager::instance()->getSettings();
    QString style = cmbToolbarButtonStyle->itemData(index).toString();
    settings->setValue("style/toolbarButtonStyle", style);
    settings->sync();

    // 同步更新 StyleManager，确保后续创建的工具栏使用最新样式
    StyleManager::setToolbarButtonStyle(style);

    // 发送信号通知已有的工具栏和控制栏更新样式
    emit toolbarButtonStyleChanged(style);
}

/**
 * @brief 保存样式设置
 * @author chiangyang
 */

void SettingsStyleTab::saveStyleSettings() {
    QSettings *settings = ConfigManager::instance()->getSettings();

    // 通过数据驱动表循环保存所有颜色（表归 StyleManager 所有）
    const auto& table = StyleManager::colorSettingTable();
    for (const auto& s : table) {
        settings->setValue(QString("style/%1").arg(s.settingsKey), s.getter().name());
    }
    settings->setValue("style/toolbarButtonStyle", cmbToolbarButtonStyle->currentData().toString());

    // 保存标注工具默认值
    settings->setValue("style/defaultPenWidth", qBound(1, StyleManager::getDefaultPenWidth(), 20));
    settings->setValue("style/defaultFontSize", qBound(8, StyleManager::getDefaultFontSize(), 48));
    // 保存橡皮擦和马赛克默认值
    settings->setValue("style/defaultEraserWidth", qBound(1, StyleManager::getDefaultEraserWidth(), 20));
    settings->setValue("style/defaultMosaicSize", qBound(1, StyleManager::getDefaultMosaicSize(), 20));

    // 同步保存
    settings->sync();
}

/**
 * @brief 加载样式设置
 *
 * StyleManager 已在启动时由 initFromConfig 从持久化恢复全部样式值，
 * 此处仅把 StyleManager 已初始化的值同步到设置页 UI，
 * 不再直接读 QSettings（避免设置页持有第二套读取逻辑）。
 * @author chiangyang
 */

void SettingsStyleTab::loadStyleSettings() {
    // 通过数据驱动表循环把当前颜色同步到色块按钮
    const auto& table = StyleManager::colorSettingTable();
    for (size_t i = 0; i < table.size(); ++i) {
        const auto& s = table[i];
        if (m_colorButtons[i]) {
            updateColorButton(m_colorButtons[i], s.getter());
        }
    }

    // 选项卡背景相关的样式联动
    QColor tabWidgetBgColor = StyleManager::getTabWidgetBgColor();
    applyColorPostUpdate(StyleManager::StyleColorCategory::TabWidgetBg, tabWidgetBgColor);

    // 工具栏按钮样式（已由 initFromConfig 恢复）
    QString toolbarButtonStyle = StyleManager::getToolbarButtonStyle();
    int styleIndex = cmbToolbarButtonStyle->findData(toolbarButtonStyle);
    if (styleIndex >= 0) {
        cmbToolbarButtonStyle->setCurrentIndex(styleIndex);
    }

    // 标注工具默认值（已由 initFromConfig 恢复，同步到 SpinBox）
    if (spnDefaultPenWidth) {
        spnDefaultPenWidth->blockSignals(true);
        spnDefaultPenWidth->setValue(StyleManager::getDefaultPenWidth());
        spnDefaultPenWidth->blockSignals(false);
    }
    if (spnDefaultFontSize) {
        spnDefaultFontSize->blockSignals(true);
        spnDefaultFontSize->setValue(StyleManager::getDefaultFontSize());
        spnDefaultFontSize->blockSignals(false);
    }
    if (spnDefaultEraserWidth) {
        spnDefaultEraserWidth->blockSignals(true);
        spnDefaultEraserWidth->setValue(StyleManager::getDefaultEraserWidth());
        spnDefaultEraserWidth->blockSignals(false);
    }
    if (spnDefaultMosaicSize) {
        spnDefaultMosaicSize->blockSignals(true);
        spnDefaultMosaicSize->setValue(StyleManager::getDefaultMosaicSize());
        spnDefaultMosaicSize->blockSignals(false);
    }
}

/**
 * @brief 选项卡切换槽函数，用于自适应调整窗口高度
 * @param index 选项卡索引
 * @author chiangyang
 */

// ============================================================
// 工具栏联动槽（SnipScreen 工具栏 → 宿主 → 本选项卡）
// ============================================================

void SettingsStyleTab::onToolPenWidthChanged(int width) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultPenWidth) {
        spnDefaultPenWidth->blockSignals(true);
        spnDefaultPenWidth->setValue(width);
        spnDefaultPenWidth->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultPenWidth", width);
    StyleManager::setDefaultPenWidth(width);
    LOG_INFO(QString("SettingsWindow: Tool pen width changed to %1 (persisted)").arg(width));
}

/**
 * @brief 工具栏字号变更槽函数
 *
 * 当用户在工具栏滑块调节字号时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param size 新的字号值
 * @author chiangyang
 */

void SettingsStyleTab::onToolFontSizeChanged(int size) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultFontSize) {
        spnDefaultFontSize->blockSignals(true);
        spnDefaultFontSize->setValue(size);
        spnDefaultFontSize->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultFontSize", size);
    StyleManager::setDefaultFontSize(size);
    LOG_INFO(QString("SettingsWindow: Tool font size changed to %1 (persisted)").arg(size));
}

/**
 * @brief 工具栏橡皮擦粗细变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节橡皮擦粗细时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param width 新的橡皮擦粗细值
 * @author chiangyang
 */

void SettingsStyleTab::onToolEraserWidthChanged(int width) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultEraserWidth) {
        spnDefaultEraserWidth->blockSignals(true);
        spnDefaultEraserWidth->setValue(width);
        spnDefaultEraserWidth->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultEraserWidth", width);
    StyleManager::setDefaultEraserWidth(width);
    LOG_INFO(QString("SettingsWindow: Tool eraser width changed to %1 (persisted)").arg(width));
}

/**
 * @brief 工具栏马赛克大小变更槽函数
 *
 * 当用户在工具栏滑块或快捷键调节马赛克大小时调用，
 * 更新 SpinBox 显示并持久化到配置，同步更新全局默认值。
 * @param size 新的马赛克大小值
 * @author chiangyang
 */

void SettingsStyleTab::onToolMosaicSizeChanged(int size) {
    // 更新 SpinBox 显示（屏蔽信号避免重入）
    if (spnDefaultMosaicSize) {
        spnDefaultMosaicSize->blockSignals(true);
        spnDefaultMosaicSize->setValue(size);
        spnDefaultMosaicSize->blockSignals(false);
    }
    // 持久化到配置并更新全局默认值
    ConfigManager::instance()->setValue("style/defaultMosaicSize", size);
    StyleManager::setDefaultMosaicSize(size);
    LOG_INFO(QString("SettingsWindow: Tool mosaic size changed to %1 (persisted)").arg(size));
}

/**
 * @brief 工具栏按钮样式改变槽函数
 * @param index 下拉框索引
 * @author chiangyang
 */
