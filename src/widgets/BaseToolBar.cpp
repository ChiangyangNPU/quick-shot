#include "BaseToolBar.h"
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QComboBox>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
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

// ============================================================
// 构造
// ============================================================

/**
 * @brief 构造函数
 * @param parent 父窗口
 * @author chiangyang
 */
BaseToolBar::BaseToolBar(QWidget *parent) : QWidget(parent) {
    m_isChinese = QApplication::translate("BaseToolBar", "Chinese").contains("中文");
    LOG_INFO(QString("BaseToolBar instance created, language: %1").arg(m_isChinese ? "Chinese" : "English"));
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
 * 半径与 QSS 中的 border-radius: 0.3em 一致，按当前字体 em 高度换算。
 * @param event 绘制事件
 * @author chiangyang
 */
void BaseToolBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(StyleManager::getToolbarBgColor());
    int radius = qRound(fontMetrics().height() * 0.3);
    painter.drawRoundedRect(rect(), radius, radius);
}

// ============================================================
// UI 翻译（默认空实现，子类实现具体逻辑）
// ============================================================

/**
 * @brief 重新翻译UI文本
 *
 * 基础类不实现具体的翻译，由子类实现
 * @author chiangyang
 */
void BaseToolBar::retranslateUi() {
    // 若 Rectangle 变体已构建，重新填充形状下拉框文本（语言切换）
    SubToolVariantData &rect = m_subVariants[VariantRectangle];
    if (rect.built && rect.shapeCombo) {
        TranslationManager *tm = TranslationManager::instance();
        rect.shapeCombo->setItemText(0, tm->get("shape.rect", "Rectangle"));
        rect.shapeCombo->setItemText(1, tm->get("shape.ellipse", "Ellipse"));
        rect.shapeCombo->setItemText(2, tm->get("shape.triangle", "Triangle"));
    }
}

// ============================================================
// 子工具栏显示
// ============================================================

/**
 * @brief 显示子工具栏
 * @param toolId 工具ID
 *
 * 采用"4 种变体预创建 + 热切换仅翻可见性"策略，彻底消除切换时的闪烁：
 *   1. 变体窗口一旦构建后常驻内存，切换路径零创建/零销毁；
 *   2. 先调出目标变体（show + raise），再退下旧变体（hide），新旧同背景色重叠 1 帧；
 *   3. 每个变体都是 fresh widget 的首次布局，QSS min-width / sizeHint 计算正确，不会压缩。
 *   变体成员绑定：切换后 m_penWidthSlider / m_colorBtns / m_selectedColorBtn /
 *   subToolbarWindow / subToolbarLayout 都指向新变体对应对象，外部调用方无感。
 * @author chiangyang
 */
void BaseToolBar::showSubTools(int toolId) {
    LOG_INFO(QString("Show sub-toolbar for tool ID=%1").arg(toolId));

    // 保存当前工具的设置（如果当前有选中工具）
    if (m_currentToolId != -1 && m_currentToolId != toolId) {
        LOG_INFO(QString("Save settings for tool ID=%1 before switching").arg(m_currentToolId));
    }

    // 更新当前工具ID
    m_currentToolId = toolId;

    // 1) 工具 ID → 变体枚举映射
    SubToolVariant newVariant = variantForToolId(toolId);
    LOG_INFO(QString("Tool id %1 maps to sub-toolbar variant %2").arg(toolId).arg(newVariant));

    // 2) 懒构建变体（首次命中才构建，此后复用）
    buildSubVariant(newVariant);
    SubToolVariantData &data = m_subVariants[newVariant];

    // 3) 位置计算：复用当前激活变体的坐标 → 切换视觉不跳位；未激活则走默认初始位置
    int subX, subY;
    if (m_activeSubVariant < VariantCount) {
        subX = m_subVariants[m_activeSubVariant].window->x();
        subY = m_subVariants[m_activeSubVariant].window->y();
        LOG_INFO(QString("Reuse active variant position (%1, %2)").arg(subX).arg(subY));
    } else {
        subX = this->x();
        subY = this->y() + this->height();
    }

    // 4) 用目标工具的设置同步变体控件（滑块值 / 选中颜色按钮 / 形状初始值）
    //    必须在 layout activate 前完成，否则尺寸计算不准
    syncVariantState(newVariant, toolId);

    // 5) 布局 + 尺寸 + 定位
    data.layout->activate();
    data.window->adjustSize();
    int toolbarHeight = this->height();
    data.window->setFixedHeight(toolbarHeight);
    data.window->move(subX, subY);
    LOG_INFO(QString("Variant %1 sub-toolbar size: %2x%3, pos=(%4,%5)")
        .arg(newVariant).arg(data.window->width()).arg(data.window->height())
        .arg(subX).arg(subY));

    // 6) 绑定成员指针 → 后续 setCurrentPenWidth / setCurrentColor / getSubToolbarWindow 都走新变体
    bindVariantInternals(newVariant);

    // 7) 原子交换：先 show + raise 新变体，再 hide 旧变体（旧变体仍常驻，只是不再可见）
    data.window->show();
    data.window->raise();
    if (m_activeSubVariant < VariantCount && m_activeSubVariant != newVariant) {
        m_subVariants[m_activeSubVariant].window->hide();
        LOG_INFO(QString("Variant %1 hidden, variant %2 shown (atomic swap)").arg(m_activeSubVariant).arg(newVariant));
    } else {
        LOG_INFO(QString("Variant %1 shown (first open)").arg(newVariant));
    }
    m_activeSubVariant = newVariant;
    // 更新对外别名（供 getSubToolbarWindow / SnipScreen::calculateToolbarPositions 等使用）
    subToolbarWindow = data.window;
    subToolbarLayout = data.layout;

    // 8) 发射对应的信号，让控制器以当前工具设置为准
    const ToolSettings &ts = m_toolSettings[toolId];
    if (toolId == TEXT) {
        emit fontSizeChanged(ts.fontSize);
        LOG_INFO(QString("Emit font size signal: %1").arg(ts.fontSize));
        emit penWidthChanged(ts.penWidth);
        LOG_INFO(QString("Emit line width signal: %1").arg(ts.penWidth));
        emit penColorChanged(ts.color);
        LOG_INFO("Emit pen color signal for TEXT tool");
    } else if (toolId != MOSAIC && toolId != ERASER) {
        emit penWidthChanged(ts.penWidth);
        LOG_INFO(QString("Emit line width signal: %1").arg(ts.penWidth));
        emit penColorChanged(ts.color);
        LOG_INFO(QString("Emit pen color signal: rgb(%1,%2,%3)")
            .arg(ts.color.red()).arg(ts.color.green()).arg(ts.color.blue()));
        if (newVariant == VariantRectangle) {
            // 矩形工具默认形状类型 1 = Rectangle
            emit shapeTypeChanged(1);
            updateShapeIcon(1);
            LOG_INFO("Emit shape type changed signal: 1 (Rectangle)");
        }
    } else {
        emit penWidthChanged(ts.penWidth);
        LOG_INFO(QString("Emit line width signal: %1").arg(ts.penWidth));
        // 根据工具类型发射特定信号到 SettingsWindow
        if (toolId == MOSAIC) {
            emit toolMosaicSizeChanged(ts.penWidth);
        } else if (toolId == ERASER) {
            emit toolEraserWidthChanged(ts.penWidth);
        }
    }

    LOG_INFO(QString("Sub-toolbar display completed. Active variant=%1, toolId=%2").arg(newVariant).arg(toolId));
}

/**
 * @brief 工具 ID → 子工具栏变体枚举映射
 * @param toolId 工具 ID
 * @return 对应的 SubToolVariant 枚举值
 * @author chiangyang
 */
BaseToolBar::SubToolVariant BaseToolBar::variantForToolId(int toolId) {
    switch (toolId) {
        case RECTANGLE: return VariantRectangle;
        case ELLIPSE:
        case ARROW:
        case PEN:
        case LINE:     return VariantColorPen;
        case TEXT:     return VariantText;
        case MOSAIC:
        case ERASER:   return VariantNoColor;
        default:       return VariantColorPen;
    }
}

/**
 * @brief 懒构建指定变体（已构建则直接返回）
 *
 * 构建统一的窗口标志、属性、样式，再按变体类型 switch 到内部控件：
 *   - VariantRectangle: ComboBox + 线宽滑块 + 颜色面板
 *   - VariantColorPen:  线宽滑块 + 颜色面板
 *   - VariantText:      字号滑块 + 颜色面板
 *   - VariantNoColor:   线宽滑块（不添加颜色面板）
 * 滑块/颜色 lambda 使用 m_currentToolId（运行时取值）而非构建时 toolId，
 * 这样同一个变体可被多个工具正确复用。
 *
 * 构建末尾：
 *   - 成员 m_penWidthSlider / m_colorBtns / m_selectedColorBtn 由 addColorPalette
 *     与 slider 创建过程写入。保存到 variant 结构后清空，让下一个变体构建从空白开始。
 * @param v 变体枚举
 * @author chiangyang
 */
void BaseToolBar::buildSubVariant(SubToolVariant v) {
    SubToolVariantData &data = m_subVariants[v];
    if (data.built) return;

    LOG_INFO(QString("Build sub-toolbar variant %1").arg(v));

    // ---------- 窗口外壳 ----------
    data.window = new QWidget(parentWidget());
    if (parentWidget() == nullptr) {
        // Pin 场景：独立顶层窗口 + 置顶 + 透明背景（圆角需要）
        data.window->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        data.window->setAttribute(Qt::WA_TranslucentBackground, true);
    } else {
        // 截图/录屏场景：父窗口内嵌子控件
        data.window->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        data.window->setAttribute(Qt::WA_TranslucentBackground, false);
    }
    data.window->setAttribute(Qt::WA_StyledBackground, true);
    data.window->setStyleSheet(StyleManager::getSubToolbarStyle());
    data.window->setCursor(Qt::ArrowCursor);
    data.window->installEventFilter(this);

    data.layout = new QHBoxLayout(data.window);
    data.layout->setContentsMargins(5, 2, 5, 2);
    data.layout->setSpacing(5);

    // 清空临时成员，让本次构建从空白开始
    m_penWidthSlider = nullptr;
    m_colorBtns.clear();
    m_selectedColorBtn = nullptr;

    // ---------- 按变体类型填充内部控件 ----------
    TranslationManager *tm = TranslationManager::instance();

    switch (v) {
        case VariantRectangle: {
            // --- 形状下拉框 ---
            data.shapeCombo = new QComboBox(data.window);
            data.shapeCombo->addItem(tm->get("shape.rect", "Rectangle"), 1);
            data.shapeCombo->addItem(tm->get("shape.ellipse", "Ellipse"), 2);
            data.shapeCombo->addItem(tm->get("shape.triangle", "Triangle"), 3);
            data.shapeCombo->setCurrentIndex(0);
            data.shapeCombo->setStyleSheet(StyleManager::getComboBoxStyle());
            connect(data.shapeCombo, &QComboBox::currentIndexChanged, [this, &data]() {
                int shapeType = data.shapeCombo->currentData().toInt();
                LOG_INFO(QString("Shape type changed: %1").arg(shapeType));
                emit shapeTypeChanged(shapeType);
                updateShapeIcon(shapeType);
            });
            data.layout->addWidget(data.shapeCombo);

            // --- 线宽滑块（lambda 用 m_currentToolId，不依赖构建时工具） ---
            data.slider = new QSlider(Qt::Horizontal, data.window);
            data.slider->setRange(1, 20);
            data.slider->setValue(StyleManager::getDefaultPenWidth());
            data.slider->setSingleStep(1);
            data.slider->setPageStep(1);
            data.slider->setTracking(true);
            data.slider->setTickPosition(QSlider::NoTicks);
            data.slider->setTickInterval(1);
            connect(data.slider, &QSlider::valueChanged, this, [this](int width) {
                if (m_currentToolId < 0) return;
                LOG_INFO(QString("Line width changed: %1").arg(width));
                m_toolSettings[m_currentToolId].penWidth = width;
                m_toolSettings[m_currentToolId].penWidthModified = true;
                emit penWidthChanged(width);
                emit toolPenWidthChanged(width);
            });
            data.slider->installEventFilter(this);
            data.layout->addWidget(data.slider);
            m_penWidthSlider = data.slider;

            // --- 颜色面板 ---
            addColorPalette(data.layout);
            break;
        }

        case VariantColorPen: {
            // --- 线宽滑块 ---
            data.slider = new QSlider(Qt::Horizontal, data.window);
            data.slider->setRange(1, 20);
            data.slider->setValue(StyleManager::getDefaultPenWidth());
            data.slider->setSingleStep(1);
            data.slider->setPageStep(1);
            data.slider->setTracking(true);
            data.slider->setTickPosition(QSlider::NoTicks);
            data.slider->setTickInterval(1);
            connect(data.slider, &QSlider::valueChanged, this, [this](int width) {
                if (m_currentToolId < 0) return;
                LOG_INFO(QString("Line width changed: %1").arg(width));
                m_toolSettings[m_currentToolId].penWidth = width;
                m_toolSettings[m_currentToolId].penWidthModified = true;
                emit penWidthChanged(width);
                emit toolPenWidthChanged(width);
            });
            data.slider->installEventFilter(this);
            data.layout->addWidget(data.slider);
            m_penWidthSlider = data.slider;

            // --- 颜色面板 ---
            addColorPalette(data.layout);
            break;
        }

        case VariantText: {
            // --- 字号滑块 ---
            data.slider = new QSlider(Qt::Horizontal, data.window);
            data.slider->setRange(8, 48);
            data.slider->setValue(StyleManager::getDefaultFontSize());
            data.slider->setSingleStep(1);
            data.slider->setPageStep(1);
            data.slider->setTracking(true);
            data.slider->setTickPosition(QSlider::NoTicks);
            data.slider->setTickInterval(1);
            connect(data.slider, &QSlider::valueChanged, this, [this](int size) {
                if (m_currentToolId < 0) return;
                LOG_INFO(QString("Font size changed: %1").arg(size));
                m_toolSettings[m_currentToolId].fontSize = size;
                m_toolSettings[m_currentToolId].fontSizeModified = true;
                emit fontSizeChanged(size);
                emit toolFontSizeChanged(size);
            });
            data.slider->installEventFilter(this);
            data.layout->addWidget(data.slider);
            // 文本工具 slider 不作为 m_penWidthSlider 暴露（它是字号滑块，快捷键处理独立）
            // 但保存到 m_penWidthSlider 以便统一释放：这里仅用于同步 setCurrentPenWidth 不用于 TEXT
            // 实际 setCurrentPenWidth 只在非 TEXT 工具调用，安全

            // --- 颜色面板 ---
            addColorPalette(data.layout);
            break;
        }

        case VariantNoColor: {
            // --- 仅线宽滑块（马赛克 / 橡皮擦，无颜色） ---
            data.slider = new QSlider(Qt::Horizontal, data.window);
            data.slider->setRange(1, 20);
            data.slider->setSingleStep(1);
            data.slider->setPageStep(1);
            data.slider->setTracking(true);
            data.slider->setTickPosition(QSlider::NoTicks);
            data.slider->setTickInterval(1);
            connect(data.slider, &QSlider::valueChanged, this, [this](int width) {
                if (m_currentToolId < 0) return;
                LOG_INFO(QString("Line width changed: %1").arg(width));
                m_toolSettings[m_currentToolId].penWidth = width;
                m_toolSettings[m_currentToolId].penWidthModified = true;
                emit penWidthChanged(width);
                // 根据当前工具类型发射特定信号
                if (m_currentToolId == MOSAIC) {
                    emit toolMosaicSizeChanged(width);
                } else if (m_currentToolId == ERASER) {
                    emit toolEraserWidthChanged(width);
                } else {
                    emit toolPenWidthChanged(width);
                }
            });
            data.slider->installEventFilter(this);
            data.layout->addWidget(data.slider);
            m_penWidthSlider = data.slider;

            // 不添加颜色面板
            break;
        }

        case VariantCount:
            // 占位分支，不应进入
            break;
    }

    // ---------- 保存本次构建写入的内部控件指针，清空临时成员 ----------
    data.colorBtns = m_colorBtns;
    data.selectedColorBtn = m_selectedColorBtn;
    m_colorBtns.clear();
    m_selectedColorBtn = nullptr;

    data.built = true;
    LOG_INFO(QString("Sub-toolbar variant %1 built OK").arg(v));
}

/**
 * @brief 将变体的内部控件指针绑定到当前成员变量
 *
 * 切换变体后调用。外部接口 setCurrentPenWidth / setCurrentColor / selectNextColor /
 * getSubToolbarWindow 等都是通过这些成员指针工作的，因此绑定后它们自动作用于新变体。
 *
 * @param v 变体枚举
 * @author chiangyang
 */
void BaseToolBar::bindVariantInternals(SubToolVariant v) {
    const SubToolVariantData &data = m_subVariants[v];
    subToolbarWindow       = data.window;
    subToolbarLayout       = data.layout;
    m_penWidthSlider       = data.slider;
    m_colorBtns            = data.colorBtns;
    m_selectedColorBtn     = data.selectedColorBtn;
}

/**
 * @brief 用指定工具的设置同步变体控件值
 *
 * 切换变体 / 切换工具时调用。将滑块值、颜色选中态、形状下拉框（若有）
 * 还原为 m_toolSettings[toolId] 中保存的设置，保证用户之前调整过的参数不丢失。
 * 此函数不发射信号，仅更新界面状态；信号由 showSubTools 在 layout 后统一发射。
 *
 * @param v 变体枚举
 * @param toolId 工具 ID
 * @author chiangyang
 */
void BaseToolBar::syncVariantState(SubToolVariant v, int toolId) {
    SubToolVariantData &data = m_subVariants[v];
    const ToolSettings &ts   = m_toolSettings[toolId];

    // 滑块
    if (data.slider) {
        data.slider->blockSignals(true);
        if (v == VariantText) {
            data.slider->setRange(8, 48);
            data.slider->setValue(ts.fontSize);
        } else if (v == VariantNoColor) {
            // VariantNoColor 共享给 MOSAIC 和 ERASER，范围统一 1-20
            data.slider->setRange(1, 20);
            data.slider->setValue(ts.penWidth);
        } else {
            data.slider->setValue(ts.penWidth);
        }
        data.slider->blockSignals(false);
    }

    // 颜色选中态（只对带颜色面板的变体）
    if (v != VariantNoColor) {
        // 遍历变体保存的所有颜色按钮，匹配颜色则高亮，否则普通样式
        // 复用 addColorPalette 中的 StyleManager::getColorButtonStyle(color.name(), selected) 接口
        QPushButton *newSelected = nullptr;
        for (QPushButton *btn : data.colorBtns) {
            QColor btnColor = btn->property("color").value<QColor>();
            if (btnColor.isValid() && btnColor == ts.color) {
                btn->setStyleSheet(StyleManager::getColorButtonStyle(btnColor.name(), true));
                newSelected = btn;
            } else {
                btn->setStyleSheet(StyleManager::getColorButtonStyle(btnColor.name(), false));
            }
        }
        data.selectedColorBtn = newSelected;
    }

    // 形状下拉框（只 Rectangle 变体）
    if (v == VariantRectangle && data.shapeCombo) {
        // 当前不持久化 shapeType 到 ToolSettings，恢复为默认矩形（index=0）
        // 如后续需要持久化，可在 ToolSettings 中新增字段
        data.shapeCombo->blockSignals(true);
        data.shapeCombo->setCurrentIndex(0);
        data.shapeCombo->blockSignals(false);
    }
}

// ============================================================
// 颜色面板
// ============================================================

/**
 * @brief 添加颜色选择面板
 * @param layout 布局对象
 *
 * 向指定布局添加颜色选择面板，包含多种颜色选项
 * @author chiangyang
 */
void BaseToolBar::addColorPalette(QLayout *layout) {
    LOG_INFO("Start adding color selection palette");
    QWidget *colorPalette = new QWidget(this);
    QHBoxLayout *paletteLayout = new QHBoxLayout(colorPalette);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    paletteLayout->setSpacing(2);

    QVector<QColor> colors = {
        Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::magenta,
        Qt::black, Qt::white, Qt::gray, Qt::darkRed, Qt::darkGreen, Qt::darkBlue
    };

    // 获取当前工具的颜色设置
    QColor currentColor = m_toolSettings[m_currentToolId].color;

    for (const QColor &color : colors) {
        QPushButton *colorBtn = new QPushButton(this);
        // 尺寸由全局 qss 的 QPushButton#toolbarColorButton 接管（1.5em 正方形，随 DPI 缩放）
        colorBtn->setObjectName("toolbarColorButton");
        colorBtn->setStyleSheet(StyleManager::getColorButtonStyle(color.name(), false));
        // 记录每个按钮自身的颜色，供 setCurrentColor（Tab 切色）读取以同步选中态；
        // 此前仅选中按钮设置该 property，导致 setCurrentColor 读到空颜色（QColor("") 无效，
        // .name() 返回 "#000000"），未选中按钮全部变黑
        colorBtn->setProperty("color", color.name());
        connect(colorBtn, &QPushButton::clicked, [this, color, colorBtn]() {
            LOG_INFO(QString("Color selected: %1").arg(color.name()));
            // 保存到当前工具的设置
            m_toolSettings[m_currentToolId].color = color;
            // 重置之前选中按钮的样式
            if (m_selectedColorBtn) {
                m_selectedColorBtn->setStyleSheet(
                    StyleManager::getColorButtonStyle(m_selectedColorBtn->property("color").toString(), false));
            }
            // 将当前按钮设置为选中状态
            colorBtn->setStyleSheet(StyleManager::getColorButtonStyle(color.name(), true));
            colorBtn->setProperty("color", color.name());
            m_selectedColorBtn = colorBtn;
            emit penColorChanged(color);
        });
        // 默认选择当前工具的颜色
        if (color == currentColor) {
            colorBtn->setStyleSheet(StyleManager::getColorButtonStyle(color.name(), true));
            colorBtn->setProperty("color", color.name());
            m_selectedColorBtn = colorBtn;
            LOG_INFO(QString("Default selected color for tool ID=%1: %2").arg(m_currentToolId).arg(currentColor.name()));
        }
        paletteLayout->addWidget(colorBtn);
        // 收集颜色按钮，供 setCurrentColor 同步选中态
        m_colorBtns.append(colorBtn);
    }

    layout->addWidget(colorPalette);
    // 发出当前工具的颜色信号
    emit penColorChanged(currentColor);
    LOG_INFO(QString("Emit color signal for tool ID=%1: %2").arg(m_currentToolId).arg(currentColor.name()));
    LOG_INFO("Color selection palette added completed");
}

// ============================================================
// 标注工具按钮组（上提到基类，两个子类共享）
// ============================================================

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
                subToolbarLayout = nullptr;
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

// ============================================================
// 分隔线
// ============================================================

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

// ============================================================
// 按钮文字/图标样式（共享逻辑）
// ============================================================

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

// ============================================================
// 标注按钮翻译
// ============================================================

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

// ============================================================
// 画笔宽度/颜色外部设置（供快捷键调用）
// ============================================================

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

// ============================================================
// 形状图标更新
// ============================================================

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

// ============================================================
// 按钮状态更新
// ============================================================

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

// ============================================================
// 取消所有标注按钮选中
// ============================================================

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

// ============================================================
// 辅助方法
// ============================================================

/**
 * @brief 获取主布局
 * @return 主布局对象
 * @author chiangyang
 */
QVBoxLayout *BaseToolBar::getMainLayout() const {
    return m_mainLayout;
}

/**
 * @brief 获取子工具栏窗口
 * @return 子工具栏窗口指针
 * @author chiangyang
 */
QWidget *BaseToolBar::getSubToolbarWindow() const {
    return subToolbarWindow;
}

/**
 * @brief 更新背景样式
 * @author chiangyang
 */
void BaseToolBar::updateBackgroundStyle() {
    // 更新子工具栏背景样式
    if (subToolbarWindow) {
        subToolbarWindow->setStyleSheet(StyleManager::getSubToolbarStyle());
        subToolbarWindow->update();
    }
}

// ============================================================
// 事件过滤器（滑块点击定位）
// ============================================================

/**
 * @brief 事件过滤器
 * @param watched 被监视的对象
 * @param event 事件
 * @return 是否处理了事件
 *
 * 实现滑块点击时直接跳转到点击位置，而不是仅移动一个步长
 * @author chiangyang
 */
bool BaseToolBar::eventFilter(QObject *watched, QEvent *event) {
    // 子工具栏（仅 Pin 场景为透明顶层窗口）：QSS 的 background-color 不被绘制，
    // 需在 paint 事件中用 QPainter 主动绘制圆角矩形背景，圆角外区域保持透明。
    // 截图/录屏场景下 subToolbarWindow 非透明，WA_StyledBackground 会正常绘制 QSS 背景，
    // 此处绘制会覆盖在其上但效果一致，返回 false 不影响子控件正常绘制。
    if (watched == subToolbarWindow && event->type() == QEvent::Paint) {
        QWidget *w = qobject_cast<QWidget*>(watched);
        if (w) {
            QPainter p(w);
            p.setRenderHint(QPainter::Antialiasing);
            // 圆角半径与 QSS border-radius: 0.3em 保持一致
            const qreal radius = 0.3 * w->fontMetrics().horizontalAdvance(QLatin1Char('M'));
            p.setPen(Qt::NoPen);
            p.setBrush(StyleManager::getSubToolbarBgColor());
            p.drawRoundedRect(w->rect(), radius, radius);
        }
        return false;
    }

    QSlider *slider = qobject_cast<QSlider*>(watched);
    if (slider && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (slider->orientation() == Qt::Horizontal) {
            // 计算鼠标点击位置对应的滑块值
            int x = mouseEvent->pos().x();
            int w = slider->width();
            int value;
            if (x <= 0) {
                value = slider->minimum();
            } else if (x >= w) {
                value = slider->maximum();
            } else {
                value = slider->minimum() + ((slider->maximum() - slider->minimum()) * x) / w;
            }
            LOG_INFO(QString("Horizontal slider clicked, position: %1, width: %2, set value: %3").arg(x).arg(w).arg(value));
            slider->setValue(value);
            // 不要返回true，让事件继续传播，这样滑块仍然可以被拖动
            return false;
        } else {
            // 处理垂直滑块
            int y = mouseEvent->pos().y();
            int h = slider->height();
            int value;
            if (y <= 0) {
                value = slider->maximum();
            } else if (y >= h) {
                value = slider->minimum();
            } else {
                value = slider->maximum() - ((slider->maximum() - slider->minimum()) * y) / h;
            }
            LOG_INFO(QString("Vertical slider clicked, position: %1, height: %2, set value: %3").arg(y).arg(h).arg(value));
            slider->setValue(value);
            // 不要返回true，让事件继续传播，这样滑块仍然可以被拖动
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}


/**
 * @brief 根据配置刷新工具默认值
 *
 * 从 StyleManager 读取当前默认画笔粗细和字号，
 * 更新所有工具的默认值设置，并同步当前可见的滑块值。
 * 当用户在设置页面修改默认值时调用。
 * @author chiangyang
 */
void BaseToolBar::refreshDefaultValues() {
    int defaultPenWidth = StyleManager::getDefaultPenWidth();
    int defaultFontSize = StyleManager::getDefaultFontSize();
    int defaultEraserWidth = StyleManager::getDefaultEraserWidth();
    int defaultMosaicSize = StyleManager::getDefaultMosaicSize();

    LOG_INFO(QString("Refreshing default values: penWidth=%1, fontSize=%2, eraserWidth=%3, mosaicSize=%4")
        .arg(defaultPenWidth).arg(defaultFontSize).arg(defaultEraserWidth).arg(defaultMosaicSize));

    // 更新所有工具的默认值设置
    for (auto it = m_toolSettings.begin(); it != m_toolSettings.end(); ++it) {
        ToolSettings &ts = it.value();
        int toolId = it.key();
        if (toolId == MOSAIC) {
            ts.penWidth = defaultMosaicSize;
        } else if (toolId == ERASER) {
            ts.penWidth = defaultEraserWidth;
        } else {
            ts.penWidth = defaultPenWidth;
        }
        ts.fontSize = defaultFontSize;
        ts.penWidthModified = false;
        ts.fontSizeModified = false;
    }

    // 同步当前可见的滑块值（总是更新）
    if (m_currentToolId >= 0 && m_penWidthSlider) {
        SubToolVariant v = variantForToolId(m_currentToolId);
        if (v == VariantText) {
            // 文本工具同步字号滑块
            m_penWidthSlider->blockSignals(true);
            m_penWidthSlider->setValue(defaultFontSize);
            m_penWidthSlider->blockSignals(false);
        } else if (v == VariantNoColor) {
            // 马赛克或橡皮擦工具同步对应默认值
            int value = (m_currentToolId == MOSAIC) ? defaultMosaicSize : defaultEraserWidth;
            m_penWidthSlider->blockSignals(true);
            m_penWidthSlider->setValue(value);
            m_penWidthSlider->blockSignals(false);
        } else {
            // 其他工具同步线宽滑块
            m_penWidthSlider->blockSignals(true);
            m_penWidthSlider->setValue(defaultPenWidth);
            m_penWidthSlider->blockSignals(false);
        }
    }
}
