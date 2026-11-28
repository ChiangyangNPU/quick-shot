#include "ColorPickerOverlay.h"

#include "../core/DisplayInfo.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../utils/Utils.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

#ifdef Q_OS_MACOS
#include <objc/objc.h>
#include <objc/message.h>

/**
 * @brief 将窗口级别设到菜单栏之上，使取色窗口能覆盖菜单栏区域
 *
 * 与 SnipScreen.cpp 的 raiseWindowAboveMenuBar 相同实现：
 * Qt::WindowStaysOnTopHint 对应 NSFloatingWindowLevel(3)，低于
 * NSMainMenuWindowLevel(24)，覆盖窗口无法接收菜单栏区域的鼠标事件。
 * @param wid QWidget::winId() 返回的 NSView 指针
 * @author chiangyang
 */
static void raisePickerWindowAboveMenuBar(WId wid) {
    if (!wid) return;
    id nsView = reinterpret_cast<id>(wid);
    // [nsView window]
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    // [nsWindow setLevel:NSPopUpMenuWindowLevel]
    ((void (*)(id, SEL, long))objc_msgSend)(nsWindow, sel_registerName("setLevel:"), 101);
}
#endif

namespace {

/**
 * @brief 抓取取色快照及其全局几何
 *
 * macOS 抓鼠标所在屏（含菜单栏几何修正，约定同 SnipScreen::grabVirtualDesktop）；
 * Windows 抓整个虚拟桌面（Win32 BitBlt，物理像素 dpr=1）；
 * 其他平台抓主屏。必须在覆盖窗口显示之前调用（避免把自身抓进快照）。
 * @param outGeometry 输出快照覆盖的全局几何
 * @param outDpr 输出快照的设备像素比
 * @return 快照（已设置 devicePixelRatio），失败返回空 QPixmap
 * @author chiangyang
 */
QPixmap grabColorPickerSnapshot(QRect &outGeometry, qreal &outDpr) {
#ifdef Q_OS_MACOS
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    QPixmap pix = screen->grabWindow(0);
    const qreal dpr = screen->devicePixelRatio();
    pix.setDevicePixelRatio(dpr);

    // QScreen::geometry() 对主屏可能不含菜单栏（y 从菜单栏高度开始），
    // 用背景逻辑尺寸修正几何，确保窗口覆盖含菜单栏的整个屏幕
    const QSize fullSize = pix.size() / dpr;
    const QRect g = screen->geometry();
    if (g.height() < fullSize.height()) {
        const int yOffset = fullSize.height() - g.height();
        outGeometry = QRect(g.x(), g.y() - yOffset, fullSize.width(), fullSize.height());
    } else {
        outGeometry = QRect(g.x(), g.y(), fullSize.width(), fullSize.height());
    }
    outDpr = dpr;
    return pix;
#elif defined(Q_OS_WIN)
    outGeometry = DisplayInfo::virtualScreenGeometry();
    outDpr = 1.0;
    return Utils::grabVirtualDesktopPixmap();
#else
    QScreen *screen = QGuiApplication::primaryScreen();
    QPixmap pix = screen->grabWindow(0);
    outDpr = screen->devicePixelRatio();
    if (outDpr <= 0) outDpr = 1.0;
    pix.setDevicePixelRatio(outDpr);
    outGeometry = screen->geometry();
    return pix;
#endif
}

} // namespace

/**
 * @brief 抓取冻结快照并显示取色覆盖窗口
 * @author chiangyang
 */
ColorPickerOverlay *ColorPickerOverlay::launch() {
    QRect geometry;
    qreal dpr = 1.0;
    const QPixmap snapshot = grabColorPickerSnapshot(geometry, dpr);
    if (snapshot.isNull()) {
        LOG_ERROR("[ColorPickerOverlay] launch: grabbed empty snapshot, ignored");
        return nullptr;
    }
    return new ColorPickerOverlay(snapshot, geometry, dpr);
}

/**
 * @brief 构造函数：设置窗口标志、创建放大镜并显示窗口
 * @author chiangyang
 */
ColorPickerOverlay::ColorPickerOverlay(const QPixmap &snapshot, const QRect &globalGeometry,
                                       qreal dpr, QWidget *parent)
    : QWidget(parent)
{
#ifdef Q_OS_MACOS
    // macOS 同 SnipScreen：不用 BypassWindowManagerHint（Sidecar/AirPlay 兼容）
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#else
    // 无边框、穿透窗口管理器、置顶
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
#endif
    // 仅放大镜区域有内容，其余完全透明
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);

    m_loupe = new ColorLoupe(this);
    m_loupe->setColorSource(snapshot, globalGeometry);
    // 信息条第二/三行的操作提示（独立取色器的交互）
    TranslationManager *tm = TranslationManager::instance();
    m_loupe->setHintText(tm->get("colorPicker.hint.picker", "Wheel: format · Click: copy"));
    m_loupe->setEscHint(tm->get("colorPicker.hint.esc.picker", "Esc: exit"));

    setGeometry(globalGeometry);
    m_loupe->setCursorGlobal(QCursor::pos());

    LOG_INFO(QString("[ColorPickerOverlay] launched, geometry: %1,%2 %3x%4, dpr=%5")
        .arg(globalGeometry.x()).arg(globalGeometry.y())
        .arg(globalGeometry.width()).arg(globalGeometry.height())
        .arg(dpr));

    show();
    raise();
    activateWindow();

#ifdef Q_OS_MACOS
    m_geometry = globalGeometry;

    // 覆盖菜单栏区域，保证菜单栏上方也能取色（move/show 可能重置级别，故在显示后调用）
    raisePickerWindowAboveMenuBar(winId());

    // 跨屏检测定时器（同 SnipScreen 的 30ms 轮询模式）
    m_screenWatchTimer.setInterval(30);
    connect(&m_screenWatchTimer, &QTimer::timeout, this, &ColorPickerOverlay::checkScreenSwitch);
    m_screenWatchTimer.start();
#endif
}

/**
 * @brief 鼠标按下：左键复制颜色，右键取消
 * @author chiangyang
 */
void ColorPickerOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        copyAndClose();
    } else if (event->button() == Qt::RightButton) {
        close();
    }
}

/**
 * @brief 鼠标移动：放大镜跟随光标
 * @author chiangyang
 */
void ColorPickerOverlay::mouseMoveEvent(QMouseEvent *event) {
    m_loupe->setCursorGlobal(event->globalPosition().toPoint());
}

/**
 * @brief 滚轮：循环切换颜色格式
 * @author chiangyang
 */
void ColorPickerOverlay::wheelEvent(QWheelEvent *event) {
    m_loupe->cycleFormat();
}

/**
 * @brief 键盘：Esc 取消、C 复制、Tab 切换格式
 *
 Tab 键在本窗口内无冲突（标注换色的 Tab 快捷键挂靠 SnipScreen 的
 * WindowShortcut 上下文，本窗口激活时不触发）。
 * @author chiangyang
 */
void ColorPickerOverlay::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_C:
        copyAndClose();
        break;
    case Qt::Key_Tab:
        m_loupe->cycleFormat();
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

/**
 * @brief 复制当前颜色到剪贴板、显示提示并关闭窗口
 * @author chiangyang
 */
void ColorPickerOverlay::copyAndClose() {
    if (!m_loupe->hasValidColor())
        return;

    const QString colorText = m_loupe->colorString();
    QGuiApplication::clipboard()->setText(colorText);

    TranslationManager *tm = TranslationManager::instance();
    showCopiedToast(tm->get("colorPicker.copied", "Copied %1").arg(colorText), QCursor::pos());

    close();
}

/**
 * @brief 在光标附近显示"已复制"提示（独立顶层窗口，约 1.2s 自动消失）
 * @author chiangyang
 */
void ColorPickerOverlay::showCopiedToast(const QString &text, const QPoint &globalPos) {
    QWidget *toast = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground, true);
    toast->setAttribute(Qt::WA_DeleteOnClose, true);

    QLabel *label = new QLabel(text, toast);
    label->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    label->setAlignment(Qt::AlignCenter);
    label->adjustSize();

    toast->resize(label->size());

    // 定位在参考点右上方，并钳制到参考点所在屏范围内
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen->geometry();
    int x = globalPos.x() + 24;
    int y = globalPos.y() - toast->height() - 24;
    x = qBound(bounds.left(), x, bounds.right() - toast->width() + 1);
    y = qBound(bounds.top(), y, bounds.bottom() - toast->height() + 1);

    toast->move(x, y);
    label->move(0, 0);
    toast->show();
    QTimer::singleShot(1200, toast, &QWidget::close);
}

#ifdef Q_OS_MACOS
/**
 * @brief 跨屏检测：鼠标移到其他屏时重抓该屏快照并移动窗口
 * @author chiangyang
 */
void ColorPickerOverlay::checkScreenSwitch() {
    const QPoint cursorPos = QCursor::pos();

    // 鼠标仍在当前覆盖范围内，无需切换
    if (m_geometry.contains(cursorPos))
        return;

    QScreen *newScreen = QGuiApplication::screenAt(cursorPos);
    if (!newScreen)
        return;

    // 先抓新屏快照（本窗口尚在旧屏，不会把自身抓进快照），再移动窗口
    QRect geometry;
    qreal dpr = 1.0;
    const QPixmap snapshot = grabColorPickerSnapshot(geometry, dpr);
    if (snapshot.isNull())
        return;

    LOG_INFO(QString("[ColorPickerOverlay] screen switch -> %1, geometry: %2,%3 %4x%5")
        .arg(newScreen->name())
        .arg(geometry.x()).arg(geometry.y())
        .arg(geometry.width()).arg(geometry.height()));

    m_geometry = geometry;
    m_loupe->setColorSource(snapshot, geometry);
    move(geometry.topLeft());
    resize(geometry.size());
    raisePickerWindowAboveMenuBar(winId());
    m_loupe->setCursorGlobal(cursorPos);
}
#endif
