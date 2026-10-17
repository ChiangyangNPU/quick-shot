#include <QtTest>
#include "core/SecretStore.h"
#include "core/ConfigManager.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

/**
 * @brief SecretStore 敏感凭据存储的单元测试
 *
 * 平台后端（Windows DPAPI / macOS Keychain）的存取回环、覆盖写、
 * 空串删除语义、旧明文配置迁移。凭据环境不可用时（CI 无解锁 Keychain
 * 等）以 QSKIP 跳过加密断言，避免环境性误报。
 * 配置经 setTestModeEnabled 隔离，不触碰真实 INI。
 * @author chiangyang
 */
class SecretStoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_tempDir;

    // 探测平台凭据存储是否在本环境可用（不可用则跳过加密断言）
    static bool probeAvailable(const QString &key) {
        if (!SecretStore::storeSecret(key, QStringLiteral("probe"))) {
            return false;
        }
        SecretStore::removeSecret(key);
        return true;
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);  // 隔离真实 INI
        ConfigManager::instance();                 // 初始化测试配置
    }

    void backend_matchesPlatform() {
#ifdef Q_OS_WIN
        QVERIFY(SecretStore::backend() == SecretStore::Backend::WindowsDPAPI);
        QVERIFY(SecretStore::isEncrypted());
#elif defined(Q_OS_MACOS)
        QVERIFY(SecretStore::backend() == SecretStore::Backend::MacKeychain);
        QVERIFY(SecretStore::isEncrypted());
#else
        QVERIFY(SecretStore::backend() == SecretStore::Backend::PlainFallback);
        QVERIFY(!SecretStore::isEncrypted());
#endif
    }

    void roundtrip_overwrite_remove() {
        const QString key = "test/roundtrip";
        if (!probeAvailable(key)) {
            QSKIP("platform credential storage unavailable in this environment");
        }
        QVERIFY(SecretStore::storeSecret(key, QStringLiteral("s3cret-值")));
        QCOMPARE(SecretStore::readSecret(key), QString("s3cret-值"));
        // 覆盖写
        QVERIFY(SecretStore::storeSecret(key, QStringLiteral("second")));
        QCOMPARE(SecretStore::readSecret(key), QString("second"));
        // 删除后读空
        SecretStore::removeSecret(key);
        QVERIFY(SecretStore::readSecret(key).isEmpty());
    }

    void storeEmpty_clearsEntry() {
        const QString key = "test/empty-clears";
        if (!probeAvailable(key)) {
            QSKIP("platform credential storage unavailable in this environment");
        }
        QVERIFY(SecretStore::storeSecret(key, QStringLiteral("v")));
        QVERIFY(SecretStore::storeSecret(key, QStringLiteral("")));  // 空串=删除
        QVERIFY(SecretStore::readSecret(key).isEmpty());
    }

    void migration_movesLegacyPlaintext() {
        const QString key = "translate/deeplKey";
        QSettings legacy(m_tempDir.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue(key, QStringLiteral("legacy-plain"));

        if (!SecretStore::isEncrypted()) {
            // 明文降级后端：迁移保持值可读（等价于原明文路径）
            QCOMPARE(SecretStore::readWithLegacyMigration(&legacy, key),
                     QString("legacy-plain"));
            return;
        }
        if (!probeAvailable(key)) {
            QSKIP("platform credential storage unavailable in this environment");
        }

        QCOMPARE(SecretStore::readWithLegacyMigration(&legacy, key),
                 QString("legacy-plain"));
        // 明文键已迁出，值进入安全存储
        QVERIFY(!legacy.contains(key));
        QCOMPARE(SecretStore::readSecret(key), QString("legacy-plain"));
        SecretStore::removeSecret(key);  // 清理
    }

    void migration_keepsPlaintext_whenStoreFails() {
        // 模拟安全存储写入失败：清除 keychain 项后跳过——DPAPI/Keychain
        // 的失败注入不可行，此用例覆盖明文降级平台（isEncrypted=false）
        if (SecretStore::isEncrypted()) {
            QSKIP("encrypted platform: failure injection not applicable");
        }
        const QString key = "translate/baiduKey";
        QSettings legacy(m_tempDir.filePath("legacy2.ini"), QSettings::IniFormat);
        legacy.setValue(key, QStringLiteral("plain-fallback"));
        QCOMPARE(SecretStore::readWithLegacyMigration(&legacy, key),
                 QString("plain-fallback"));
        QVERIFY(legacy.contains(key));  // 降级平台保留明文（功能优先）
    }

    void unknownKey_readsEmpty() {
        QVERIFY(SecretStore::readSecret(QStringLiteral("test/never-stored-key"))
                    .isEmpty());
    }
};

QTEST_GUILESS_MAIN(SecretStoreTest)
#include "SecretStoreTest.moc"
