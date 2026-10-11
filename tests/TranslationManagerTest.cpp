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
};

QTEST_GUILESS_MAIN(TranslationManagerTest)
#include "TranslationManagerTest.moc"
