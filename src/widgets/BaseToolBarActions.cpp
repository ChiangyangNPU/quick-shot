#include "BaseToolBar.h"

// ============================================================
// BaseToolBar 动作与样式层（自 BaseToolBar.cpp 分层拆出）
//
// 承载：动作按钮注册表（ActionBtnSpec + makeActionButton）、
// 标注工具元数据表（ToolDef）、按钮样式应用与翻译刷新、
// 画笔宽度/颜色/形状子类型的操作入口、工具栏状态切换。
// 核心骨架（构造/子工具栏变体系统/定位/eventFilter）仍在
// BaseToolBar.cpp；本文件变更只重编译动作与样式相关代码。
// ============================================================

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <functional>
#include "StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../capture/Annotation.h"

using namespace ToolIds;

// ============================================================
// 标注工具元数据
// ============================================================

struct ToolDef {
    int toolId;
    QString iconPath;
    QString translationKey;
};

static const ToolDef s_toolDefs[] = {
    {RECTANGLE, ":/icons/rect.svg", QString()},
    {ELLIPSE,   ":/icons/ellipse.svg", QString()},
    {ARROW,     ":/icons/arrow.svg", QString()},
    {PEN,       ":/icons/pen.svg", QString()},
    {LINE,      ":/icons/line.svg", QString()},
    {TEXT,      ":/icons/text.svg", QString()},
    {MOSAIC,    ":/icons/mosaic.svg", QString()},
    {ERASER,    ":/icons/erase.svg", QString()},
};

static constexpr int s_numToolDefs = sizeof(s_toolDefs) / sizeof(s_toolDefs[0]);

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
QPushButton *BaseToolBar::makeActionButton(const ActionBtnSpec &spec, std::function<void()> onClicked) {
    QPushButton *btn = new QPushButton(TranslationManager::instance()->get(spec.trKey), this);
    if (spec.closeStyle) {
        btn->setObjectName("cancelButton");
        StyleManager::applyCloseButtonStyle(btn);
    } else {
        StyleManager::applyActionButtonStyle(btn);
    }
    connect(btn, &QPushButton::clicked, this, std::move(onClicked));
    m_actionButtons.append({btn, spec});
    *spec.slot = btn;
    return btn;
}

void BaseToolBar::updateActionButtons(bool isIcon) {
    auto *tm = TranslationManager::instance();
    for (const auto &entry : std::as_const(m_actionButtons)) {
        applyButtonStyle(entry.first, entry.second.icon,
                         tm->get(entry.second.trKey), isIcon);
    }
}

void BaseToolBar::retranslateActionButtons() {
    auto *tm = TranslationManager::instance();
    for (const auto &entry : std::as_const(m_actionButtons)) {
        entry.first->setText(tm->get(entry.second.trKey));
    }
}

/**
 * @brief 创建标注工具按钮组
 *
 * 创建矩形、椭圆、箭头、画笔、直线、文本、马赛克、橡皮擦 8 个标注工具按钮。
 * 按钮为 checkable，实现 radio 互斥行为。
 * @author chiangyang
 */
void BaseToolBar::createAnnotationTools() {
    LOG_INFO("BaseToolBar: Creating annotation tools");

    auto *tm = TranslationManager::instance();

    struct { const char *key; const char *fallback; } toolNames[] = {
        {"shape.rect", "Rectangle"}, {"ellipse", "Ellipse"},
        {"arrow", "Arrow"},         {"pen", "Pen"},
        {"line", "Line"},           {"text", "Text"},
        {"mosaic", "Mosaic"},       {"eraser", "Eraser"},
    };

    for (int i = 0; i < s_numToolDefs; ++i) {
        const auto &def = s_toolDefs[i];
        QString name = tm->get(toolNames[i].key, toolNames[i].fallback);

        // Initialize tool settings with defaults from StyleManager
        ToolSettings ts;
        // 根据工具类型设置不同的默认值和范围
        if (def.toolId == MOSAIC) {
            ts.penWidth = StyleManager::getDefaultMosaicSize();
        } else if (def.toolId == ERASER) {
            ts.penWidth = StyleManager::getDefaultEraserWidth();
        } else {
            ts.penWidth = StyleManager::getDefaultPenWidth();
        }
        ts.fontSize = StyleManager::getDefaultFontSize();
        ts.penWidthModified = false;
        ts.fontSizeModified = false;
        m_toolSettings[def.toolId] = ts;

        QPushButton *btn = new QPushButton(name, this);
        btn->setProperty("toolId", def.toolId);
        btn->setCheckable(true);
        StyleManager::applyToolButtonStyle(btn);
        m_primaryLayout->addWidget(btn);
        m_annotationBtns.push_back(btn);

        connect(btn, &QPushButton::toggled, [this, btn](bool checked) {
            int toolId = btn->property("toolId").toInt();
            if (checked) {
                // 取消其他标注按钮的选中状态（radio 行为）
                for (auto *other : m_annotationBtns) {
                    if (other != btn && other->isChecked()) {
                        other->blockSignals(true);
                        other->setChecked(false);
                        other->blockSignals(false);
                        other->setStyleSheet(StyleManager::getToolButtonStyle());
                    }
                }
                btn->setStyleSheet(StyleManager::getButtonCheckedStyle());
                emit toolSelected(toolId);
                showSubTools(toolId);
            } else {
                btn->setStyleSheet(StyleManager::getToolButtonStyle());
                // 隐藏子工具栏（变体窗口不销毁，保持常驻以保证下次切换零创建无闪烁）
                if (m_activeSubVariant < VariantCount) {
                    m_subVariants[m_activeSubVariant].window->hide();
                    LOG_INFO(QString("Active sub-toolbar variant %1 hidden (not destroyed)").arg(m_activeSubVariant));
                    m_activeSubVariant = VariantCount;
                }
                subToolbarWindow = nullptr;
                emit annotationToolDeselected();
            }
        });
    }

    LOG_INFO("BaseToolBar: Annotation tools created");
}

void BaseToolBar::selectAnnotationTool(int toolId) {
    for (auto *btn : m_annotationBtns) {
        if (btn->property("toolId").toInt() == toolId) {
            btn->setChecked(true);
            return;
        }
    }
}

/**
 * @brief 在布局中添加垂直分隔线
 * @param layout 目标布局
 * @author chiangyang
 */
void BaseToolBar::addSeparator(QHBoxLayout *layout) {
    QWidget *separator = new QWidget(this);
    // 宽度/高度由全局 qss 管理，随 DPI 缩放；颜色用半透明深色，与浅灰工具栏背景形成对比
    separator->setObjectName("toolbarSeparator");
    separator->setStyleSheet("background-color: rgba(0, 0, 0, 40);");
    layout->addWidget(separator);
}

/**
 * @brief 应用单个按钮的文字/图标样式
 * @param btn 按钮
 * @param iconPath 图标 SVG 路径
 * @param text 按钮文字
 * @param isIcon true=图标模式, false=文字模式
 * @author chiangyang
 */
void BaseToolBar::applyButtonStyle(QPushButton *btn, const QString &iconPath,
                                   const QString &text, bool isIcon) {
    if (!btn) return;
    if (isIcon) {
        btn->setText("");
        btn->setIcon(StyleManager::loadSvgIcon(iconPath));
        btn->setToolTip(text);
    } else {
        btn->setText(text);
        btn->setIcon(QIcon());
        btn->setToolTip("");
    }
    if (btn->isChecked()) {
        btn->setStyleSheet(StyleManager::getButtonCheckedStyle());
    } else if (btn->objectName() == "cancelButton") {
        // 取消/关闭按钮使用专属红色样式，避免被普通工具按钮样式覆盖
        StyleManager::applyCloseButtonStyle(btn);
    } else {
        StyleManager::applyToolButtonStyle(btn);
    }
    btn->updateGeometry();
}

/**
 * @brief 更新所有标注工具按钮的文字/图标样式
 * @param isIcon true=图标模式, false=文字模式
 * @author chiangyang
 */
void BaseToolBar::updateAnnotationButtonStyles(bool isIcon) {
    auto *tm = TranslationManager::instance();

    const char *toolKeys[] = {"shape.rect", "ellipse", "arrow", "pen", "line", "text", "mosaic", "eraser"};
    const char *toolFallbacks[] = {
        "Rectangle", "Ellipse", "Arrow", "Pen", "Line", "Text", "Mosaic", "Eraser"};

    for (auto *btn : m_annotationBtns) {
        int toolId = btn->property("toolId").toInt();
        for (int i = 0; i < s_numToolDefs; ++i) {
            if (s_toolDefs[i].toolId == toolId) {
                applyButtonStyle(btn, s_toolDefs[i].iconPath,
                                 tm->get(toolKeys[i], toolFallbacks[i]), isIcon);
                break;
            }
        }
    }
}

/**
 * @brief 重新翻译标注工具按钮文字
 * @author chiangyang
 */
void BaseToolBar::retranslateAnnotationButtons() {
    auto *tm = TranslationManager::instance();

    struct { const char *key; const char *fallback; } toolNames[] = {
        {"shape.rect", "Rectangle"}, {"ellipse", "Ellipse"},
        {"arrow", "Arrow"},         {"pen", "Pen"},
        {"line", "Line"},           {"text", "Text"},
        {"mosaic", "Mosaic"},       {"eraser", "Eraser"},
    };

    for (size_t i = 0; i < sizeof(toolNames) / sizeof(toolNames[0]) && i < m_annotationBtns.size(); ++i) {
        m_annotationBtns[i]->setText(tm->get(toolNames[i].key, toolNames[i].fallback));
    }
}

/**
 * @brief 设置当前工具的画笔宽度
 *
 * 供 SnipScreen 的 [/] 快捷键调用：更新 m_toolSettings 中当前工具的画笔宽度，
 * 同步子工具栏滑块显示（blockSignals 防止 penWidthChanged 重入），
 * 再发射 penWidthChanged 让 SnipScreen 同步 m_currentPenWidth。
 * @param width 新的画笔宽度
 * @author chiangyang
 */
void BaseToolBar::setCurrentPenWidth(int width) {
    if (m_currentToolId == -1) return;
    m_toolSettings[m_currentToolId].penWidth = width;
    m_toolSettings[m_currentToolId].penWidthModified = true;
    // 同步子工具栏滑块（若存在且可见），屏蔽信号避免 valueChanged 重入
    if (m_penWidthSlider) {
        m_penWidthSlider->blockSignals(true);
        m_penWidthSlider->setValue(width);
        m_penWidthSlider->blockSignals(false);
    }
    emit penWidthChanged(width);
    // 同步到 SettingsWindow 显示并持久化（按工具类型发射对应信号）
    if (m_currentToolId == MOSAIC) {
        emit toolMosaicSizeChanged(width);
    } else if (m_currentToolId == ERASER) {
        emit toolEraserWidthChanged(width);
    } else {
        emit toolPenWidthChanged(width);
    }
    LOG_INFO(QString("[Shortcut] setCurrentPenWidth: tool=%1 width=%2").arg(m_currentToolId).arg(width));
}

/**
 * @brief 设置当前工具的画笔颜色
 *
 * 供 SnipScreen 的 Tab 快捷键循环切色调用：更新 m_toolSettings 中当前工具的颜色，
 * 同步颜色面板选中态（重置所有颜色按钮为未选中，匹配色按钮设为选中），
 * 再发射 penColorChanged 让 SnipScreen 同步 m_currentColor。
 * @param color 新的画笔颜色
 * @author chiangyang
 */
void BaseToolBar::setCurrentColor(const QColor &color) {
    if (m_currentToolId == -1) return;
    m_toolSettings[m_currentToolId].color = color;
    // 同步颜色面板选中态：重置全部按钮，匹配色按钮设为选中
    m_selectedColorBtn = nullptr;
    for (QPushButton *btn : m_colorBtns) {
        QColor btnColor(btn->property("color").toString());
        bool selected = (btnColor == color);
        btn->setStyleSheet(StyleManager::getColorButtonStyle(btnColor.name(), selected));
        if (selected) {
            m_selectedColorBtn = btn;
        }
    }
    // 无匹配按钮时（颜色不在预设面板内），保留原选中按钮但更新其颜色属性与样式
    if (!m_selectedColorBtn && !m_colorBtns.isEmpty()) {
        QPushButton *first = m_colorBtns.first();
        first->setProperty("color", color.name());
        first->setStyleSheet(StyleManager::getColorButtonStyle(color.name(), true));
        m_selectedColorBtn = first;
    }
    emit penColorChanged(color);
    LOG_INFO(QString("[Shortcut] setCurrentColor: tool=%1 color=%2").arg(m_currentToolId).arg(color.name()));
}

/**
 * @brief 切换到颜色面板中的下一个颜色（供 Tab 快捷键调用）
 *
 * 在 m_colorBtns 中定位当前选中按钮 m_selectedColorBtn 的索引，取下一个（末尾回绕到首部），
 * 复用 setCurrentColor 同步选中态并发射 penColorChanged。这样 Tab 切色按面板实际顺序
 * 循环遍历全部预设颜色，且始终从当前选中色的下一个开始，与用户点击选中的颜色保持同步。
 * @author chiangyang
 */
void BaseToolBar::selectNextColor() {
    if (m_colorBtns.isEmpty()) return;
    int idx = 0;
    if (m_selectedColorBtn) {
        int curIdx = m_colorBtns.indexOf(m_selectedColorBtn);
        if (curIdx >= 0) {
            idx = (curIdx + 1) % m_colorBtns.size();
        }
    }
    QPushButton *btn = m_colorBtns[idx];
    QColor color(btn->property("color").toString());
    setCurrentColor(color);
}

/**
 * @brief 更新形状工具按钮的图标/文字
 * @param shapeType 形状类型（1=矩形, 2=椭圆, 3=三角形）
 * @author chiangyang
 */
void BaseToolBar::updateShapeIcon(int shapeType) {
    const bool isIcon = StyleManager::getToolbarButtonStyle() == "icon";
    TranslationManager *tm = TranslationManager::instance();

    for (auto *btn : m_annotationBtns) {
        int toolId = btn->property("toolId").toInt();
        if (toolId == RECTANGLE) {
            QString iconPath;
            QString text;
            switch (shapeType) {
                case 1: iconPath = ":/icons/rect.svg";     text = tm->get("shape.rect", "Rectangle");     break;
                case 2: iconPath = ":/icons/ellipse.svg";  text = tm->get("shape.ellipse", "Ellipse");    break;
                case 3: iconPath = ":/icons/triangle.svg"; text = tm->get("shape.triangle", "Triangle"); break;
                default: iconPath = ":/icons/rect.svg";    text = tm->get("shape.rect", "Rectangle");
            }
            applyButtonStyle(btn, iconPath, text, isIcon);
            break;
        }
    }
}

/**
 * @brief 更新工具栏按钮状态
 * @param hasSelection 是否有选区（保留参数，预留扩展）
 * @param canUndo 是否可撤销
 * @param canRedo 是否可重做
 * @author chiangyang
 */
void BaseToolBar::updateState(bool hasSelection, bool canUndo, bool canRedo) {
    LOG_INFO(QString("BaseToolBar: Updating state - selection: %1, canUndo: %2, canRedo: %3")
        .arg(hasSelection).arg(canUndo).arg(canRedo));
    if (m_undoBtn)  m_undoBtn->setEnabled(canUndo);
    if (m_redoBtn)  m_redoBtn->setEnabled(canRedo);
    if (m_clearBtn) m_clearBtn->setEnabled(canUndo || canRedo);
}

/**
 * @brief 取消所有标注工具按钮的选中状态
 * @author chiangyang
 */
void BaseToolBar::uncheckAllAnnotationBtns() {
    for (auto *btn : m_annotationBtns) {
        if (btn->isChecked()) {
            btn->blockSignals(true);
            btn->setChecked(false);
            btn->blockSignals(false);
            btn->setStyleSheet(StyleManager::getToolButtonStyle());
        }
    }
}
