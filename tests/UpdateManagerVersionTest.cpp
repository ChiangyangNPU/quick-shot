#include <QtTest>
#include "update/UpdateManager.h"

/**
 * @brief UpdateManager 版本比较与 release API 解析的单元测试
 *
 * compareVersion 的关键性质：按数字段比较（非字典序）、短版本号补零对齐。
 * parseReleaseApi 的关键性质：tag_name 剥离 v/名称前缀、assets 首项映射、
 * 无数字 tag 解析失败。
 * @author chiangyang
 */
class UpdateManagerVersionTest : public QObject {
    Q_OBJECT

private slots:
    // ---- compareVersion：数字段比较（回归：不能用字典序）----
    void compareVersion_numericNotLexicographic() {
        // 字典序下 "1.2.10" < "1.2.3"（'1'<'3'），数字段比较应为 1.2.10 更大
        QCOMPARE(UpdateManager::compareVersion("1.2.10", "1.2.3"), 1);
        QCOMPARE(UpdateManager::compareVersion("1.2.3", "1.2.10"), -1);
    }

    void compareVersion_shortSegmentPadsZero() {
        // 短版本号按 0 补齐对齐：1.0 与 1.0.0 相等
        QCOMPARE(UpdateManager::compareVersion("1.0", "1.0.0"), 0);
        QCOMPARE(UpdateManager::compareVersion("1.0.1", "1.0"), 1);
        QCOMPARE(UpdateManager::compareVersion("1.0", "1.0.1"), -1);
    }

    void compareVersion_direction() {
        QCOMPARE(UpdateManager::compareVersion("0.9.9", "1.0.0"), -1);
        QCOMPARE(UpdateManager::compareVersion("2.0", "1.9.9"), 1);
        QCOMPARE(UpdateManager::compareVersion("1.2.3", "1.2.3"), 0);
    }

    // ---- isNewerVersion：方向语义 ----
    void isNewerVersion_direction() {
        QVERIFY(UpdateManager::isNewerVersion("1.1.0", "1.0.0"));
        QVERIFY(!UpdateManager::isNewerVersion("1.0.0", "1.1.0"));
        QVERIFY(!UpdateManager::isNewerVersion("1.0.0", "1.0.0"));
    }

    // ---- parseReleaseApi：tag 剥离与字段映射 ----
    void parseReleaseApi_stripsTagPrefix() {
        QJsonObject obj;
        obj.insert("tag_name", "v0.1.1");
        obj.insert("body", "release notes");
        UpdateManager::VersionInfo info;
        QVERIFY(UpdateManager::parseReleaseApi(obj, info));
        QCOMPARE(info.version, QString("0.1.1"));
        QCOMPARE(info.releaseNotes, QString("release notes"));
    }

    void parseReleaseApi_stripsReleasePrefixedTag() {
        // 兼容 "QuickShot-Release-v1.2.3" 等发布命名风格
        QJsonObject obj;
        obj.insert("tag_name", "QuickShot-Release-v1.2.3");
        UpdateManager::VersionInfo info;
        QVERIFY(UpdateManager::parseReleaseApi(obj, info));
        QCOMPARE(info.version, QString("1.2.3"));
    }

    void parseReleaseApi_mapsFirstAsset() {
        QJsonObject asset;
        asset.insert("name", "QuickShot-Release-v0.1.1.dmg");
        asset.insert("browser_download_url", "http://example/download");
        asset.insert("size", 12345);
        QJsonObject obj;
        obj.insert("tag_name", "v0.1.1");
        obj.insert("assets", QJsonArray{asset});

        UpdateManager::VersionInfo info;
        QVERIFY(UpdateManager::parseReleaseApi(obj, info));
        QCOMPARE(info.downloadUrl, QString("http://example/download"));
        QCOMPARE(info.fileSize, qint64(12345));
        // release API 不提供校验和，留空
        QVERIFY(info.checksum.isEmpty());
    }

    void parseReleaseApi_emptyTagFails() {
        // 仓库未发布 release 时 tag_name 为空，应返回 false 且 version 为空
        QJsonObject obj;
        obj.insert("tag_name", "");
        UpdateManager::VersionInfo info;
        QCOMPARE(UpdateManager::parseReleaseApi(obj, info), false);
        QVERIFY(info.version.isEmpty());
    }
};

QTEST_GUILESS_MAIN(UpdateManagerVersionTest)
#include "UpdateManagerVersionTest.moc"
