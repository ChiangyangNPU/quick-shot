#include <QtTest>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>
#include "widgets/settings/SettingsAboutTab.h"
#include "widgets/ElidedLabel.h"
#include "update/UpdateManager.h"

/**
 * @brief 关于选项卡更新流程的高度测量回归测试
 *
 * 复刻 SettingsWindow 的结构（窗口 + QTabWidget + 页面）与其
 * applyTabHeight 的测量方式（布局失效 + 调整页面到 sizeHint 后读高度，
 * 再 setFixedSize 到「页面高度 + 选项卡补偿」），验证更新流程取消下载后
 * 窗口高度能回到空闲态。
 *
 * 原实现两处缺陷（2026-10 取消下载闪退修复时一并发现）：
 * 1. 测量用 layout()->update()（仅异步投递 LayoutRequest，不清几何缓存）
 *    + adjustSize()（读到上一状态残留的缓存高度），取消后测量目标偏大
 *    约 64px（进度条+取消按钮的高度），窗口底部留下一大段空白；
 * 2. 状态文字用 ElidedLabel，其 clear() 未清除自绘缓存的 m_fullText，
 *    取消后「正在下载更新...」仍然显示。
 * @author chiangyang
 */
class AboutTabHeightTest : public QObject {
    Q_OBJECT

    QWidget *m_window = nullptr;
    QTabWidget *m_tabWidget = nullptr;
    SettingsAboutTab *m_aboutTab = nullptr;

    /// 复刻 SettingsWindow::applyTabHeight（无动画路径，含修复）
    int applyTabHeightSim() {
        QWidget *currentTab = m_tabWidget->currentWidget();
        if (!currentTab) return -1;
        // 递归失效窗口内所有层级的布局缓存（与生产修复一致）
        const std::function<void(QWidget *)> invalidateLayouts = [&invalidateLayouts](QWidget *w) {
            if (QLayout *l = w->layout())
                l->invalidate();
            const QList<QObject *> children = w->children();
            for (QObject *child : children) {
                if (auto *childWidget = qobject_cast<QWidget *>(child))
                    invalidateLayouts(childWidget);
            }
        };
        invalidateLayouts(m_window);
        if (currentTab->layout()) {
            currentTab->layout()->invalidate();   // 失效几何缓存
            currentTab->adjustSize();             // 第一次：激活消费旧缓存
            if (currentTab->size() != currentTab->sizeHint())
                currentTab->resize(currentTab->sizeHint()); // 第二次：按新鲜 sizeHint 修正
        }
        int tabHeight = currentTab->size().height();
        if (tabHeight < 0) tabHeight = currentTab->size().height();
        const int tabBarExtra = 50;
        int target = qMin(tabHeight + tabBarExtra, 1280);
        if (m_window->layout())
            m_window->layout()->invalidate();
        m_window->setFixedSize(m_window->width(), target);
        return m_window->height();
    }

private slots:
    void initTestCase() {
        QApplication::setApplicationVersion("0.1.3");
    }

    void cancel_returnsToIdleWindowHeight() {
        m_window = new QWidget;
        m_window->resize(760, 600);
        QVBoxLayout *winLayout = new QVBoxLayout(m_window);
        winLayout->setContentsMargins(0, 0, 0, 0);
        m_tabWidget = new QTabWidget(m_window);
        winLayout->addWidget(m_tabWidget);
        m_aboutTab = new SettingsAboutTab;
        m_tabWidget->addTab(m_aboutTab, QStringLiteral("About"));
        m_window->show();
        QTest::qWait(100); // 激活布局（offscreen 下无 expose 事件）

        applyTabHeightSim();
        const int idleH = m_window->height();

        // 发现新版本（模拟 checkFinished 成功分支）
        UpdateManager::VersionInfo info;
        info.version = "0.1.4";
        info.releaseNotes = QStringLiteral("chore: bump to v0.1.4\n\n**Full Changelog**: https://github.com/chian...");
        UpdateManager::ErrorInfo err;
        QVERIFY(QMetaObject::invokeMethod(m_aboutTab, "onUpdateCheckFinished",
            Q_ARG(bool, true),
            Q_ARG(UpdateManager::VersionInfo, info),
            Q_ARG(UpdateManager::ErrorInfo, err)));
        const int foundH = applyTabHeightSim();
        QVERIFY(foundH > idleH);

        // 下载态：复刻 onDownloadUpdate 的 UI 部分（不发起真实网络请求）
        auto *progress = m_aboutTab->findChild<QProgressBar *>();
        auto *status = m_aboutTab->findChild<ElidedLabel *>();
        QVERIFY(progress);
        QVERIFY(status);
        QPushButton *btnCheck = nullptr;
        QPushButton *btnDownload = nullptr;
        QPushButton *btnCancel = nullptr;
        const QList<QPushButton *> btns = m_aboutTab->findChildren<QPushButton *>();
        for (QPushButton *b : btns) {
            if (b->objectName() == QStringLiteral("primaryButton")) btnCheck = b;
        }
        QVERIFY(btnCheck);
        for (QPushButton *b : btns) {
            if (b == btnCheck) continue;
            if (!b->isHidden()) btnDownload = b;
        }
        QVERIFY(btnDownload);
        for (QPushButton *b : btns) {
            if (b != btnCheck && b != btnDownload) btnCancel = b;
        }
        QVERIFY(btnCancel);

        btnDownload->hide();
        btnCheck->hide();
        progress->show();
        progress->setValue(0);
        btnCancel->show();
        status->setText(QStringLiteral("正在下载更新..."));
        const int downloadH = applyTabHeightSim();
        QVERIFY(downloadH > idleH);

        // 取消：走真实 onCancelUpdate → cancel() + resetUpdateUI
        QVERIFY(QMetaObject::invokeMethod(m_aboutTab, "onCancelUpdate"));
        applyTabHeightSim();
        // 生产实现在高度设置后延迟一拍复核（scheduleHeightRecheck）：
        // 嵌套布局几何在本次设置的激活中才重算，复核用重算后数据收敛
        QTest::qWait(50);
        const int cancelH = applyTabHeightSim();

        // 取消后可见控件与空闲态完全一致：窗口高度回到空闲值（允许 ±4px
        // 取整差：空标签经 setText("") 清空的 sizeHint 重算路径与从未设置
        // 略有出入，视觉不可感知）、状态文字清空
        QVERIFY(qAbs(cancelH - idleH) <= 4);
        QVERIFY(status->text().isEmpty());
    }
};

QTEST_MAIN(AboutTabHeightTest)
#include "AboutTabHeightTest.moc"
