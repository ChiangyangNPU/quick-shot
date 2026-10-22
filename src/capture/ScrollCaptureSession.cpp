#include "ScrollCaptureSession.h"
#include "SnipScreen.h"
#include "Selector.h"
#include "../core/TranslationManager.h"
#include "../log/Logger.h"
#include "../utils/Utils.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
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
///< 连续无法定位多少帧后重设匹配基准（150ms/帧，约 1.2 秒）
constexpr int kNoMatchResetThreshold = 8;
///< “已重新对齐”提示显示时长
constexpr int kResyncNoticeMs = 2500;

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

    m_stitcher.reset(orientation);
    m_active = true;
    m_noMatchStreak = 0;
    m_showResyncNotice = false;
    if (m_noticeTimer) {
        m_noticeTimer->stop();
    }

    // 隐藏工具栏与遮罩窗口：让用户看到并操作真实页面
    m_host->hideToolBar();
    m_host->hide();

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
            if (++m_noMatchStreak >= kNoMatchResetThreshold
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

    if (image.isNull()) {
        LOG_INFO("[ScrollCapture] finish without result, treating as cancel");
        emit finished();
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(1.0);
    LOG_INFO(QString("[ScrollCapture] finished, result %1x%2")
        .arg(pixmap.width()).arg(pixmap.height()));

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

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(14, 8, 10, 8);
    layout->setSpacing(12);

    /**
     * @brief 行高跟随字体/DPI 缩放
     *
     * 全局样式表字体为 10.5pt；固定 26px 在 150%~200% 缩放下会贴边甚至
     * 裁字。这里显式设置同一字号并按字体度量留出上下留白。
     */
    QFont hintFont = m_hintBar->font();
    hintFont.setPointSizeF(10.5);
    m_hintBar->setFont(hintFont);
    const int hintRowHeight = qMax(26, QFontMetrics(hintFont).height() + 10);

    auto *titleLabel = new QLabel(container);
    titleLabel->setObjectName(QStringLiteral("ScrollHintTitle"));
    titleLabel->setFixedHeight(hintRowHeight);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    m_statusLabel = new QLabel(container);
    m_statusLabel->setObjectName(QStringLiteral("ScrollHintStatus"));
    // 固定最小宽度：拼接长度变化时不引起提示条宽度抖动
    m_statusLabel->setMinimumWidth(190);
    m_statusLabel->setFixedHeight(hintRowHeight);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    m_finishBtn = new QPushButton(scrollText("scroll.finish", "Finish"), container);
    m_finishBtn->setObjectName(QStringLiteral("ScrollHintButton"));
    m_finishBtn->setFixedHeight(hintRowHeight);
    // 按钮不抢焦点：Enter/Esc 始终由提示条窗口的 eventFilter 统一处理
    m_finishBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_finishBtn, &QPushButton::clicked, this, &ScrollCaptureSession::finish);
    layout->addWidget(m_finishBtn, 0, Qt::AlignVCenter);

    m_cancelBtn = new QPushButton(scrollText("scroll.cancel", "Cancel"), container);
    m_cancelBtn->setObjectName(QStringLiteral("ScrollHintButton"));
    m_cancelBtn->setFixedHeight(hintRowHeight);
    m_cancelBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ScrollCaptureSession::cancel);
    layout->addWidget(m_cancelBtn, 0, Qt::AlignVCenter);

    /**
     * @brief 提示条样式：接近实色的深色 HUD
     *
     * 容器底板 248/255 不透明，加 1px 亮边与页面内容分离；状态文字由
     * 灰色提升为高亮半粗体，按钮填充/描边同步增强，避免在浅色或复杂
     * 页面上看起来像半透明、看不清。
     */
    container->setStyleSheet(QStringLiteral(
        "QWidget#ScrollHintContainer {"
        "  background-color: rgba(22, 22, 26, 248);"
        "  border: 1px solid rgba(255, 255, 255, 55);"
        "  border-radius: 8px; }"
        "QLabel#ScrollHintTitle {"
        "  color: #ffffff; font-weight: bold; padding: 0; margin: 0; }"
        "QLabel#ScrollHintStatus {"
        "  color: #f2f2f2; font-weight: bold; padding: 0; margin: 0; }"
        "QPushButton#ScrollHintButton {"
        "  color: #ffffff; background-color: rgba(255, 255, 255, 48);"
        "  border: 1px solid rgba(255, 255, 255, 125); border-radius: 5px;"
        "  padding: 0 14px; margin: 0; font-weight: bold; }"
        "QPushButton#ScrollHintButton:hover {"
        "  background-color: rgba(255, 255, 255, 78);"
        "  border-color: rgba(255, 255, 255, 170); }"));

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
            default:
                break;
        }
    }
    return QObject::eventFilter(watched, event);
}
