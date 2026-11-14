#include "ScrollCaptureSession.h"
#include "SnipScreen.h"
#include "Selector.h"
#include "../core/ConfigManager.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../utils/Utils.h"

#include <QAbstractButton>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// WDA_EXCLUDEFROMCAPTURE 在 Windows 10 2004+ SDK 中定义，旧 SDK 需手动定义
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

#ifdef Q_OS_MACOS
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace {
///< 抓帧间隔（毫秒）：兼顾滚动跟手与 CPU 占用
constexpr int kCaptureIntervalMs = 150;
///< 选区最小尺寸：过小无法进行重叠匹配
constexpr int kMinSelectionSize = 32;
///< 隐藏宿主后等待一帧再抓首帧，避免抓到尚未消失的遮罩
constexpr int kStartDelayMs = 120;
///< “已重新对齐”提示显示时长
constexpr int kResyncNoticeMs = 2500;
///< 选区参考框向外扩展的像素：让边框整体落在抓帧区域之外，避免被拍进长图
constexpr int kFrameOverlayOutset = 2;

/**
 * @brief 生成 QSS 颜色文本（带透明度时用 rgba()，否则用 #RRGGBB）
 * @param c 颜色
 * @return QSS 颜色文本
 * @author chiangyang
 */
/**
 * @brief 各灵敏度档位对应的"连续无法定位重设基准"帧数
 *
 * 宽松档容忍更多连续失败帧（减少"画面变化过大"提示的出现频率），
 * 严格档尽快重设基准。
 * @author chiangyang
 */
int noMatchResetFor(int sensitivity) {
    switch (sensitivity) {
        case 0:  return 14;  // 宽松：约 2.1 秒
        case 2:  return 5;   // 严格：约 0.75 秒
        default: return 8;   // 标准：约 1.2 秒
    }
}

QString hintQssColor(const QColor &c) {
    if (c.alpha() < 255) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    return c.name();
}

#ifdef Q_OS_MACOS
/**
 * @brief 让提示窗口忽略鼠标事件（macOS）
 *
 * Qt::WindowTransparentForInput 在 macOS 上不生效，需要直接设置
 * NSWindow 的 ignoresMouseEvents：否则这个全屏透明的参考框会吞掉
 * 滚轮与点击，用户无法操作下层页面滚动。
 * @param wid 窗口句柄
 * @author chiangyang
 */
void setWindowIgnoresMouse(WId wid) {
    if (!wid) return;
    id nsView = reinterpret_cast<id>(wid);
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    ((void (*)(id, SEL, signed char))objc_msgSend)(nsWindow, sel_registerName("setIgnoresMouseEvents:"), 1);
}
#endif

/**
 * @brief 滚动截图期间的选区参考框
 *
 * 会话开始后宿主 SnipScreen 仍然要隐藏（否则遮罩会挡住页面，也会被拍进长图），
 * 因此用独立的小窗口只画一圈边框，给用户提示当前正在截取的范围。
 * 窗口完全透明且鼠标穿透，用户可以直接操作下层应用滚动页面；
 * 边框绘制在选区外侧，抓帧区域（选区向内缩 2px）不会拍到框线。
 * @author chiangyang
 */
class ScrollFrameOverlay : public QWidget {
public:
    explicit ScrollFrameOverlay(const QRect &selection, QWidget *parent = nullptr)
        : QWidget(parent) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        // show() 不抢焦点：否则用户第一次滚动页面的点击会被这个提示框吃掉
        setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_MACOS
        // 用户会切到浏览器等其它应用滚动页面，参考框需在应用失活时继续显示
        setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
        setGeometry(selection.adjusted(-kFrameOverlayOutset, -kFrameOverlayOutset,
                                      kFrameOverlayOutset, kFrameOverlayOutset));
    }

protected:
    /**
     * @brief 只绘制选区边框，不画遮罩、锚点与尺寸标签
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(QPen(StyleManager::getCaptureBorderColor(),
                            StyleManager::SNIP_BORDER_WIDTH));
        // 内缩 1px 绘制：2px 宽的边框整体落在窗口外缘，即选区外侧
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
};

/**
 * @brief 将窗口排除在屏幕捕获之外（提示条兜底，避免选区占满整屏时被拍进长图）
 *
 * macOS 使用 NSWindow.sharingType = NSWindowSharingNone，
 * 仅对基于 CGWindowList 的抓屏生效；提示条正常情况下已位于选区外，
 * 此处只为极端场景兜底。
 * @param wid 窗口句柄
 * @author chiangyang
 */
void excludeWindowFromCapture(WId wid) {
    if (!wid) return;
#ifdef Q_OS_WIN
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(wid), WDA_EXCLUDEFROMCAPTURE);
#elif defined(Q_OS_MACOS)
    id nsView = reinterpret_cast<id>(wid);
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)(nsView, sel_registerName("window"));
    if (!nsWindow) return;
    ((void (*)(id, SEL, long))objc_msgSend)(nsWindow, sel_registerName("setSharingType:"), 1);
#else
    Q_UNUSED(wid);
#endif
}

#ifdef Q_OS_WIN
/**
 * @brief 将提示条提升到 Windows 置顶窗口组的最上层
 *
 * Qt::WindowStaysOnTopHint 只保证窗口属于 WS_EX_TOPMOST 组；如果目标
 * 应用本身也置顶（全屏浏览器、播放器等），新创建的工具窗仍可能被压住，
 * 看起来发灰、像半透明。显示后显式 SetWindowPos(HWND_TOPMOST) 可把
 * 提示条放到置顶组顶部，保证状态和按钮可读。
 * @param wid 窗口句柄
 * @author chiangyang
 */
void raiseWindowAboveTopmost(WId wid) {
    if (!wid) return;
    SetWindowPos(reinterpret_cast<HWND>(wid), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
#endif

/**
 * @brief 翻译键取值（带英文兜底，未翻译语言也能正常显示）
 * @author chiangyang
 */
QString scrollText(const char *key, const char *fallback) {
    return TranslationManager::instance()->get(QString::fromLatin1(key), QString::fromLatin1(fallback));
}
} // namespace

ScrollCaptureSession::ScrollCaptureSession(SnipScreen *host, QObject *parent)
    : QObject(parent)
    , m_host(host) {
        // 从配置恢复对齐灵敏度（提示条按钮可改，跨会话保留）
        m_sensitivity = qBound(0, ConfigManager::instance()->value(
            QStringLiteral("capture/scrollSensitivity"), 1).toInt(), 2);
        m_noMatchResetThreshold = noMatchResetFor(m_sensitivity);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ScrollCaptureSession::captureTick);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, this, [this]() {
        m_showResyncNotice = false;
        updateHintText();
    });
}

ScrollCaptureSession::~ScrollCaptureSession() {
    shutdown();
}

bool ScrollCaptureSession::start(ScrollStitcher::Orientation orientation) {
    if (m_active || !m_host) {
        return false;
    }
    // 仅在选区完成后可用；Locked 表示处于标注模式，选区几何同样有效
    const SelectorStatus status = m_host->selector()->status();
    if (status != SelectorStatus::Captured && status != SelectorStatus::Locked) {
        LOG_WARNING("[ScrollCapture] start ignored: selection not captured");
        return false;
    }

    const QRect selection = m_host->selector()->selected();
    if (selection.width() < kMinSelectionSize || selection.height() < kMinSelectionSize) {
        LOG_WARNING(QString("[ScrollCapture] start ignored: selection too small %1x%2")
            .arg(selection.width()).arg(selection.height()));
        return false;
    }

    // 内缩 2px 避开选区边框（边框绘制在选区边界上）
    m_captureRect = selection.adjusted(2, 2, -2, -2);
    if (m_captureRect.width() < kMinSelectionSize || m_captureRect.height() < kMinSelectionSize) {
        m_captureRect = selection;
    }

    // 记录选区所在屏的 DPR：拼接帧按物理像素处理，最终长图需携带该 DPR 才能按逻辑尺寸显示
    QScreen *screen = QGuiApplication::screenAt(m_captureRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    m_dpr = screen ? screen->devicePixelRatio() : 1.0;

    m_stitcher.reset(orientation);
    m_stitcher.setSensitivity(m_sensitivity);
    m_active = true;
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }

    // 隐藏工具栏与遮罩窗口：让用户看到并操作真实页面
    m_host->hideToolBar();
    m_host->hide();

    // 宿主整体隐藏后，用独立透明窗口保留选区边框，提示当前截取范围
    createFrameOverlay(selection);

    createHintBar();
    positionHintBar();
    m_hintBar->show();
    m_hintBar->raise();
    m_hintBar->activateWindow();
    m_hintBar->setFocus();
#ifdef Q_OS_WIN
    // 目标应用自身置顶时，Qt 的置顶窗口仍可能被压在下面；再提升一次
    raiseWindowAboveTopmost(m_hintBar->winId());
#endif

    LOG_INFO(QString("[ScrollCapture] start %1, rect: %2,%3 %4x%5")
        .arg(orientation == ScrollStitcher::Orientation::Vertical ? "vertical" : "horizontal")
        .arg(m_captureRect.x()).arg(m_captureRect.y())
        .arg(m_captureRect.width()).arg(m_captureRect.height()));

    // 等遮罩真正消失后再抓首帧
    QTimer::singleShot(kStartDelayMs, this, &ScrollCaptureSession::beginCapture);
    return true;
}

void ScrollCaptureSession::beginCapture() {
    if (!m_active) {
        return;
    }
    if (m_timer) {
        m_timer->start(kCaptureIntervalMs);
    }
    captureTick();
}

void ScrollCaptureSession::stopTimer() {
    if (m_timer) {
        m_timer->stop();
    }
}

void ScrollCaptureSession::captureTick() {
    if (!m_active) {
        return;
    }

    const QImage frame = grabFrame();
    if (frame.isNull()) {
        return;
    }

    switch (m_stitcher.appendFrame(frame)) {
        case ScrollStitcher::AppendResult::Appended:
            m_noMatchStreak = 0;
            updateHintText();
            break;
        case ScrollStitcher::AppendResult::LimitReached:
            LOG_INFO("[ScrollCapture] max length reached, finishing automatically");
            finish();
            break;
        case ScrollStitcher::AppendResult::SizeMismatch:
            m_noMatchStreak = 0;
            LOG_WARNING("[ScrollCapture] frame size changed, ignored");
            break;
        case ScrollStitcher::AppendResult::NoMatch:
            // 快速滚动、页面跳转或大面积动画导致无法对齐：连续失败后
            // 以当前画面重设基准，避免一直和过期基准比较而永久卡住
            if (++m_noMatchStreak >= m_noMatchResetThreshold
                && m_stitcher.restartFromFrame(frame)) {
                LOG_INFO("[ScrollCapture] re-aligned after repeated no-match");
                m_noMatchStreak = 0;
                showResyncNotice();
            }
            break;
        default:
            // Duplicate：页面未滚动（或仅有抖动），等待下一帧
            m_noMatchStreak = 0;
            break;
    }
}

QImage ScrollCaptureSession::grabFrame() const {
    if (!m_host) {
        return QImage();
    }

    QImage image;
#ifdef Q_OS_WIN
    // 只抓选区矩形：滚动截图每 150ms 抓一帧，多屏高分屏下抓整块虚拟桌面
    // 会产生大量无用的拷贝与内存分配，且全部发生在 GUI 线程
    const QPixmap shot = Utils::grabScreenRegionPixmap(m_captureRect);
    if (shot.isNull() || shot.size() != m_captureRect.size()) {
        LOG_WARNING(QString("[ScrollCapture] grabFrame: region grab failed, shot %1x%2, rect %3x%4")
            .arg(shot.width()).arg(shot.height())
            .arg(m_captureRect.width()).arg(m_captureRect.height()));
        return QImage();
    }
    image = shot.toImage();
#else
    QPixmap shot;
    qreal dpr = 1.0;
#ifdef Q_OS_MACOS
    // macOS 单屏：抓选区所在屏整屏（包含菜单栏区域）
    QScreen *screen = QGuiApplication::screenAt(m_captureRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return QImage();
    shot = screen->grabWindow(0);
    dpr = screen->devicePixelRatio();
#else
    shot = Utils::grabVirtualDesktopPixmap();
#endif
    if (shot.isNull()) {
        return QImage();
    }

    // host->virtualGeometry() 与抓图覆盖范围一致（macOS 已含菜单栏修正）
    const QRect geo = m_host->virtualGeometry();
    const QRect local(
        QPoint(qRound((m_captureRect.x() - geo.x()) * dpr),
               qRound((m_captureRect.y() - geo.y()) * dpr)),
        QSize(qRound(m_captureRect.width() * dpr), qRound(m_captureRect.height() * dpr)));
    const QRect clipped = local.intersected(shot.rect());
    if (clipped.isEmpty()) {
        LOG_WARNING("[ScrollCapture] grabFrame: selection outside captured desktop");
        return QImage();
    }
    image = shot.copy(clipped).toImage();
#endif

    // 统一为物理像素、dpr=1，保证逐帧尺寸一致
    image.setDevicePixelRatio(1.0);
    return image.convertToFormat(QImage::Format_RGB32);
}

void ScrollCaptureSession::finish() {
    if (!m_active) {
        return;
    }

    stopTimer();
    const QImage image = m_stitcher.result();
    m_active = false;
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }
    destroyHintBar();
    destroyFrameOverlay();

    if (image.isNull()) {
        LOG_INFO("[ScrollCapture] finish without result, treating as cancel");
        emit finished();
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    // 拼接图是物理像素，携带选区屏 DPR，贴图窗口按逻辑尺寸显示才不会放大模糊
    pixmap.setDevicePixelRatio(qMax<qreal>(1.0, m_dpr));
    LOG_INFO(QString("[ScrollCapture] finished, result %1x%2 dpr=%3")
        .arg(pixmap.width()).arg(pixmap.height()).arg(pixmap.devicePixelRatio()));

    emit completed(pixmap, m_captureRect.topLeft());
    emit finished();
}

void ScrollCaptureSession::cancel() {
    if (!m_active) {
        return;
    }
    stopTimer();
    m_active = false;
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }
    destroyHintBar();
    destroyFrameOverlay();
    LOG_INFO("[ScrollCapture] canceled");
    emit finished();
}

void ScrollCaptureSession::setOrientation(ScrollStitcher::Orientation orientation) {
    if (!m_active || m_stitcher.orientation() == orientation) {
        return;
    }
    // 切换方向即重置拼接：以切换后的第一帧重新开始
    m_stitcher.reset(orientation);
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }
    updateHintText();
    LOG_INFO(QString("[ScrollCapture] orientation switched to %1")
        .arg(orientation == ScrollStitcher::Orientation::Vertical ? "vertical" : "horizontal"));
}

void ScrollCaptureSession::setSensitivity(int level) {
    const int clamped = qBound(0, level, 2);
    if (m_sensitivity == clamped) {
        return;
    }
    m_sensitivity = clamped;
    m_noMatchResetThreshold = noMatchResetFor(clamped);
    m_stitcher.setSensitivity(clamped);
    ConfigManager::instance()->setValue(QStringLiteral("capture/scrollSensitivity"), clamped);
    updateHintText();
    LOG_INFO(QString("[ScrollCapture] sensitivity switched to %1")
        .arg(clamped == 0 ? "loose" : (clamped == 2 ? "strict" : "normal")));
}

void ScrollCaptureSession::shutdown() {
    // SnipScreen 全局复用，退出后可能再次进入截图：这里只停止并清理界面，
    // 定时器本身保留（随 QObject 父子关系销毁），避免下次 start 无定时器可用
    if (m_timer) {
        m_timer->stop();
    }
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }
    m_active = false;
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    destroyHintBar();
    destroyFrameOverlay();
}

void ScrollCaptureSession::createFrameOverlay(const QRect &selection) {
    if (m_frameOverlay) {
        return;
    }

    m_frameOverlay = new ScrollFrameOverlay(selection);
    m_frameOverlay->setObjectName(QStringLiteral("ScrollFrameOverlay"));
    // 先 show 再设置捕获排除：Windows 需要窗口已创建且可见才能设置显示亲和性
    // 注意不能调 raise()：macOS 上 raise 会激活整个应用，把被盖住的窗口
    // （如设置窗口）整体抬到前台。参考框有 WA_ShowWithoutActivating +
    // 置顶 Tool 层级，show 后本身就在其他应用窗口之上，无需提升
    m_frameOverlay->show();
    excludeWindowFromCapture(m_frameOverlay->winId());
#ifdef Q_OS_MACOS
    // 参考框只做视觉提示，不参与任何鼠标交互
    setWindowIgnoresMouse(m_frameOverlay->winId());
#endif

    LOG_INFO(QString("[ScrollCapture] frame overlay rect: %1,%2 %3x%4")
        .arg(m_frameOverlay->geometry().x()).arg(m_frameOverlay->geometry().y())
        .arg(m_frameOverlay->geometry().width()).arg(m_frameOverlay->geometry().height()));
}

void ScrollCaptureSession::destroyFrameOverlay() {
    if (!m_frameOverlay) {
        return;
    }
    m_frameOverlay->hide();
    m_frameOverlay->deleteLater();
    m_frameOverlay = nullptr;
}

void ScrollCaptureSession::createHintBar() {
    if (m_hintBar) {
        return;
    }

    m_hintBar = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_hintBar->setObjectName(QStringLiteral("ScrollHintBar"));
#ifdef Q_OS_MACOS
    // 用户需要切到其他应用滚动页面；Qt::Tool 窗口默认随应用失活隐藏，
    // 这里保持提示条常驻，确保进度与完成/取消按钮始终可见
    m_hintBar->setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
    // 非激活面板：滚动过程中点击完成/取消按钮不激活应用，应用里被其他
    // 应用盖住的窗口（如设置窗口）不会随激活被系统整体抬起
    Utils::setNSPanelNonActivating(m_hintBar);
#endif
    m_hintBar->setAttribute(Qt::WA_TranslucentBackground);
    m_hintBar->setAttribute(Qt::WA_DeleteOnClose, false);
    m_hintBar->installEventFilter(this);

    /**
     * @brief 深色圆角由容器子控件绘制，顶层窗口只负责透明
     *
     * 普通 QWidget 同时设置 WA_TranslucentBackground 时，样式表里的
     * background-color 不会生效，只剩文字浮在页面上，看起来像“透明字”。
     * 这里用带 WA_StyledBackground 的容器绘制底板，顶层窗口保持透明，
     * 圆角外的区域仍能透出桌面。
     */
    auto *outerLayout = new QVBoxLayout(m_hintBar);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *container = new QWidget(m_hintBar);
    container->setObjectName(QStringLiteral("ScrollHintContainer"));
    container->setAttribute(Qt::WA_StyledBackground, true);
    outerLayout->addWidget(container);

    // 内边距与间距对齐工具栏（BaseToolBar::initShell 为 8,4,8,4 + spacing 4），
    // 保证提示条高度与工具栏一致
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    auto *titleLabel = new QLabel(container);
    titleLabel->setObjectName(QStringLiteral("ScrollHintTitle"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    m_statusLabel = new QLabel(container);
    m_statusLabel->setObjectName(QStringLiteral("ScrollHintStatus"));
    // 固定最小宽度：拼接长度变化时不引起提示条宽度抖动
    m_statusLabel->setMinimumWidth(190);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    /**
     * @brief 创建提示条按钮（与工具栏按钮完全同款）
     *
     * 直接使用 QPushButton + StyleManager::applyActionButtonStyle，与
     * ScreenshotToolBar 的操作按钮走同一套 QSS（内边距 0.24em、圆角 0.24em、
     * ToolbarBtn/ToolbarButtonHover/ToolbarText 配色），按钮高度与观感和
     * 工具栏一致，主题切换同步生效。不设固定高度，由按钮自身的 QSS 决定。
     * @author chiangyang
     */
    auto makeHintButton = [container](const QString &text) {
        auto *btn = new QPushButton(text, container);
        btn->setObjectName(QStringLiteral("ScrollHintButton"));
        // 按钮不抢焦点：Enter/Esc 始终由提示条窗口的 eventFilter 统一处理
        btn->setFocusPolicy(Qt::NoFocus);
        StyleManager::applyActionButtonStyle(btn);
        return btn;
    };

    // 方向切换按钮：点击在垂直/水平之间来回切换（文案在 updateHintText 中按当前方向刷新）
    m_orientationBtn = makeHintButton(QString());
    connect(m_orientationBtn, &QAbstractButton::clicked, this, [this]() {
        setOrientation(m_stitcher.orientation() == ScrollStitcher::Orientation::Vertical
                           ? ScrollStitcher::Orientation::Horizontal
                           : ScrollStitcher::Orientation::Vertical);
    });
    layout->addWidget(m_orientationBtn, 0, Qt::AlignVCenter);

    // 灵敏度切换按钮：点击在 宽松/标准/严格 之间循环（文案在 updateHintText 刷新）
    m_sensitivityBtn = makeHintButton(QString());
    connect(m_sensitivityBtn, &QAbstractButton::clicked, this, [this]() {
        setSensitivity((m_sensitivity + 1) % 3);
    });
    layout->addWidget(m_sensitivityBtn, 0, Qt::AlignVCenter);

    m_finishBtn = makeHintButton(scrollText("scroll.finish", "Finish"));
    connect(m_finishBtn, &QAbstractButton::clicked, this, &ScrollCaptureSession::finish);
    layout->addWidget(m_finishBtn, 0, Qt::AlignVCenter);

    m_cancelBtn = makeHintButton(scrollText("scroll.cancel", "Cancel"));
    connect(m_cancelBtn, &QAbstractButton::clicked, this, &ScrollCaptureSession::cancel);
    layout->addWidget(m_cancelBtn, 0, Qt::AlignVCenter);

    /**
     * @brief 提示条样式：与工具栏统一
     *
     * 底板复用工具栏背景色（ToolbarBg + 0.3em 圆角），标题/状态文字用工具栏
     * 文字色；按钮由 makeHintButton 应用工具栏按钮样式，三者随深浅主题同步。
     */
    const QString toolbarBg = hintQssColor(StyleManager::color(StyleManager::StyleColorId::ToolbarBg));
    const QString toolbarText = hintQssColor(StyleManager::color(StyleManager::StyleColorId::ToolbarText));
    container->setStyleSheet(
        QStringLiteral(
            "QWidget#ScrollHintContainer {"
            "  background-color: %1; border-radius: 0.3em; }"
            "QLabel#ScrollHintTitle {"
            "  color: %2; font-weight: bold; padding: 0; margin: 0; }"
            "QLabel#ScrollHintStatus {"
            "  color: %2; padding: 0; margin: 0; }")
            .arg(toolbarBg, toolbarText));

    updateHintText();
    m_hintBar->adjustSize();

    // 兜底：选区占满整屏时提示条可能落在选区内，排除出捕获避免被拍进长图
    excludeWindowFromCapture(m_hintBar->winId());
}

void ScrollCaptureSession::destroyHintBar() {
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }
    m_showResyncNotice = false;
    if (!m_hintBar) {
        return;
    }
    m_hintBar->removeEventFilter(this);
    m_hintBar->hide();
    m_hintBar->deleteLater();
    m_hintBar = nullptr;
    m_statusLabel = nullptr;
    m_orientationBtn = nullptr;
    m_sensitivityBtn = nullptr;
    m_finishBtn = nullptr;
    m_cancelBtn = nullptr;
}

void ScrollCaptureSession::positionHintBar() {
    if (!m_hintBar) {
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(m_captureRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(QPoint(0, 0), m_captureRect.size());

    const QSize hintSize = m_hintBar->size();
    const int gap = 10;
    const int minX = available.left() + gap;
    const int minY = available.top() + gap;
    const int maxX = qMax(minX, available.right() - hintSize.width() - gap);
    const int maxY = qMax(minY, available.bottom() - hintSize.height() - gap);

    /**
     * @brief 夹紧到可用区后放置；若仍与选区相交则放弃该候选
     *
     * 提示条一旦落入选区就会被抓进长图，而它的状态文字又随拼接长度变化，
     * 会反过来干扰模板匹配，造成"没滚动但像素一直增加"的假滚动。
     */
    auto tryPlace = [&](int x, int y) -> bool {
        const QRect candidate(qBound(minX, x, maxX), qBound(minY, y, maxY),
                              hintSize.width(), hintSize.height());
        if (candidate.intersects(m_captureRect)) {
            return false;
        }
        m_hintBar->move(candidate.topLeft());
        return true;
    };

    const int centerX = m_captureRect.center().x() - hintSize.width() / 2;
    const int centerY = m_captureRect.center().y() - hintSize.height() / 2;
    // 依次尝试：选区下方 → 上方 → 右侧 → 左侧，全部避开选区
    if (tryPlace(centerX, m_captureRect.bottom() + gap)
        || tryPlace(centerX, m_captureRect.top() - hintSize.height() - gap)
        || tryPlace(m_captureRect.right() + gap, centerY)
        || tryPlace(m_captureRect.left() - hintSize.width() - gap, centerY)) {
        return;
    }

    // 选区几乎占满整屏时无处可放：退回屏幕顶部，靠平台排除捕获兜底
    LOG_WARNING("[ScrollCapture] hint bar overlaps selection, rely on capture exclusion");
    m_hintBar->move(qBound(minX, centerX, maxX), minY);
}

void ScrollCaptureSession::updateHintText() {
    if (!m_hintBar) {
        return;
    }

    // 标题/提示文案随方向与长度刷新
    const bool vertical = m_stitcher.orientation() == ScrollStitcher::Orientation::Vertical;
    auto *titleLabel = m_hintBar->findChild<QLabel *>(QStringLiteral("ScrollHintTitle"));
    if (titleLabel) {
        titleLabel->setText(vertical ? scrollText("scroll.vertical", "Vertical Scroll Capture")
                                     : scrollText("scroll.horizontal", "Horizontal Scroll Capture"));
    }
    // 方向按钮文案展示当前方向，点击后切换到另一方向
    if (m_orientationBtn) {
        m_orientationBtn->setText(vertical
            ? scrollText("scroll.mode.vertical", "Vertical")
            : scrollText("scroll.mode.horizontal", "Horizontal"));
    }
    // 灵敏度按钮文案展示当前档位，点击后循环切换
    if (m_sensitivityBtn) {
        static const char *levelKeys[] = {
            "scroll.sens.loose", "scroll.sens.normal", "scroll.sens.strict"
        };
        static const char *levelFallbacks[] = { "Loose", "Normal", "Strict" };
        m_sensitivityBtn->setText(scrollText("scroll.sensitivity", "Sensitivity")
            + QLatin1String(": ")
            + scrollText(levelKeys[m_sensitivity], levelFallbacks[m_sensitivity]));
    }
    if (m_statusLabel) {
        if (m_showResyncNotice) {
            m_statusLabel->setText(
                scrollText("scroll.realign", "Content changed; re-aligned"));
        } else {
            const int length = m_stitcher.stitchedLength();
            m_statusLabel->setText(length > 0
                ? scrollText("scroll.status", "Captured %1 px").arg(length)
                : scrollText("scroll.hint", "Scroll the page to start"));
        }
    }
    m_hintBar->adjustSize();
    positionHintBar();
}

void ScrollCaptureSession::showResyncNotice() {
    m_showResyncNotice = true;
    updateHintText();
    if (m_noticeTimer) {
        m_noticeTimer->start(kResyncNoticeMs);
    }
}

bool ScrollCaptureSession::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_hintBar && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                finish();
                return true;
            case Qt::Key_Escape:
                cancel();
                return true;
            case Qt::Key_V:
                setOrientation(ScrollStitcher::Orientation::Vertical);
                return true;
            case Qt::Key_H:
                setOrientation(ScrollStitcher::Orientation::Horizontal);
                return true;
            case Qt::Key_S:
                setSensitivity((m_sensitivity + 1) % 3);
                return true;
            default:
                break;
        }
    }
    return QObject::eventFilter(watched, event);
}
