#include "Selector.h"
#include "../log/Logger.h"
#include <QApplication>
#include <QMouseEvent>
#include <QShortcut>

/**
 * @brief 构造函数
 * @param detector 猎物检测器实例
 * @param parent 父窗口
 * @author chiangyang
 */
Selector::Selector(IPreyDetector *detector, QWidget *parent)
    : QWidget(parent)
    , m_detector(detector)
{
    LOG_INFO("Selector instance created");

    // 尺寸信息标签：显示选区的宽高和目标名称
    m_infoLabel = new QLabel(this);
    m_infoLabel->setMinimumSize(120, 28);
    m_infoLabel->setAlignment(Qt::AlignCenter);
    m_infoLabel->setObjectName("size_info");
    m_infoLabel->setStyleSheet(StyleManager::getSnipInfoLabelStyle());
    m_infoLabel->setVisible(false);

    // 录制时间标签：显示在尺寸信息标签上方
    m_timerLabel = new QLabel(this);
    m_timerLabel->setMinimumSize(80, 28);
    m_timerLabel->setAlignment(Qt::AlignCenter);
    m_timerLabel->setStyleSheet(StyleManager::getRecordTimerLabelStyle());
    m_timerLabel->setVisible(false);

    connect(this, &Selector::moved, [this]() { update(); });
    connect(this, &Selector::resized, [this]() { update(); });

    registerShortcuts();
    setStatus(SelectorStatus::Ready);
}

// ============================================================
// 状态管理
// ============================================================

/**
 * @brief 设置选区状态
 * @param status 新状态
 * @author chiangyang
 */
void Selector::setStatus(SelectorStatus status) {
    if (m_status == status)
        return;

    LOG_INFO(QString("Selector status changed: %1 -> %2")
        .arg(static_cast<int>(m_status))
        .arg(static_cast<int>(status)));

    m_status = status;

    // Locked 状态不再穿透鼠标事件
    // Selector 自身的 mousePressEvent 已通过 m_status != Locked 检查忽略鼠标操作
    setAttribute(Qt::WA_TransparentForMouseEvents, false);

    switch (status) {
    case SelectorStatus::PreySelecting:
    case SelectorStatus::FreeSelecting:
        emit selecting();
        break;
    case SelectorStatus::Captured:
        update();
        emit captured();
        break;
    case SelectorStatus::Locked:
        emit locked();
        break;
    default:
        break;
    }

    emit statusChanged(m_status);
}

// ============================================================
// 选区查询
// ============================================================

/**
 * @brief 获取当前选区矩形
 * @param relative true 返回相对坐标，false 返回全局坐标
 * @return 选区矩形
 * @author chiangyang
 */
QRect Selector::selected(bool relative) const {
    if (relative)
        return m_box.rect().translated(-m_box.range().topLeft());
    return m_box.rect();
}

// ============================================================
// 选区设置
// ============================================================

/**
 * @brief 选中指定猎物区域
 * @param prey 猎物对象
 * @author chiangyang
 */
void Selector::select(const Prey &prey) {
    m_box.coords(prey.geometry);
    m_prey = prey;
    LOG_INFO(QString("Selector selected prey: type=%1, name=%2, rect=(%3,%4,%5,%6)")
        .arg(static_cast<int>(prey.type))
        .arg(prey.name)
        .arg(prey.geometry.left())
        .arg(prey.geometry.top())
        .arg(prey.geometry.width())
        .arg(prey.geometry.height()));
    update();
}

/**
 * @brief 选中指定矩形区域
 * @param rect 矩形区域
 * @author chiangyang
 */
void Selector::select(const QRect &rect) {
    m_box.coords(rect);
    m_prey = Prey::from(rect);
    LOG_INFO(QString("Selector selected rect: (%1,%2,%3,%4)")
        .arg(rect.left())
        .arg(rect.top())
        .arg(rect.width())
        .arg(rect.height()));
    update();
}

// ============================================================
// 开始选区
// ============================================================

/**
 * @brief 开始选区交互
 * @author chiangyang
 */
void Selector::start() {
    if (m_status != SelectorStatus::Ready)
        return;

    LOG_INFO("Selector start: begin selection");

    setMouseTracking(true);

    // 选区范围 = 父窗口坐标系（macOS 单屏模式下为当前屏，其他平台为虚拟桌面）
    // m_coordinate 由 SnipScreen::startCapture 通过 setCoordinate() 设置
    const QRect virtualGeometry = m_coordinate;
    m_box.range(virtualGeometry);

    // 枚举窗口和显示器
    if (m_detector) {
        m_detector->ready();
    }

    // 初始吸附到鼠标下的目标
    if (m_detector) {
        select(m_detector->hunt(QCursor::pos()));
    }
    m_infoLabel->show();

    setStatus(SelectorStatus::PreySelecting);
}

// ============================================================
// 鼠标事件
// ============================================================

/**
 * @brief 鼠标按下事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void Selector::mousePressEvent(QMouseEvent *event) {
    auto pos = event->globalPosition().toPoint();

    if (event->button() == Qt::LeftButton && m_status != SelectorStatus::Locked) {
        m_cursorPos = m_box.absolutePos(pos);

        switch (m_status) {
        case SelectorStatus::PreySelecting: {
            // 如果 scope 是 Display，限制选区到当前显示器
            if (m_scope == SelectionScope::Display) {
                auto display = DisplayInfo::displayContains(pos);
                if (display.has_value())
                    m_box.range(display->geometry);
            }

            m_dragStart = pos;
            m_isDragging = true;
            break;
        }

        case SelectorStatus::Captured:
            if (m_cursorPos == ResizerLocation::EMPTY_INSIDE) {
                // 点击选区内部 -> 开始移动
                m_moveStart = m_moveEnd = pos;
                setStatus(SelectorStatus::Moving);
            } else if (any(m_cursorPos & ResizerLocation::ADJUST_AREA)) {
                // 点击锚点/边框 -> 开始调整大小
                setStatus(SelectorStatus::Resizing);
            }
            break;

        default:
            break;
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_status == SelectorStatus::Locked) return;
        switch (m_status) {
        case SelectorStatus::PreySelecting:
            // 右键取消
            emit stopped();
            break;
        case SelectorStatus::Captured:
            // 右键重新选择
            if (m_detector) {
                select(m_detector->hunt(QCursor::pos()));
            }
            // 重置 scope 范围
            if (m_scope == SelectionScope::Display)
                m_box.range(m_coordinate);
            setStatus(SelectorStatus::PreySelecting);
            break;
        default:
            break;
        }
    }
}

/**
 * @brief 鼠标移动事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void Selector::mouseMoveEvent(QMouseEvent *event) {
    auto mousePos = event->globalPosition().toPoint();

    switch (m_status) {
    case SelectorStatus::PreySelecting:
        if (m_isDragging) {
            // 按下后移动超过 4px 才进入自由选区
            if (std::abs(mousePos.x() - m_dragStart.x()) >= 4 &&
                std::abs(mousePos.y() - m_dragStart.y()) >= 4) {
                select({m_dragStart, mousePos});
                m_infoLabel->show();
                m_isDragging = false;
                setStatus(SelectorStatus::FreeSelecting);
            }
        } else {
            // 自动吸附到鼠标下的目标
            if (m_detector) {
                select(m_detector->hunt(mousePos));
            }
            setCursor(Qt::CrossCursor);
        }
        break;

    case SelectorStatus::FreeSelecting:
        // 扩展选区到鼠标位置
        adjustSelection(0, 0, mousePos.x() - m_box.x2(), mousePos.y() - m_box.y2());
        break;

    case SelectorStatus::Captured:
    case SelectorStatus::Locked:
        // 标注模式下：选区内外不同光标；非标注模式下：根据位置显示光标
        if (m_isAnnotationMode) {
            setCursor(m_box.contains(mousePos) ? Qt::CrossCursor : Qt::ArrowCursor);
        } else if (m_status == SelectorStatus::Captured) {
            setCursor(getCursorByLocation(m_box.relativePos(mousePos)));
        }
        break;

    case SelectorStatus::Moving: {
        // 移动选区
        m_moveEnd = mousePos;
        translateSelection(m_moveEnd.x() - m_moveStart.x(), m_moveEnd.y() - m_moveStart.y());
        m_moveStart = mousePos;
        setCursor(Qt::SizeAllCursor);
        break;
    }

    case SelectorStatus::Resizing: {
        // 根据按下时的位置类型调整大小
        switch (m_cursorPos) {
        case ResizerLocation::Y1_BORDER: case ResizerLocation::Y1_ANCHOR:
            adjustSelection(0, mousePos.y() - m_box.y1(), 0, 0); break;
        case ResizerLocation::Y2_BORDER: case ResizerLocation::Y2_ANCHOR:
            adjustSelection(0, 0, 0, mousePos.y() - m_box.y2()); break;
        case ResizerLocation::X1_BORDER: case ResizerLocation::X1_ANCHOR:
            adjustSelection(mousePos.x() - m_box.x1(), 0, 0, 0); break;
        case ResizerLocation::X2_BORDER: case ResizerLocation::X2_ANCHOR:
            adjustSelection(0, 0, mousePos.x() - m_box.x2(), 0); break;
        case ResizerLocation::X1Y1_ANCHOR:
            adjustSelection(mousePos.x() - m_box.x1(), mousePos.y() - m_box.y1(), 0, 0); break;
        case ResizerLocation::X1Y2_ANCHOR:
            adjustSelection(mousePos.x() - m_box.x1(), 0, 0, mousePos.y() - m_box.y2()); break;
        case ResizerLocation::X2Y1_ANCHOR:
            adjustSelection(0, mousePos.y() - m_box.y1(), mousePos.x() - m_box.x2(), 0); break;
        case ResizerLocation::X2Y2_ANCHOR:
            adjustSelection(0, 0, mousePos.x() - m_box.x2(), mousePos.y() - m_box.y2()); break;
        default: break;
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @brief 鼠标释放事件处理
 * @param event 鼠标事件
 * @author chiangyang
 */
void Selector::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        switch (m_status) {
        case SelectorStatus::PreySelecting:
            if (m_isDragging) {
                // 吸附状态下释放 -> 完成选区
                m_isDragging = false;
                setStatus(SelectorStatus::Captured);
            }
            break;
        case SelectorStatus::FreeSelecting:
        case SelectorStatus::Moving:
        case SelectorStatus::Resizing:
            // 选区太小则回退到吸附
            if (isInvalid() && m_detector)
                select(m_detector->hunt(QCursor::pos()));
            setStatus(SelectorStatus::Captured);
            break;
        default:
            break;
        }
    }
}

// ============================================================
// 滚轮事件：切换目标层级
// ============================================================

/**
 * @brief 滚轮事件，切换目标层级（更大/更小）
 * @param event 滚轮事件
 * @author chiangyang
 */
void Selector::wheelEvent(QWheelEvent *event) {
    if (m_status == SelectorStatus::PreySelecting && m_detector) {
        if (event->angleDelta().y() > 0)
            select(m_detector->contains(m_prey));   // 向上：更大
        else
            select(m_detector->contained(m_prey, QCursor::pos())); // 向下：更小
    }
    QWidget::wheelEvent(event);
}

// ============================================================
// 关闭事件：重置状态
// ============================================================

/**
 * @brief 关闭事件，重置状态
 * @param event 关闭事件
 * @author chiangyang
 */
void Selector::closeEvent(QCloseEvent *event) {
    setStatus(SelectorStatus::Ready);
    setMouseTracking(false);
    m_infoLabel->hide();

    m_box.coords(m_box.range());
    m_prey = {};
    m_isDragging = false;
    m_isAnnotationMode = false;

    // 不在此处调用 repaint()：widget 即将关闭，重绘无意义；
    // 且 macOS 下关闭流程中 repaint() 会同步触发 paintEvent，第一次 painter
    // 析构 flush 时处理排队的异步重绘（第二次 paintEvent），此时 backing store
    // 已失效，第二次 painter 析构 flush 导致闪退（仅 Mac，Windows 正常）。
    if (m_detector) {
        m_detector->clear();
    }
    QWidget::closeEvent(event);
}

// ============================================================
// 绘制
// ============================================================

/**
 * @brief 绘制事件：遮罩、边框、锚点、十字准线
 * @param event 绘制事件
 * @author chiangyang
 */
void Selector::paintEvent(QPaintEvent *) {
    QPainter painter(this);

    // 设置坐标系：如果父窗口不在 (0,0)，需要平移
    if (m_coordinate != QRect{})
        painter.setWindow(m_coordinate);

    const QRect srect = selected();

    // 绘制十字准线
    if (m_crossHair && m_status < SelectorStatus::Captured) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        const auto pos = QCursor::pos();
        painter.drawLine(0, pos.y(), width(), pos.y());
        painter.drawLine(pos.x(), 0, pos.x(), height());
        // 短十字线段
        painter.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(pos.x() - 30, pos.y(), pos.x() - 10, pos.y());
        painter.drawLine(pos.x() + 10, pos.y(), pos.x() + 30, pos.y());
        painter.drawLine(pos.x(), pos.y() - 30, pos.x(), pos.y() - 10);
        painter.drawLine(pos.x(), pos.y() + 10, pos.x(), pos.y() + 30);
        painter.restore();
    }

    // 绘制半透明遮罩（选区外区域变暗）
    painter.save();
    painter.setPen(m_maskColor);
    painter.setBrush(m_maskColor);
    painter.setClipping(true);
    painter.setClipRegion(QRegion(painter.window()).subtracted(QRegion(srect)));
    painter.drawRect(painter.window());
    painter.setClipping(false);
    painter.restore();

    if (m_status > SelectorStatus::Ready) {
        // 更新尺寸信息标签
        updateInfoLabel();

        // 绘制选区边框
        painter.setPen(m_borderPen);
        painter.setBrush(QColor(0, 0, 0, 1));
        painter.drawRect(srect.adjusted(-m_borderPen.width() % 2, -m_borderPen.width() % 2, 0, 0));

        // 绘制锚点（选区足够大时显示全部 8 个，否则只显示 4 个角）
        painter.setPen({m_borderPen.color(), m_borderPen.widthF(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.setBrush(Qt::white);
        painter.drawRects((srect.width() >= 32 && srect.height() >= 32)
                              ? m_box.anchors()
                              : m_box.cornerAnchors());
    }
}

// ============================================================
// 尺寸信息标签
// ============================================================

/**
 * @brief 更新尺寸信息标签内容和位置
 * @author chiangyang
 */
void Selector::updateInfoLabel() {
    QString text = QStringLiteral("%1 x %2").arg(selected().width()).arg(selected().height());

    // 添加目标名称
    if (m_prey.type == PreyType::Display && !m_prey.codename.isEmpty())
        text += QStringLiteral(" : %1 - %2").arg(m_prey.codename, m_prey.name);
    else if (m_prey.type == PreyType::Window && !m_prey.name.isEmpty())
        text += QStringLiteral(" : %1").arg(m_prey.name);

    m_infoLabel->setText(text);
    m_infoLabel->adjustSize();

    // 定位到选区上方
    int infoY = m_box.top() - m_infoLabel->height() - 1;
    QPoint pos(m_box.left() + 1, (infoY < 0 ? m_box.top() + 1 : infoY - 1));
    m_infoLabel->move(pos - m_coordinate.topLeft());

    // 更新录制时间标签位置
    updateTimerLabel();
}

// ============================================================
// 切换屏幕（macOS 单屏窗口模式）
// ============================================================

/**
 * @brief 切换到新屏幕，更新坐标系和选区范围，重新吸附目标
 * @param screenGeometry 新屏幕的全局几何
 * @author chiangyang
 */
void Selector::switchScreen(const QRect &screenGeometry) {
    m_coordinate = screenGeometry;
    m_box.range(screenGeometry);
    // 重新吸附到鼠标下的目标（新屏幕上的窗口/显示器）
    if (m_detector) {
        select(m_detector->hunt(QCursor::pos()));
    }
    updateInfoLabel();
}

/**
 * @brief 更新录制时间标签位置
 *
 * 根据 infoLabel 的位置决定 timerLabel 放在上方还是下方：
 * - 正常情况（infoLabel 在选区上方有空间）：timerLabel 在 infoLabel 上方
 * - 空间不足（infoLabel 贴在选区上边缘）：timerLabel 在 infoLabel 下方
 * @author chiangyang
 */
void Selector::updateTimerLabel() {
    if (!m_timerLabel->isVisible())
        return;

    m_timerLabel->adjustSize();

    // 判断 infoLabel 是否贴在选区上边缘（上方空间不足）
    int selectionTopInWidget = m_box.top() - m_coordinate.topLeft().y();
    bool infoLabelAtTop = (m_infoLabel->y() <= selectionTopInWidget + 2);

    int timerY;
    if (infoLabelAtTop) {
        // 空间不足，timerLabel 放在 infoLabel 下方
        timerY = m_infoLabel->y() + m_infoLabel->height() + 2;
    } else {
        // 正常情况，timerLabel 放在 infoLabel 上方
        timerY = m_infoLabel->y() - m_timerLabel->height() - 2;
    }
    m_timerLabel->move(m_infoLabel->x(), timerY);
}

/**
 * @brief 显示录制时间标签
 * @author chiangyang
 */
void Selector::showTimerLabel() {
    LOG_INFO("Selector: show timer label");
    m_timerLabel->setText("00:00");
    m_timerLabel->adjustSize();
    m_timerLabel->setVisible(true);
    updateTimerLabel();
}

/**
 * @brief 更新录制时间标签文本
 * @param text 时间文本
 * @author chiangyang
 */
void Selector::updateTimerText(const QString &text) {
    m_timerLabel->setText(text);
    updateTimerLabel();
}

/**
 * @brief 隐藏录制时间标签
 * @author chiangyang
 */
void Selector::hideTimerLabel() {
    LOG_INFO("Selector: hide timer label");
    m_timerLabel->setVisible(false);
}

/**
 * @brief 获取信息标签和时间标签的独立区域（用于遮罩计算）
 * @return 各标签独立矩形的区域（不合并，保持各自宽度）
 * @author chiangyang
 */
QRegion Selector::getLabelGeometry() const {
    QRegion region;
    if (m_infoLabel && m_infoLabel->isVisible()) {
        region += m_infoLabel->geometry();
    }
    if (m_timerLabel && m_timerLabel->isVisible()) {
        region += m_timerLabel->geometry();
    }
    return region;
}

// ============================================================
// 选区变换
// ============================================================

/**
 * @brief 平移选区
 * @param dx X 方向偏移
 * @param dy Y 方向偏移
 * @author chiangyang
 */
void Selector::translateSelection(int dx, int dy) {
    if (m_status != SelectorStatus::Captured && m_status != SelectorStatus::Moving)
        return;

    m_box.translate(dx, dy);
    m_prey = Prey::from(m_box.rect());
    emit moved();
}

/**
 * @brief 调整选区大小
 * @param dx1 左边界偏移
 * @param dy1 上边界偏移
 * @param dx2 右边界偏移
 * @param dy2 下边界偏移
 * @author chiangyang
 */
void Selector::adjustSelection(int dx1, int dy1, int dx2, int dy2) {
    if (m_status != SelectorStatus::Captured && m_status != SelectorStatus::Resizing &&
        m_status != SelectorStatus::FreeSelecting)
        return;

    m_box.adjust(dx1, dy1, dx2, dy2);
    m_prey = Prey::from(m_box.rect());
    emit resized();
}

/**
 * @brief 通过边距调整选区大小
 * @param dt 上边距
 * @param dr 右边距
 * @param db 下边距
 * @param dl 左边距
 * @author chiangyang
 */
void Selector::marginsSelection(int dt, int dr, int db, int dl) {
    if (m_status != SelectorStatus::Captured && m_status != SelectorStatus::Resizing)
        return;

    m_box.margins(dt, dr, db, dl);
    m_prey = Prey::from(m_box.rect());
    emit resized();
}

// ============================================================
// 快捷键
// ============================================================

/**
 * @brief 注册选区快捷键（WASD/方向键移动，Ctrl/Shift 调整大小，Ctrl+A 全屏）
 * @author chiangyang
 */
void Selector::registerShortcuts() {
    // WASD / 方向键：像素级移动选区
    auto moveUp    = [this]() { translateSelection(0, -1); };
    auto moveDown  = [this]() { translateSelection(0, +1); };
    auto moveLeft  = [this]() { translateSelection(-1, 0); };
    auto moveRight = [this]() { translateSelection(+1, 0); };

    connect(new QShortcut(Qt::Key_W, this), &QShortcut::activated, moveUp);
    connect(new QShortcut(Qt::Key_Up, this), &QShortcut::activated, moveUp);
    connect(new QShortcut(Qt::Key_S, this), &QShortcut::activated, moveDown);
    connect(new QShortcut(Qt::Key_Down, this), &QShortcut::activated, moveDown);
    connect(new QShortcut(Qt::Key_A, this), &QShortcut::activated, moveLeft);
    connect(new QShortcut(Qt::Key_Left, this), &QShortcut::activated, moveLeft);
    connect(new QShortcut(Qt::Key_D, this), &QShortcut::activated, moveRight);
    connect(new QShortcut(Qt::Key_Right, this), &QShortcut::activated, moveRight);

    // Ctrl + 方向键：像素级扩大
    connect(new QShortcut(Qt::CTRL | Qt::Key_Up, this), &QShortcut::activated,
            [this]() { marginsSelection(-1, 0, 0, 0); });
    connect(new QShortcut(Qt::CTRL | Qt::Key_Down, this), &QShortcut::activated,
            [this]() { marginsSelection(0, 0, +1, 0); });
    connect(new QShortcut(Qt::CTRL | Qt::Key_Left, this), &QShortcut::activated,
            [this]() { marginsSelection(0, 0, 0, -1); });
    connect(new QShortcut(Qt::CTRL | Qt::Key_Right, this), &QShortcut::activated,
            [this]() { marginsSelection(0, +1, 0, 0); });

    // Shift + 方向键：像素级缩小
    connect(new QShortcut(Qt::SHIFT | Qt::Key_Up, this), &QShortcut::activated,
            [this]() { marginsSelection(+1, 0, 0, 0); });
    connect(new QShortcut(Qt::SHIFT | Qt::Key_Down, this), &QShortcut::activated,
            [this]() { marginsSelection(0, 0, -1, 0); });
    connect(new QShortcut(Qt::SHIFT | Qt::Key_Left, this), &QShortcut::activated,
            [this]() { marginsSelection(0, 0, 0, +1); });
    connect(new QShortcut(Qt::SHIFT | Qt::Key_Right, this), &QShortcut::activated,
            [this]() { marginsSelection(0, -1, 0, 0); });

    // Ctrl+A：全屏（先显示器，再桌面）
    connect(new QShortcut(Qt::CTRL | Qt::Key_A, this), &QShortcut::activated, [this]() {
        if (m_status > SelectorStatus::Captured)
            return;

        setStatus(SelectorStatus::PreySelecting);

        // 如果当前是窗口/矩形，先升级到所在显示器
        if (m_prey.type < PreyType::Display) {
            for (const auto &display : DisplayInfo::displays()) {
                if (display.geometry.contains(m_box.rect())) {
                    select(Prey::from(display));
                    setStatus(SelectorStatus::Captured);
                    return;
                }
            }
        }

        // 如果已经是显示器或更小，升级到整个桌面
        if (m_prey.type <= PreyType::Display && m_scope == SelectionScope::Desktop) {
            Prey desktop;
            desktop.type = PreyType::Desktop;
            // macOS 单屏模式下 m_coordinate = 当前屏 geometry，
            // 其他平台 m_coordinate = 虚拟桌面 geometry
            desktop.geometry = m_coordinate;
            desktop.name = QStringLiteral("Desktop");
            select(desktop);
            setStatus(SelectorStatus::Captured);
        }
    });
}
