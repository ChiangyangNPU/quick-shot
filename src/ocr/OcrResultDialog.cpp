#include "OcrResultDialog.h"
#include "StyleManager.h"
#include "TranslationManager.h"
#include "TranslateService.h"
#include "ConfigManager.h"
#include "Logger.h"

#include <QClipboard>
#include <QApplication>
#include <QScreen>
#include <QLabel>
#include <QComboBox>
#include "MessageBox.h"
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QTimer>

/**
 * @brief 构造函数
 * @param result OCR 识别结果
 * @param parent 父窗口
 * @author chiangyang
 */
OcrResultDialog::OcrResultDialog(const OcrEngine::OcrResult &result, QWidget *parent)
    : QWidget(parent) {
    LOG_INFO("OcrResultDialog: Creating dialog");

    // 按 Y 坐标合并同行文本：同行用空格，跨行用换行
    if (result.texts.isEmpty()) {
        m_fullText.clear();
    } else if (result.texts.size() == 1) {
        m_fullText = result.texts.first();
    } else {
        // 计算每个区域的中心 Y 坐标
        struct TextItem { QString text; float cy; float cx; };
        QVector<TextItem> items;
        for (int i = 0; i < result.texts.size(); ++i) {
            float cy = 0, cx = 0;
            if (i < result.polygons.size() && !result.polygons[i].isEmpty()) {
                for (const auto &pt : result.polygons[i]) {
                    cx += pt.x();
                    cy += pt.y();
                }
                cx /= result.polygons[i].size();
                cy /= result.polygons[i].size();
            }
            items.append({result.texts[i], cy, cx});
        }

        // 按 Y 排序，同行（Y 差距小于行高阈值）的按 X 排序
        float avgHeight = 0;
        for (int i = 0; i < items.size(); ++i) {
            if (i < result.polygons.size() && result.polygons[i].size() >= 2) {
                float minY = 1e9, maxY = -1e9;
                for (const auto &pt : result.polygons[i]) {
                    minY = qMin(minY, (float)pt.y());
                    maxY = qMax(maxY, (float)pt.y());
                }
                avgHeight += (maxY - minY);
            }
        }
        avgHeight /= qMax(1, (int)items.size());
        float lineThreshold = avgHeight * 0.6f; // 同行判定阈值

        // 简单的行合并：遍历排序后的 items，Y 差距小则同行
        std::sort(items.begin(), items.end(), [](const TextItem &a, const TextItem &b) {
            return a.cy < b.cy || (a.cy == b.cy && a.cx < b.cx);
        });

        QStringList lines;
        QString currentLine = items[0].text;
        float currentY = items[0].cy;

        for (int i = 1; i < items.size(); ++i) {
            if (qAbs(items[i].cy - currentY) < lineThreshold) {
                // 同行，用空格连接
                currentLine += " " + items[i].text;
            } else {
                // 换行
                lines.append(currentLine);
                currentLine = items[i].text;
                currentY = items[i].cy;
            }
        }
        lines.append(currentLine);
        m_fullText = lines.join("\n");
    }

    setupUi();
    retranslateUi();

    // 连接翻译服务的完成/失败信号
    connect(TranslateService::instance(), &TranslateService::finished,
            this, &OcrResultDialog::onTranslateFinished, Qt::UniqueConnection);
    connect(TranslateService::instance(), &TranslateService::failed,
            this, &OcrResultDialog::onTranslateFailed, Qt::UniqueConnection);

    // 设置窗口属性
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowIcon(StyleManager::loadAppIcon());
    setAttribute(Qt::WA_DeleteOnClose);
    resize(500, 400);
    setMinimumSize(500, 400);

    LOG_INFO(QString("OcrResultDialog: Displaying %1 text regions").arg(result.texts.size()));
}

/**
 * @brief 设置 UI 布局
 * @author chiangyang
 */
void OcrResultDialog::setupUi() {
    setMouseTracking(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // 标题标签
    QLabel *titleLabel = new QLabel(this);
    titleLabel->setObjectName("titleLabel");
    titleLabel->setStyleSheet(StyleManager::getOcrResultTitleStyle());
    titleLabel->installEventFilter(this);
    mainLayout->addWidget(titleLabel);

    // 视图模式切换下拉框（原文/译文/对照）
    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->setStyleSheet(StyleManager::getSettingsButtonStyle());
    m_viewModeCombo->installEventFilter(this);
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OcrResultDialog::onViewModeChanged);
    mainLayout->addWidget(m_viewModeCombo);

    // 文本显示区域
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setPlainText(m_fullText);
    m_textEdit->setStyleSheet(StyleManager::getOcrResultTextStyle());
    // 设置字体以确保 QFontMetrics 计算准确
    QFont textFont = m_textEdit->font();
    textFont.setPointSize(14);
    m_textEdit->setFont(textFont);
    m_textEdit->viewport()->installEventFilter(this);
    mainLayout->addWidget(m_textEdit);

    // 按钮布局
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    m_copyButton = new QPushButton(this);
    m_copyButton->setObjectName("copyButton");
    StyleManager::applyActionButtonStyle(m_copyButton);
    m_copyButton->installEventFilter(this);
    connect(m_copyButton, &QPushButton::clicked, this, &OcrResultDialog::copyToClipboard);
    buttonLayout->addWidget(m_copyButton);

    m_translateButton = new QPushButton(this);
    m_translateButton->setObjectName("translateButton");
    StyleManager::applyActionButtonStyle(m_translateButton);
    m_translateButton->installEventFilter(this);
    connect(m_translateButton, &QPushButton::clicked, this, &OcrResultDialog::onTranslate);
    buttonLayout->addWidget(m_translateButton);

    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName("closeButton");
    StyleManager::applyCloseButtonStyle(m_closeButton);
    m_closeButton->installEventFilter(this);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);

    // 应用整体背景样式（使用类型选择器限定作用域，避免子控件如 QMessageBox 继承圆角背景）
    // border-radius 用 em 单位随字体（DPI）缩放
    setStyleSheet(QString("OcrResultDialog { background-color: %1; border-radius: 0.5em; }")
        .arg(StyleManager::getToolbarBgColor().name()));
}

/**
 * @brief 获取指定点的边缘类型
 * @param pos 窗口内坐标
 * @return 边缘类型
 * @author chiangyang
 */
OcrResultDialog::ResizeEdge OcrResultDialog::edgeAt(const QPoint &pos) const {
    int x = pos.x();
    int y = pos.y();
    int w = width();
    int h = height();

    bool nearLeft = x < kEdgeMargin;
    bool nearRight = x > w - kEdgeMargin;
    bool nearTop = y < kEdgeMargin;
    bool nearBottom = y > h - kEdgeMargin;

    if (nearTop && nearLeft) return ResizeEdge::TopLeft;
    if (nearTop && nearRight) return ResizeEdge::TopRight;
    if (nearBottom && nearLeft) return ResizeEdge::BottomLeft;
    if (nearBottom && nearRight) return ResizeEdge::BottomRight;
    if (nearLeft) return ResizeEdge::Left;
    if (nearRight) return ResizeEdge::Right;
    if (nearTop) return ResizeEdge::Top;
    if (nearBottom) return ResizeEdge::Bottom;
    return ResizeEdge::None;
}

/**
 * @brief 根据边缘类型获取对应光标
 * @param edge 边缘类型
 * @return 光标形状
 * @author chiangyang
 */
Qt::CursorShape OcrResultDialog::cursorForEdge(ResizeEdge edge) const {
    switch (edge) {
    case ResizeEdge::Left:
    case ResizeEdge::Right:
        return Qt::SizeHorCursor;
    case ResizeEdge::Top:
    case ResizeEdge::Bottom:
        return Qt::SizeVerCursor;
    case ResizeEdge::TopLeft:
    case ResizeEdge::BottomRight:
        return Qt::SizeFDiagCursor;
    case ResizeEdge::TopRight:
    case ResizeEdge::BottomLeft:
        return Qt::SizeBDiagCursor;
    default:
        return Qt::ArrowCursor;
    }
}

/**
 * @brief 鼠标按下事件，用于开始拖动窗口或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void OcrResultDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->position().toPoint();
        ResizeEdge edge = edgeAt(pos);

        if (edge != ResizeEdge::None) {
            // 开始调整大小
            m_isResizing = true;
            m_resizeEdge = edge;
            m_startGeometry = geometry();
            m_resizeStartPos = event->globalPosition().toPoint();
        } else {
            // 开始拖动窗口
            m_isDragging = true;
            m_dragStartPos = event->globalPosition().toPoint();
            m_widgetStartPos = frameGeometry().topLeft();
        }
    }
}

/**
 * @brief 鼠标移动事件，用于拖动窗口或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void OcrResultDialog::mouseMoveEvent(QMouseEvent *event) {
    if (m_isResizing && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPosition().toPoint() - m_resizeStartPos;
        QRect geo = m_startGeometry;

        switch (m_resizeEdge) {
        case ResizeEdge::Left:
            geo.setLeft(geo.left() + delta.x());
            break;
        case ResizeEdge::Right:
            geo.setRight(geo.right() + delta.x());
            break;
        case ResizeEdge::Top:
            geo.setTop(geo.top() + delta.y());
            break;
        case ResizeEdge::Bottom:
            geo.setBottom(geo.bottom() + delta.y());
            break;
        case ResizeEdge::TopLeft:
            geo.setTopLeft(geo.topLeft() + delta);
            break;
        case ResizeEdge::TopRight:
            geo.setTopRight(geo.topRight() + delta);
            break;
        case ResizeEdge::BottomLeft:
            geo.setBottomLeft(geo.bottomLeft() + delta);
            break;
        case ResizeEdge::BottomRight:
            geo.setBottomRight(geo.bottomRight() + delta);
            break;
        default:
            break;
        }

        // 确保不小于最小尺寸
        if (geo.width() < minimumWidth()) {
            if (m_resizeEdge == ResizeEdge::Left || m_resizeEdge == ResizeEdge::TopLeft || m_resizeEdge == ResizeEdge::BottomLeft)
                geo.setLeft(geo.right() - minimumWidth());
            else
                geo.setRight(geo.left() + minimumWidth());
        }
        if (geo.height() < minimumHeight()) {
            if (m_resizeEdge == ResizeEdge::Top || m_resizeEdge == ResizeEdge::TopLeft || m_resizeEdge == ResizeEdge::TopRight)
                geo.setTop(geo.bottom() - minimumHeight());
            else
                geo.setBottom(geo.top() + minimumHeight());
        }

        setGeometry(geo);
    } else if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_widgetStartPos + delta);
    } else {
        // 更新光标形状
        ResizeEdge edge = edgeAt(event->position().toPoint());
        setCursor(cursorForEdge(edge));
    }
}

/**
 * @brief 鼠标释放事件，结束拖动或调整大小
 * @param event 鼠标事件
 * @author chiangyang
 */
void OcrResultDialog::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        m_isResizing = false;
        m_resizeEdge = ResizeEdge::None;
    }
}

/**
 * @brief 触发翻译
 * @author chiangyang
 */
void OcrResultDialog::onTranslate() {
    // 检查翻译功能是否启用 + 首次隐私提示
    if (!TranslateService::checkEnabledAndPrivacy(this)) {
        return;
    }

    if (m_translating || m_fullText.isEmpty()) {
        return;
    }

    m_translating = true;
    m_translateButton->setEnabled(false);
    TranslationManager *tm = TranslationManager::instance();
    m_translateButton->setText(tm->get("translate.loading", "Translating..."));

    LOG_INFO("OcrResultDialog: translation requested");
    TranslateService::instance()->translate(m_fullText);
}

/**
 * @brief 根据当前视图模式更新文本显示
 * @author chiangyang
 */
void OcrResultDialog::updateView() {
    QString content;
    switch (m_viewMode) {
    case ViewMode::Original:
        content = m_fullText;
        break;
    case ViewMode::Translation:
        content = m_translatedText.isEmpty() ? m_fullText : m_translatedText;
        break;
    case ViewMode::Both:
        if (m_translatedText.isEmpty()) {
            content = m_fullText;
        } else {
            QStringList origLines = m_fullText.split("\n");
            QStringList transLines = m_translatedText.split("\n");
            QStringList out;
            int n = qMax(origLines.size(), transLines.size());
            for (int i = 0; i < n; ++i) {
                QString o = i < origLines.size() ? origLines[i] : QString();
                QString t = i < transLines.size() ? transLines[i] : QString();
                out << (o + "  ->  " + t);
            }
            content = out.join("\n");
        }
        break;
    }
    m_textEdit->setPlainText(content);
}

/**
 * @brief 视图模式下拉框索引改变槽函数
 * @param index 下拉框索引
 * @author chiangyang
 */
void OcrResultDialog::onViewModeChanged(int index) {
    m_viewMode = static_cast<ViewMode>(index);
    updateView();
}

/**
 * @brief 翻译完成槽函数
 * @param original 原文
 * @param translated 译文
 * @author chiangyang
 */
void OcrResultDialog::onTranslateFinished(const QString &original, const QString &translated) {
    Q_UNUSED(original);
    m_translating = false;
    m_translateButton->setEnabled(true);
    m_translatedText = translated;

    // 自动切换到译文视图（屏蔽信号避免重复刷新，手动更新）
    if (m_viewModeCombo) {
        m_viewModeCombo->blockSignals(true);
        m_viewModeCombo->setCurrentIndex(static_cast<int>(ViewMode::Translation));
        m_viewModeCombo->blockSignals(false);
    }
    m_viewMode = ViewMode::Translation;
    updateView();
    retranslateUi(); // 恢复翻译按钮文字
    LOG_INFO("OcrResultDialog: translation finished");
}

/**
 * @brief 翻译失败槽函数
 * @param code 错误分类码，据此显示本地化文案
 * @param detail 原始技术细节，显示在详细信息中
 * @author chiangyang
 */
void OcrResultDialog::onTranslateFailed(TranslateEngine::TranslateError code, const QString &detail) {
    m_translating = false;
    m_translateButton->setEnabled(true);
    retranslateUi();
    TranslationManager *tm = TranslationManager::instance();

    // 根据错误分类码获取本地化文案（统一映射，消除重复 switch-case）
    QString message = TranslateService::errorMessage(code);

    MessageBox msgBox(this);
    msgBox.setType(QMessageBox::Warning);
    msgBox.setContent(tm->get("translate.failed", "Translation Failed"), message);

    // 详细信息按钮（点击后显示原始报错，原文保持不翻译）
    QPushButton *detailButton = nullptr;
    if (!detail.isEmpty()) {
        detailButton = msgBox.addCustomButton(tm->get("translate.showDetails", "Show Details"), QMessageBox::ActionRole);
    }
    QPushButton *okButton = msgBox.addOkButton();
    msgBox.setDefaultButton(okButton);
    msgBox.exec();

    // 用户点击了详细信息按钮，弹出原始报错（保持原文不翻译）
    if (detailButton && msgBox.clickedButton() == detailButton) {
        MessageBox::information(this, tm->get("translate.failed", "Translation Failed"), detail);
    }

    LOG_INFO(QString("OcrResultDialog: translation failed, code=%1 detail=%2")
                 .arg(static_cast<int>(code)).arg(detail));
}

/**
 * @brief 重新翻译用户界面
 * @author chiangyang
 */
void OcrResultDialog::retranslateUi() {
    TranslationManager *tm = TranslationManager::instance();

    QLabel *titleLabel = findChild<QLabel*>("titleLabel");
    if (titleLabel) {
        titleLabel->setText(tm->get("ocr.title", "OCR Result"));
    }
    if (m_copyButton) {
        m_copyButton->setText(tm->get("ocr.copy", "Copy"));
    }
    if (m_closeButton) {
        m_closeButton->setText(tm->get("ocr.close", "Close"));
    }
    if (m_translateButton && !m_translating) {
        m_translateButton->setText(tm->get("ocr.translate", "Translate"));
    }
    if (m_viewModeCombo) {
        int cur = m_viewModeCombo->currentIndex();
        m_viewModeCombo->blockSignals(true);
        m_viewModeCombo->clear();
        m_viewModeCombo->addItem(tm->get("translate.viewOriginal", "Original"));
        m_viewModeCombo->addItem(tm->get("translate.viewTranslation", "Translation"));
        m_viewModeCombo->addItem(tm->get("translate.viewBoth", "Side by Side"));
        m_viewModeCombo->setCurrentIndex(cur < 0 ? 0 : cur);
        m_viewModeCombo->blockSignals(false);
    }
}

/**
 * @brief 复制文本到剪贴板
 * @author chiangyang
 */
void OcrResultDialog::copyToClipboard() {
    // 按当前视图模式复制对应内容
    QString text;
    switch (m_viewMode) {
    case ViewMode::Original:
        text = m_fullText;
        break;
    case ViewMode::Translation:
        text = m_translatedText;
        break;
    case ViewMode::Both:
        text = m_textEdit->toPlainText();
        break;
    }
    if (text.isEmpty()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
        LOG_INFO("OcrResultDialog: Text copied to clipboard");

        // 临时改变按钮文本提示复制成功
        if (m_copyButton) {
            TranslationManager *tm = TranslationManager::instance();
            QString origText = tm->get("ocr.copy", "Copy");
            m_copyButton->setText(tm->get("ocr.copied", "Copied!"));
            QTimer::singleShot(1500, this, [this, origText]() {
                if (m_copyButton) {
                    m_copyButton->setText(origText);
                }
            });
        }
    }
}

/**
 * @brief 事件过滤器，处理子控件上的鼠标移动事件
 * @param obj 监听对象
 * @param event 事件
 * @return 是否拦截事件
 * @author chiangyang
 */
bool OcrResultDialog::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->position().toPoint();

        // 将子控件坐标转换为窗口坐标
        QWidget *widget = qobject_cast<QWidget*>(obj);
        if (widget) {
            QPoint windowPos = widget->mapTo(this, pos);

            // 如果正在拖动或调整大小，继续处理
            if ((m_isResizing || m_isDragging) && (mouseEvent->buttons() & Qt::LeftButton)) {
                if (m_isResizing) {
                    QPoint delta = mouseEvent->globalPosition().toPoint() - m_resizeStartPos;
                    QRect geo = m_startGeometry;

                    switch (m_resizeEdge) {
                    case ResizeEdge::Left: geo.setLeft(geo.left() + delta.x()); break;
                    case ResizeEdge::Right: geo.setRight(geo.right() + delta.x()); break;
                    case ResizeEdge::Top: geo.setTop(geo.top() + delta.y()); break;
                    case ResizeEdge::Bottom: geo.setBottom(geo.bottom() + delta.y()); break;
                    case ResizeEdge::TopLeft: geo.setTopLeft(geo.topLeft() + delta); break;
                    case ResizeEdge::TopRight: geo.setTopRight(geo.topRight() + delta); break;
                    case ResizeEdge::BottomLeft: geo.setBottomLeft(geo.bottomLeft() + delta); break;
                    case ResizeEdge::BottomRight: geo.setBottomRight(geo.bottomRight() + delta); break;
                    default: break;
                    }

                    // 确保不小于最小尺寸
                    if (geo.width() < minimumWidth()) {
                        if (m_resizeEdge == ResizeEdge::Left || m_resizeEdge == ResizeEdge::TopLeft || m_resizeEdge == ResizeEdge::BottomLeft)
                            geo.setLeft(geo.right() - minimumWidth());
                        else
                            geo.setRight(geo.left() + minimumWidth());
                    }
                    if (geo.height() < minimumHeight()) {
                        if (m_resizeEdge == ResizeEdge::Top || m_resizeEdge == ResizeEdge::TopLeft || m_resizeEdge == ResizeEdge::TopRight)
                            geo.setTop(geo.bottom() - minimumHeight());
                        else
                            geo.setBottom(geo.top() + minimumHeight());
                    }

                    setGeometry(geo);
                } else if (m_isDragging) {
                    QPoint delta = mouseEvent->globalPosition().toPoint() - m_dragStartPos;
                    move(m_widgetStartPos + delta);
                }

                return true; // 拦截事件
            }

            // 更新光标形状
            ResizeEdge edge = edgeAt(windowPos);
            setCursor(cursorForEdge(edge));
        }
    } else if (event->type() == QEvent::Leave && !m_isResizing && !m_isDragging) {
        setCursor(Qt::ArrowCursor);
    } else if (event->type() == QEvent::MouseButtonRelease) {
        m_isDragging = false;
        m_isResizing = false;
        m_resizeEdge = ResizeEdge::None;
    }

    return QWidget::eventFilter(obj, event);
}
