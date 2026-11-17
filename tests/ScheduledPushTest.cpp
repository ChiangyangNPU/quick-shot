#include <QtTest>
#include <QFile>
#include <QSignalSpy>

#include "pet/ScheduledPushRunner.h"
#include "pet/ScheduledTaskStore.h"

/**
 * @brief 定时推送：星期/时间匹配、网址规范化、仓库读写、调度去重
 * @author chiangyang
 */
class ScheduledPushTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        ScheduledTaskStore::setFilePathForTest(m_tempDir.filePath(QStringLiteral("scheduled_tasks.json")));
    }

    void cleanupTestCase() {
        ScheduledTaskStore::setFilePathForTest(QString());
    }

    void cleanup() {
        // 每个用例从空任务表开始，避免相互影响
        QFile::remove(m_tempDir.filePath(QStringLiteral("scheduled_tasks.json")));
    }

    void weekdayMaskAndMatch() {
        QCOMPARE(ScheduledTaskStore::weekdayMask(1), 1);
        QCOMPARE(ScheduledTaskStore::weekdayMask(7), 1 << 6);
        QCOMPARE(ScheduledTaskStore::weekdayMask(0), 0);
        QCOMPARE(ScheduledTaskStore::weekdayMask(8), 0);

        const int workdays = ScheduledTaskStore::weekdayMask(1) | ScheduledTaskStore::weekdayMask(2)
                             | ScheduledTaskStore::weekdayMask(3) | ScheduledTaskStore::weekdayMask(4)
                             | ScheduledTaskStore::weekdayMask(5);
        QCOMPARE(workdays, 0x1F);
        for (int day = 1; day <= 5; ++day) {
            QVERIFY(ScheduledTaskStore::matchesWeekday(workdays, day));
        }
        QVERIFY(!ScheduledTaskStore::matchesWeekday(workdays, 6));
        QVERIFY(!ScheduledTaskStore::matchesWeekday(workdays, 7));
        QVERIFY(!ScheduledTaskStore::matchesWeekday(0, 1));
    }

    void timeMatch() {
        QVERIFY(ScheduledTaskStore::matchesTime(QStringLiteral("09:30"), QTime(9, 30, 0)));
        QVERIFY(ScheduledTaskStore::matchesTime(QStringLiteral("09:30"), QTime(9, 30, 59)));
        QVERIFY(!ScheduledTaskStore::matchesTime(QStringLiteral("09:30"), QTime(9, 31, 0)));
        QVERIFY(!ScheduledTaskStore::matchesTime(QStringLiteral("09:30"), QTime(10, 30, 0)));
        // 非法格式与无效时间不命中
        QVERIFY(!ScheduledTaskStore::matchesTime(QStringLiteral("9:30"), QTime(9, 30, 0)));
        QVERIFY(!ScheduledTaskStore::matchesTime(QStringLiteral("25:00"), QTime(9, 30, 0)));
        QVERIFY(!ScheduledTaskStore::matchesTime(QStringLiteral("09:30"), QTime()));
    }

    void normalizeUrl_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("bare domain") << "example.com" << "https://example.com";
        QTest::newRow("https kept") << "https://example.com/path?a=1"
                                    << "https://example.com/path?a=1";
        QTest::newRow("http kept") << "http://example.com" << "http://example.com";
        QTest::newRow("trim spaces") << "  example.com  " << "https://example.com";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("ftp rejected") << "ftp://example.com" << "";
    }

    void normalizeUrl() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(ScheduledTaskStore::normalizeUrl(input), expected);
    }

    void storeRoundTrip() {
        ScheduledTaskStore *store = ScheduledTaskStore::instance();
        QSignalSpy changes(store, &ScheduledTaskStore::changed);

        ScheduledTask task;
        task.enabled = true;
        task.days = ScheduledTaskStore::weekdayMask(1) | ScheduledTaskStore::weekdayMask(3);
        task.time = QStringLiteral("08:15");
        task.url = QStringLiteral("example.com");

        const ScheduledTask added = store->add(task);
        QVERIFY(!added.id.isEmpty());
        QCOMPARE(changes.count(), 1);
        QCOMPARE(added.url, QStringLiteral("https://example.com"));

        QVector<ScheduledTask> tasks = store->list();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.at(0).id, added.id);
        QCOMPARE(tasks.at(0).time, QStringLiteral("08:15"));
        QCOMPARE(tasks.at(0).days, task.days);
        QVERIFY(tasks.at(0).enabled);

        ScheduledTask edited = tasks.at(0);
        edited.enabled = false;
        edited.time = QStringLiteral("21:45");
        edited.url = QStringLiteral("https://example.com/news");
        QVERIFY(store->update(edited));
        QCOMPARE(changes.count(), 2);
        tasks = store->list();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.at(0).time, QStringLiteral("21:45"));
        QCOMPARE(tasks.at(0).url, QStringLiteral("https://example.com/news"));
        QVERIFY(!tasks.at(0).enabled);

        QVERIFY(store->remove(added.id));
        QCOMPARE(changes.count(), 3);
        QVERIFY(store->list().isEmpty());
    }

    void storeRejectsInvalidTime() {
        ScheduledTaskStore *store = ScheduledTaskStore::instance();
        ScheduledTask task;
        task.days = ScheduledTaskStore::weekdayMask(1);
        task.time = QStringLiteral("9:30");
        task.url = QStringLiteral("example.com");
        QVERIFY(store->add(task).id.isEmpty());
        QVERIFY(store->list().isEmpty());
    }

    void runnerTriggersOncePerMinute() {
        ScheduledTaskStore *store = ScheduledTaskStore::instance();
        const QDate date(2026, 11, 17);
        const int weekday = date.dayOfWeek();

        ScheduledTask task;
        task.enabled = true;
        task.days = ScheduledTaskStore::weekdayMask(weekday);
        task.time = QStringLiteral("09:30");
        task.url = QStringLiteral("example.com");
        QVERIFY(!store->add(task).id.isEmpty());

        ScheduledPushRunner runner;
        QSignalSpy due(&runner, &ScheduledPushRunner::taskDue);

        runner.tickAt(QDateTime(date, QTime(9, 30, 5)));
        QCOMPARE(due.count(), 1);
        // 同一分钟内重复 tick 不重复触发
        runner.tickAt(QDateTime(date, QTime(9, 30, 40)));
        QCOMPARE(due.count(), 1);
        // 下一分钟不再命中
        runner.tickAt(QDateTime(date, QTime(9, 31, 0)));
        QCOMPARE(due.count(), 1);
    }

    void runnerSkipsDisabledAndOtherDays() {
        ScheduledTaskStore *store = ScheduledTaskStore::instance();
        const QDate date(2026, 11, 17);
        const int weekday = date.dayOfWeek();
        const int otherDay = weekday == 7 ? 1 : weekday + 1;

        ScheduledTask disabled;
        disabled.enabled = false;
        disabled.days = ScheduledTaskStore::weekdayMask(weekday);
        disabled.time = QStringLiteral("09:30");
        disabled.url = QStringLiteral("example.com");
        QVERIFY(!store->add(disabled).id.isEmpty());

        ScheduledTask otherDayTask;
        otherDayTask.enabled = true;
        otherDayTask.days = ScheduledTaskStore::weekdayMask(otherDay);
        otherDayTask.time = QStringLiteral("09:30");
        otherDayTask.url = QStringLiteral("example.com");
        QVERIFY(!store->add(otherDayTask).id.isEmpty());

        ScheduledPushRunner runner;
        QSignalSpy due(&runner, &ScheduledPushRunner::taskDue);
        runner.tickAt(QDateTime(date, QTime(9, 30, 0)));
        QCOMPARE(due.count(), 0);
    }

private:
    QTemporaryDir m_tempDir; ///< 测试数据目录（用例间复用，cleanup 清文件）
};

QTEST_GUILESS_MAIN(ScheduledPushTest)
#include "ScheduledPushTest.moc"
