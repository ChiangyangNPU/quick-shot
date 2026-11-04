#include "TranslateOverlayWindow.h"
#include "../translate/TranslateService.h"
#include "../translate/TranslateEngine.h"
#include "../core/StyleManager.h"
#include "../core/TranslationManager.h"
#include "../utils/Utils.h"
#include "../log/Logger.h"
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QToolButton>
#include <QSplitter>
#include <QSizeGrip>
#include <QFrame>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QTimer>
#include <QShortcut>
#include <QMouseEvent>
#include <QTextDocument>
#include <QList>
#include <memory>

namespace {

/**
 * @brief 翻译窗口配色（跟随当前主题）
 * @author chiangyang
 */
struct TranslatePalette {
    QColor windowBg;  ///< 窗口/文本区域背景色
    QColor headerBg;  ///< 标题栏背景色
    QColor border;    ///< 窗口边框色
    QColor divider;   ///< 分栏/分隔线颜色
    QColor imageBg;   ///< 原图区域背景色
    QColor textColor; ///< 主文字颜色
    QColor subText;   ///< 次要文字颜色
};

/**
 * @brief 按当前主题返回翻译窗口配色
 * @return 配色结构体
 * @author chiangyang
 */
TranslatePalette translatePalette() {
    if (StyleManager::themeMode() == StyleManager::ThemeMode::Dark) {
        return {QColor(32, 33, 36), QColor(43, 44, 47), QColor(60, 64, 67),
                QColor(60, 64, 67), QColor(24, 25, 27), QColor(232, 234, 237),
                QColor(154, 160, 166)};
    }
    return {QColor(255, 255, 255), QColor(242, 243, 245), QColor(217, 221, 227),
            QColor(228, 231, 235), QColor(246, 247, 248), QColor(31, 31, 31),
            QColor(95, 99, 104)};
}

/**
 * @brief 创建底部操作按钮（图标在上、文字在下）
 * @param iconPath 图标资源路径
 * @param text 按钮文字
 * @param palette 配色
 * @param parent 父控件
 * @return 创建好的工具按钮
 * @author chiangyang
 */
QToolButton *createActionButton(const QString &iconPath, const QString &text,
                                const TranslatePalette &palette, QWidget *parent) {
    QToolButton *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(StyleManager::loadSvgIcon(iconPath, QSize(20, 20), palette.subText));
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(54);
    button->setMinimumWidth(76);
    button->setStyleSheet(QString(
        "QToolButton{background:transparent;border:none;border-radius:6px;"
        "color:%1;font-size:9pt;padding:4px 6px;}"
        "QToolButton:hover{background:%2;}"
        "QToolButton:pressed{background:%3;}")
        .arg(palette.subText.name(), palette.headerBg.name(), palette.divider.name()));
    return button;
}

} // namespace

/**
 * @brief 原图显示控件：等比缩放并居中绘制截图
 * @author chiangyang
 */
class TranslateImageView : public QWidget {
public:
    /**
     * @brief 构造函数
     * @param pixmap 待显示的原图
     * @param bgColor 背景色
     * @param parent 父控件
     * @author chiangyang
     */
    TranslateImageView(const QPixmap &pixmap, const QColor &bgColor, QWidget *parent = nullptr)
        : QWidget(parent), m_pixmap(pixmap), m_bgColor(bgColor) {
        setMinimumSize(120, 120);
    }

protected:
    /**
     * @brief 绘制事件：等比缩放原图并居中绘制
     * @param event 绘制事件
     * @author chiangyang
     */
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), m_bgColor);
        if (m_pixmap.isNull()) return;

        // 四周留白，原图按控件大小等比缩放后居中
        const int margin = 16;
        const QRect area = rect().adjusted(margin, margin, -margin, -margin);
        if (area.width() <= 0 || area.height() <= 0) return;

        const QSize scaledSize = m_pixmap.size().scaled(area.size(), Qt::KeepAspectRatio);
        const QPoint topLeft(area.left() + (area.width() - scaledSize.width()) / 2,
                             area.top() + (area.height() - scaledSize.height()) / 2);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(QRect(topLeft, scaledSize), m_pixmap);
    }

private:
    QPixmap m_pixmap;  ///< 原图
    QColor m_bgColor;  ///< 背景色
};

/**
 * @brief 翻译窗口标题栏：按住拖动窗口，双击关闭
 * @author chiangyang
 */
class TranslateTitleBar : public QWidget {
public:
    /**
     * @brief 构造函数
     * @param target 被拖动的窗口
     * @param parent 父控件
     * @author chiangyang
     */
    explicit TranslateTitleBar(QWidget *target, QWidget *parent = nullptr)
        : QWidget(parent), m_target(target) {}

protected:
    /**
     * @brief 鼠标按下事件：记录拖动起点
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - m_target->frameGeometry().topLeft();
        }
    }

    /**
     * @brief 鼠标移动事件：拖动窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_dragging) {
            m_target->move(event->globalPosition().toPoint() - m_dragOffset);
        }
    }

    /**
     * @brief 鼠标释放事件：结束拖动
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = false;
        }
    }

    /**
     * @brief 鼠标双击事件：关闭窗口
     * @param event 鼠标事件
     * @author chiangyang
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            m_target->close();
        }
    }

private:
    QWidget *m_target;    ///< 被拖动的窗口
    QPoint m_dragOffset;  ///< 拖动起始偏移
    bool m_dragging = false; ///< 是否正在拖动
};

/**
 * @brief 一站式批量翻译并显示翻译结果窗口
 * @param parent 父窗口（用于创建标签和信号接收者）
 * @param pixmap 截图原图（用于左侧原图显示）
 * @param texts 原文文本列表
 * @param overlayPos 结果窗口定位点（全局坐标，自动夹取到屏幕内）
 * @param labelRect 加载/错误标签的居中区域（parent 相对坐标）
 * @param onOverlayShown 翻译成功并显示结果窗口后的回调（可选，如 SnipScreen 退出截图框）
 * @author chiangyang
 */
void TranslateOverlayWindow::translateAndShow(QWidget *parent,
                                              const QPixmap &pixmap,
                                              const QStringList &texts,
                                              const QPoint &overlayPos,
                                              const QRect &labelRect,
                                              std::function<void()> onOverlayShown) {
    if (!parent) return;

    TranslationManager *tm = TranslationManager::instance();

    // 创建"翻译中"加载提示标签（居中在 labelRect）
    QLabel *translatingLabel = new QLabel(tm->get("translate.translating", "Translating..."), parent);
    translatingLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
    translatingLabel->setAlignment(Qt::AlignCenter);
    translatingLabel->adjustSize();
    translatingLabel->move(labelRect.x() + (labelRect.width() - translatingLabel->width()) / 2,
                           labelRect.y() + (labelRect.height() - translatingLabel->height()) / 2);
    translatingLabel->show();

    LOG_INFO(QString("TranslateOverlayWindow: batch translation started, %1 segments").arg(texts.size()));

    // 连接 TranslateService 信号（一次性连接，任一信号触发后自动断开两者）
    TranslateService *ts = TranslateService::instance();
    auto connFinish = std::make_shared<QMetaObject::Connection>();
    auto connFail = std::make_shared<QMetaObject::Connection>();

    *connFinish = QObject::connect(ts, &TranslateService::batchFinished, parent,
            [pixmap, texts, overlayPos, translatingLabel, connFinish, connFail, onOverlayShown](
                const QStringList &translatedTexts) {
        QObject::disconnect(*connFinish);
        QObject::disconnect(*connFail);

        translatingLabel->hide();
        translatingLabel->deleteLater();

        auto *overlay = new TranslateOverlayWindow(pixmap, texts, translatedTexts);

        // 结果窗口比截图选区更宽，定位点夹取到所在屏幕的可用区域内，避免超出屏幕
        QPoint pos = overlayPos;
        QScreen *screen = QGuiApplication::screenAt(overlayPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect avail = screen->availableGeometry();
            pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - overlay->width() + 1)));
            pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - overlay->height() + 1)));
        }
        overlay->move(pos);
        overlay->show();
        overlay->setFocus();

        LOG_INFO("TranslateOverlayWindow: result window shown after batch translation");

        // 翻译成功显示窗口后触发回调（如 SnipScreen 退出截图框，类似贴图完成后销毁截图框）
        if (onOverlayShown) {
            onOverlayShown();
        }
    });

    *connFail = QObject::connect(ts, &TranslateService::failed, parent,
            [parent, labelRect, translatingLabel, connFinish, connFail](
                TranslateEngine::TranslateError code, const QString &detail) {
        QObject::disconnect(*connFinish);
        QObject::disconnect(*connFail);

        translatingLabel->hide();
        translatingLabel->deleteLater();

        // 根据错误码显示本地化错误提示（3秒后自动消失）
        QString errMsg = TranslateService::errorMessage(code);

        QLabel *errLabel = new QLabel(errMsg, parent);
        errLabel->setStyleSheet(StyleManager::getOcrLoadingLabelStyle());
        errLabel->setAlignment(Qt::AlignCenter);
        errLabel->adjustSize();
        errLabel->move(labelRect.x() + (labelRect.width() - errLabel->width()) / 2,
                       labelRect.y() + (labelRect.height() - errLabel->height()) / 2);
        errLabel->show();
        QTimer::singleShot(3000, errLabel, &QWidget::deleteLater);

        LOG_WARNING(QString("TranslateOverlayWindow: translate failed, code=%1, detail=%2")
                        .arg(static_cast<int>(code)).arg(detail));
    });

    ts->translateBatch(texts);
}

/**
 * @brief 构造函数
 * @param pixmap 截图原图
 * @param texts 原文文本列表
 * @param translatedTexts 译文文本列表
 * @param parent 父对象
 * @author chiangyang
 */
TranslateOverlayWindow::TranslateOverlayWindow(const QPixmap &pixmap,
                                               const QStringList &texts,
                                               const QStringList &translatedTexts,
                                               QWidget *parent)
    : QWidget(parent)
    , m_pixmap(pixmap)
    , m_texts(texts)
    , m_translatedTexts(translatedTexts)
    , m_viewMode(Translation) {
    LOG_INFO(QString("TranslateOverlayWindow created, pixmap: %1x%2, segments: %3")
                 .arg(pixmap.width()).arg(pixmap.height()).arg(texts.size()));

    // 窗口标志：无边框、置顶、工具窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);

    const TranslatePalette palette = translatePalette();
    m_windowBg = palette.windowBg;
    m_borderColor = palette.border;

    // 初始尺寸：原图宽度 + 右侧文本栏宽度，并限制在屏幕可用区域内
    const int headerHeight = 42;
    const int textPanelWidth = qBound(320, pixmap.width() / 2, 480);
    int windowWidth = pixmap.width() + textPanelWidth;
    int windowHeight = pixmap.height() + headerHeight;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        windowWidth = qMin(windowWidth, int(available.width() * 0.85));
        windowHeight = qMin(windowHeight, int(available.height() * 0.85));
    }
    resize(qMax(640, windowWidth), qMax(400, windowHeight));

    TranslationManager *tm = TranslationManager::instance();

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    // ============ 标题栏 ============
    TranslateTitleBar *titleBar = new TranslateTitleBar(this, this);
    titleBar->setObjectName("translateTitleBar");
    titleBar->setFixedHeight(headerHeight);
    titleBar->setStyleSheet(QString(
        "QWidget#translateTitleBar{background:%1;border-bottom:1px solid %2;}")
        .arg(palette.headerBg.name(), palette.divider.name()));

    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(14, 0, 8, 0);
    titleLayout->setSpacing(8);

    QLabel *titleLabel = new QLabel(tm->get("translate.resultTitle", "Translation"), titleBar);
    titleLabel->setStyleSheet(QString("background:transparent;color:%1;font-size:10pt;font-weight:600;")
                                  .arg(palette.textColor.name()));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    QToolButton *closeButton = new QToolButton(titleBar);
    closeButton->setIcon(StyleManager::loadSvgIcon(":/icons/close.svg", QSize(14, 14), palette.subText));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setFixedSize(28, 28);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip(tm->get("cancel", "Cancel"));
    closeButton->setStyleSheet(
        "QToolButton{background:transparent;border:none;border-radius:4px;}"
        "QToolButton:hover{background:rgba(235, 87, 87, 0.16);}");
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(closeButton, 0, Qt::AlignVCenter);

    rootLayout->addWidget(titleBar);

    // ============ 主体：左侧原图 / 右侧译文 ============
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("translateSplitter");
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet(QString("QSplitter#translateSplitter::handle{background:%1;}")
                                .arg(palette.divider.name()));
    rootLayout->addWidget(splitter, 1);

    m_imageView = new TranslateImageView(pixmap, palette.imageBg, splitter);
    m_imageView->setMinimumWidth(120);
    m_imageView->installEventFilter(this);

    QWidget *textPanel = new QWidget(splitter);
    textPanel->setObjectName("translateTextPanel");
    textPanel->setMinimumWidth(240);
    textPanel->setStyleSheet(QString("QWidget#translateTextPanel{background:%1;}")
                                 .arg(palette.windowBg.name()));

    QVBoxLayout *textLayout = new QVBoxLayout(textPanel);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    m_textView = new QTextEdit(textPanel);
    m_textView->setReadOnly(true);
    m_textView->setFrameShape(QFrame::NoFrame);
    m_textView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_textView->setStyleSheet(QString("QTextEdit{background:%1;border:none;color:%2;font-size:11pt;}")
                                  .arg(palette.windowBg.name(), palette.textColor.name()));
    m_textView->document()->setDocumentMargin(18);
    m_textView->viewport()->installEventFilter(this);
    textLayout->addWidget(m_textView, 1);

    QFrame *separator = new QFrame(textPanel);
    separator->setFixedHeight(1);
    separator->setStyleSheet(QString("background:%1;").arg(palette.divider.name()));
    textLayout->addWidget(separator);

    // 底部操作栏：复制全文 / 另存为图片 / 右下角缩放抓手
    QWidget *actionBar = new QWidget(textPanel);
    actionBar->setStyleSheet(QString("background:%1;").arg(palette.windowBg.name()));
    QHBoxLayout *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(12, 6, 6, 6);
    actionLayout->setSpacing(6);
    actionLayout->addStretch();

    QToolButton *copyButton = createActionButton(
        ":/icons/copy.svg", tm->get("translate.copyAll", "Copy All"), palette, actionBar);
    connect(copyButton, &QToolButton::clicked, this, &TranslateOverlayWindow::copyTranslation);
    actionLayout->addWidget(copyButton);

    QToolButton *saveButton = createActionButton(
        ":/icons/save.svg", tm->get("translate.saveAsImage", "Save as Image"), palette, actionBar);
    connect(saveButton, &QToolButton::clicked, this, &TranslateOverlayWindow::saveAsImage);
    actionLayout->addWidget(saveButton);

    QSizeGrip *sizeGrip = new QSizeGrip(actionBar);
    sizeGrip->setFixedSize(16, 16);
    actionLayout->addWidget(sizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

    textLayout->addWidget(actionBar);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes(QList<int>() << qMax(120, width() - textPanelWidth - 2)
                                    << textPanelWidth);

    // 右侧文本按当前视图模式填充
    updateTextContent();

    // ESC 关闭（无论焦点在窗口还是文本编辑框都生效）
    QShortcut *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, &QWidget::close);
}

/**
 * @brief 设置视图模式
 * @param mode 视图模式
 * @author chiangyang
 */
void TranslateOverlayWindow::setViewMode(ViewMode mode) {
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    updateTextContent();
    LOG_INFO(QString("TranslateOverlayWindow: view mode changed to %1").arg(static_cast<int>(mode)));
}

/**
 * @brief 获取当前视图模式
 * @return 当前视图模式
 * @author chiangyang
 */
TranslateOverlayWindow::ViewMode TranslateOverlayWindow::viewMode() const {
    return m_viewMode;
}

/**
 * @brief 根据当前视图模式刷新右侧文本内容
 * @author chiangyang
 */
void TranslateOverlayWindow::updateTextContent() {
    if (!m_textView) return;

    const TranslatePalette palette = translatePalette();

    switch (m_viewMode) {
    case Original:
        m_textView->setPlainText(m_texts.join("\n\n"));
        break;
    case Both: {
        // 对照模式：按段显示"原文（灰色小字）+ 译文（深色正文）"
        QString html;
        const int count = qMax(m_texts.size(), m_translatedTexts.size());
        for (int i = 0; i < count; ++i) {
            QString original = i < m_texts.size() ? m_texts.at(i) : QString();
            QString translated = i < m_translatedTexts.size() ? m_translatedTexts.at(i) : QString();
            // 先转义 HTML 特殊字符，再把换行替换为 <br/>，避免换行标记被转义
            original = original.toHtmlEscaped();
            original.replace('\n', "<br/>");
            translated = translated.toHtmlEscaped();
            translated.replace('\n', "<br/>");
            if (!original.isEmpty()) {
                html += QString("<p style=\"margin:0 0 4px 0;color:%1;font-size:9pt;\">%2</p>")
                            .arg(palette.subText.name(), original);
            }
            if (!translated.isEmpty()) {
                html += QString("<p style=\"margin:0 0 16px 0;color:%1;font-size:11pt;\">%2</p>")
                            .arg(palette.textColor.name(), translated);
            }
        }
        m_textView->setHtml(html);
        break;
    }
    case Translation:
    default:
        m_textView->setPlainText(m_translatedTexts.join("\n\n"));
        break;
    }
}

/**
 * @brief 绘制事件：绘制窗口背景与边框
 * @param event 绘制事件
 * @author chiangyang
 */
void TranslateOverlayWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_windowBg);
    painter.setPen(QPen(m_borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

/**
 * @brief 右键菜单事件：视图切换、复制、另存、关闭
 * @param event 右键菜单事件
 * @author chiangyang
 */
void TranslateOverlayWindow::contextMenuEvent(QContextMenuEvent *event) {
    showContextMenu(event->globalPos());
}

/**
 * @brief 弹出窗口右键菜单
 * @param globalPos 菜单弹出位置（全局坐标）
 * @author chiangyang
 */
void TranslateOverlayWindow::showContextMenu(const QPoint &globalPos) {
    TranslationManager *tm = TranslationManager::instance();

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(StyleManager::getMenuStyle());

    // 视图模式切换（单选，仅影响右侧文本内容）
    QMenu *viewMenu = menu.addMenu(tm->get("translate.viewMode", "View"));
    QActionGroup *viewGroup = new QActionGroup(&menu);
    viewGroup->setExclusive(true);

    QAction *actOriginal = viewMenu->addAction(tm->get("translate.viewOriginal"));
    actOriginal->setCheckable(true);
    actOriginal->setChecked(m_viewMode == Original);
    viewGroup->addAction(actOriginal);

    QAction *actTranslation = viewMenu->addAction(tm->get("translate.viewTranslation"));
    actTranslation->setCheckable(true);
    actTranslation->setChecked(m_viewMode == Translation);
    viewGroup->addAction(actTranslation);

    QAction *actBoth = viewMenu->addAction(tm->get("translate.viewBoth"));
    actBoth->setCheckable(true);
    actBoth->setChecked(m_viewMode == Both);
    viewGroup->addAction(actBoth);

    connect(actOriginal, &QAction::triggered, this, [this]() { setViewMode(Original); });
    connect(actTranslation, &QAction::triggered, this, [this]() { setViewMode(Translation); });
    connect(actBoth, &QAction::triggered, this, [this]() { setViewMode(Both); });

    menu.addSeparator();

    // 复制原文
    QAction *copyOriginalAction = menu.addAction(tm->get("translate.copyOriginal", "Copy Original"));
    connect(copyOriginalAction, &QAction::triggered, this, &TranslateOverlayWindow::copyOriginal);

    // 复制译文
    QAction *copyTranslationAction = menu.addAction(tm->get("translate.copyTranslation"));
    connect(copyTranslationAction, &QAction::triggered, this, &TranslateOverlayWindow::copyTranslation);

    // 另存为图片
    QAction *saveAction = menu.addAction(tm->get("translate.saveAsImage", "Save as Image"));
    connect(saveAction, &QAction::triggered, this, &TranslateOverlayWindow::saveAsImage);

    menu.addSeparator();

    // 关闭
    QAction *closeAction = menu.addAction(tm->get("cancel"));
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    menu.exec(globalPos);
}

/**
 * @brief 事件过滤器：把原图与文本区域的右键事件转发给本窗口
 * @param watched 被监听的对象（原图控件或文本编辑框 viewport）
 * @param event 事件
 * @return true 表示已处理（仅对 ContextMenu 事件），false 表示继续传递
 * @author chiangyang
 */
bool TranslateOverlayWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ContextMenu
        && (watched == m_imageView
            || (m_textView && watched == m_textView->viewport()))) {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        showContextMenu(contextEvent->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief 复制原文到剪贴板
 * @author chiangyang
 */
void TranslateOverlayWindow::copyOriginal() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_texts.join("\n"));
    LOG_INFO("TranslateOverlayWindow: original text copied to clipboard");
}

/**
 * @brief 复制译文到剪贴板
 * @author chiangyang
 */
void TranslateOverlayWindow::copyTranslation() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_translatedTexts.join("\n"));
    LOG_INFO("TranslateOverlayWindow: translated text copied to clipboard");
}

/**
 * @brief 另存原图为图片文件
 * @author chiangyang
 */
void TranslateOverlayWindow::saveAsImage() {
    const QString defaultName = QString("QuickShot_Translate_%1.png")
                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    TranslationManager *tm = TranslationManager::instance();
    QPixmap result = m_pixmap;
    // 过滤器与 SnipScreen/HistoryWindow 的保存对话框保持一致
    Utils::savePixmapToFile(
        this, [result]() -> QPixmap { return result; }, defaultName,
        tm->get("translate.saveAsImage", "Save as Image"),
        QStringLiteral("PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)"));

    LOG_INFO("TranslateOverlayWindow: save dialog opened");
}
