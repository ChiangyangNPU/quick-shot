#include <QtTest>
#include "core/TranslationManager.h"

/**
 * @brief TranslationManager 翻译键回退行为的单元测试
 *
 * get(key, fallback) 的关键契约：键缺失时返回 fallback（调用方据此
 * 保证新功能文案在语言文件未同步时也有可用输出）。
 * 注：测试环境不依赖具体语言文件的加载结果，只断言与语言无关的回退契约。
 * @author chiangyang
 */
class TranslationManagerTest : public QObject {
    Q_OBJECT

private slots:
    void get_missingKeyReturnsFallback() {
        auto *tm = TranslationManager::instance();
        QCOMPARE(tm->get("test.definitely.missing.key.12345", "回退文案"),
                 QString("回退文案"));
        QCOMPARE(tm->get("test.definitely.missing.key.12345", "fallback text"),
                 QString("fallback text"));
    }

    void get_fallbackWithPlaceholdersKeptVerbatim() {
        // 键缺失时 fallback 原样返回，不做 %1 替换（替换由调用方负责）
        auto *tm = TranslationManager::instance();
        QCOMPARE(tm->get("test.definitely.missing.key.12345", "值: %1"),
                 QString("值: %1"));
    }

    void instance_isSingleton() {
        QVERIFY(TranslationManager::instance() == TranslationManager::instance());
    }

    void loadLanguage_sixLanguagesResolve() {
        // 六语言 JSON 均可从源码树回退路径（tests/../../src/languages）加载，
        // 加载成功后 currentLanguage 跟随最后一次成功加载
        auto *tm = TranslationManager::instance();
        const QStringList langs = {"zh_CN", "zh_TW", "zh_HK", "en_US", "ja_JP", "ko_KR"};
        for (const QString &lang : langs) {
            QVERIFY2(tm->loadLanguage(lang), qPrintable(QString("load %1 failed").arg(lang)));
            QCOMPARE(tm->currentLanguage(), lang);
        }
    }

    void loadLanguage_failureKeepsPreviousLanguage() {
        auto *tm = TranslationManager::instance();
        QVERIFY(tm->loadLanguage("zh_CN"));
        // 不存在的语言加载失败，且不污染当前语言状态
        QVERIFY(!tm->loadLanguage("xx_XX"));
        QCOMPARE(tm->currentLanguage(), QString("zh_CN"));
    }

    void get_newConfigErrorKeysResolveInAllLanguages() {
        // ConfigManager 错误文案词条六语言齐备（缺失时 get 会返回空串，
        // 而非 fallback——fallback 只在键完全缺失时生效，这里显式校验非空）
        auto *tm = TranslationManager::instance();
        const QStringList keys = {"config.fileNotFound", "config.pathEmpty",
                                  "config.readFailed", "config.createDefaultFailed"};
        for (const QString &lang : {"zh_CN", "zh_TW", "zh_HK", "en_US", "ja_JP", "ko_KR"}) {
            QVERIFY(tm->loadLanguage(lang));
            for (const QString &key : keys) {
                QVERIFY2(!tm->get(key).isEmpty(),
                         qPrintable(QString("%1 missing key %2").arg(lang, key)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TranslationManagerTest)
#include "TranslationManagerTest.moc"
