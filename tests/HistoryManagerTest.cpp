#include <QtTest>
#include "history/HistoryManager.h"
#include "history/HistoryItem.h"

#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QImage>

/**
 * @brief HistoryManager 的单元测试（内存 SQLite，不触碰真实历史库）
 *
 * setTestModeEnabled 隔离配置目录 + setDatabaseFilePathForTest(":memory:")
 * 隔离数据库；过期测试通过 SQL 回写时间戳（白盒）。覆盖：
 * - 分页（getItems LIMIT/OFFSET 边界与倒序）
 * - 类型过滤
 * - 搜索（LIKE 通配符转义、分页、类型过滤）
 * - 自动清理（保留天数过期 + maxItems 超额删旧）
 * - 单条删除与 id 哨兵、按类型清空
 * @author chiangyang
 */
class HistoryManagerTest : public QObject {
    Q_OBJECT

    HistoryManager *m_mgr = nullptr;
    QTemporaryDir m_tempDir;

    // 插入 n 条剪贴板文本记录（内容带唯一前缀），返回实际插入条数
    int insertTexts(const QString &prefix, int n) {
        int inserted = 0;
        for (int i = 0; i < n; ++i) {
            QTest::qWait(2);  // 保证 timestamp 单调，分页/倒序断言稳定
            if (m_mgr->addClipboardText(QString("%1-%2").arg(prefix).arg(i),
                                        "testSource") > 0) {
                ++inserted;
            }
        }
        return inserted;
    }

    // 白盒：把指定 id 的时间戳回拨 days 天（绕过"只能插入当前时间"的限制）
    void backdate(const QList<qint64> &ids, int days) {
        QSqlDatabase db = QSqlDatabase::database("HistoryDB");
        QVERIFY(db.isValid());
        QSqlQuery q(db);
        q.prepare("UPDATE history_items SET timestamp = :ts WHERE id = :id");
        const QString old = QDateTime::currentDateTime().addDays(-days).toString(Qt::ISODate);
        for (qint64 id : ids) {
            q.bindValue(":ts", old);
            q.bindValue(":id", id);
            QVERIFY(q.exec());
        }
    }

    QStringList contentsOf(const QList<HistoryItem> &items) {
        QStringList out;
        for (const auto &it : items) out << it.content;
        return out;
    }

private slots:
    void initTestCase() {
        // 隔离：配置走测试目录，数据库用进程内 SQLite
        QStandardPaths::setTestModeEnabled(true);
        HistoryManager::setDatabaseFilePathForTest(":memory:");
        m_mgr = HistoryManager::instance();
        QVERIFY(m_mgr != nullptr);
        QCOMPARE(m_mgr->getItemCount(HistoryType::All), 0);
    }

    void init() {
        // 每个用例从确定的空库+默认配置开始：单例成员与测试沙箱配置
        // （~/.qttest）都跨用例、跨运行持久，setMaxItems(3) 之类的写操作
        // 若不归一化会泄漏进后续用例（甚至下一次运行）
        m_mgr->clearAll();
        m_mgr->setMaxItems(1000);
        m_mgr->setRetentionDays(7);
    }

    void pagination_getItemsBoundaries() {
        QCOMPARE(insertTexts("pg", 25), 25);

        // 第一页满 10 条、按时间倒序（最新在前）
        auto page0 = m_mgr->getItems(HistoryType::All, 0, 10);
        QCOMPARE(page0.size(), 10);
        QCOMPARE(page0.first().content, QString("pg-24"));
        QCOMPARE(page0.last().content, QString("pg-15"));

        // 第二页
        auto page1 = m_mgr->getItems(HistoryType::All, 1, 10);
        QCOMPARE(page1.size(), 10);
        QCOMPARE(page1.first().content, QString("pg-14"));

        // 最后一页只剩 5 条；越界页为空
        auto page2 = m_mgr->getItems(HistoryType::All, 2, 10);
        QCOMPARE(page2.size(), 5);
        QCOMPARE(page2.first().content, QString("pg-4"));
        QVERIFY(m_mgr->getItems(HistoryType::All, 3, 10).isEmpty());
    }

    void typeFilter_and_screenshotRecords() {
        // 真实 PNG 走截图路径（含缩略图生成）
        const QString png = m_tempDir.filePath("shot.png");
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(png, "PNG"));
        QVERIFY(m_mgr->addScreenshot(png, "unit-test", QSize(4, 4)) > 0);
        QCOMPARE(insertTexts("tx", 5), 5);

        QCOMPARE(m_mgr->getItemCount(HistoryType::All), 6);
        QCOMPARE(m_mgr->getItemCount(HistoryType::Screenshot), 1);
        QCOMPARE(m_mgr->getItemCount(HistoryType::ClipboardText), 5);

        auto shots = m_mgr->getItems(HistoryType::Screenshot, 0, 10);
        QCOMPARE(shots.size(), 1);
        QCOMPARE(shots.first().type, HistoryType::Screenshot);
        QCOMPARE(shots.first().imageSize, QSize(4, 4));
        QVERIFY(QFile::exists(shots.first().content));  // 原图路径保留
    }

    void search_likeEscaping_andPagination() {
        const QStringList seeds = {"apple pie", "apple juice", "banana",
                                   "100% juice", "snake_case"};
        for (const QString &s : seeds) {
            QVERIFY(m_mgr->addClipboardText(s, "src") > 0);
            QTest::qWait(2);
        }

        // 普通子串
        QCOMPARE(contentsOf(m_mgr->searchItems("apple", HistoryType::All)).size(), 2);
        // LIKE 通配符被转义为字面字符：搜 "%" 只命中 "100% juice"
        QCOMPARE(contentsOf(m_mgr->searchItems("%", HistoryType::All)).size(), 1);
        // 搜 "_" 只命中 "snake_case"（其含字面下划线）
        QCOMPARE(contentsOf(m_mgr->searchItems("_", HistoryType::All)).size(), 1);
        QCOMPARE(contentsOf(m_mgr->searchItems("snake_case", HistoryType::All)).size(), 1);
        // 不存在的关键词
        QVERIFY(m_mgr->searchItems("zzzz", HistoryType::All).isEmpty());

        // 搜索分页
        auto p0 = m_mgr->searchItems("apple", HistoryType::All, 0, 1);
        auto p1 = m_mgr->searchItems("apple", HistoryType::All, 1, 1);
        auto p2 = m_mgr->searchItems("apple", HistoryType::All, 2, 1);
        QCOMPARE(p0.size(), 1);
        QCOMPARE(p1.size(), 1);
        QVERIFY(p0.first().content != p1.first().content);
        QVERIFY(p2.isEmpty());

        // 搜索类型过滤
        QVERIFY(m_mgr->searchItems("apple", HistoryType::Screenshot).isEmpty());
    }

    void cleanup_expiredByRetentionDays() {
        QList<qint64> freshIds, oldIds;
        for (int i = 0; i < 5; ++i) {
            freshIds << m_mgr->addClipboardText(QString("keep-%1").arg(i), "src");
            QTest::qWait(2);
        }
        for (int i = 0; i < 5; ++i) {
            qint64 id = m_mgr->addClipboardText(QString("stale-%1").arg(i), "src");
            oldIds << id;
            QTest::qWait(2);
        }
        backdate(oldIds, 10);  // 回拨 10 天（保留期 7 天）

        m_mgr->cleanupExpired();
        auto remain = contentsOf(m_mgr->getItems(HistoryType::All, 0, 100));
        QVERIFY2(m_mgr->getItemCount(HistoryType::All) == 5,
                 qPrintable(QString("remain %1: %2").arg(remain.size()).arg(remain.join(","))));
        // 剩余的都是 fresh
        QCOMPARE(remain.size(), 5);
        for (const QString &c : remain) QVERIFY(c.startsWith("keep-"));
    }

    void cleanup_trimsBeyondMaxItems_deletingOldest() {
        for (int i = 0; i < 10; ++i) {
            m_mgr->addClipboardText(QString("cap-%1").arg(i), "src");
            QTest::qWait(2);
        }
        m_mgr->setMaxItems(3);
        m_mgr->cleanupExpired();
        QCOMPARE(m_mgr->getItemCount(HistoryType::All), 3);
        // 留下的是最新的 3 条
        auto remain = contentsOf(m_mgr->getItems(HistoryType::All, 0, 10));
        QVERIFY(remain.contains(QString("cap-9")));
        QVERIFY(remain.contains(QString("cap-8")));
        QVERIFY(remain.contains(QString("cap-7")));
        QVERIFY(!remain.contains(QString("cap-0")));
    }

    void removeItem_and_getItemById() {
        qint64 id = m_mgr->addClipboardText("to-remove", "src");
        QVERIFY(id > 0);
        QCOMPARE(m_mgr->getItemById(id).content, QString("to-remove"));
        // 不存在的 id → 哨兵 id=0
        QCOMPARE(m_mgr->getItemById(99999).id, 0);

        QVERIFY(m_mgr->removeItem(id));
        QCOMPARE(m_mgr->getItemCount(HistoryType::All), 0);
        QCOMPARE(m_mgr->getItemById(id).id, 0);
    }

    void clearByType_onlyRemovesTargetType() {
        const QString png = m_tempDir.filePath("shot2.png");
        QVERIFY(QImage(2, 2, QImage::Format_RGB32).save(png, "PNG"));
        QVERIFY(m_mgr->addScreenshot(png, "t", QSize(2, 2)) > 0);
        QCOMPARE(insertTexts("cb", 3), 3);

        m_mgr->clearByType(HistoryType::ClipboardText);
        QCOMPARE(m_mgr->getItemCount(HistoryType::ClipboardText), 0);
        QCOMPARE(m_mgr->getItemCount(HistoryType::Screenshot), 1);

        m_mgr->clearAll();
        QCOMPARE(m_mgr->getItemCount(HistoryType::All), 0);
    }
};

QTEST_MAIN(HistoryManagerTest)
#include "HistoryManagerTest.moc"
