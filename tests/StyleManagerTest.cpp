#include <QtTest>
#include "core/StyleManager.h"

/**
 * @brief StyleManager 颜色元数据表与存取回环的单元测试
 *
 * 重点：数据驱动表（colorSettingTable）的完整性约束，以及用户重构引入的
 * color()/setColor(id) 表驱动存取的回环正确性。
 * @author chiangyang
 */
class StyleManagerTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 元数据表完整性 ----
    void colorSettingTable_sizeMatchesConstant() {
        QCOMPARE(StyleManager::colorSettingTable().size(),
                 static_cast<size_t>(StyleManager::kStyleColorCount));
    }

    void colorSettingTable_settingsKeysUniqueAndNonEmpty() {
        const auto &table = StyleManager::colorSettingTable();
        QSet<QString> seen;
        for (const auto &s : table) {
            QVERIFY2(!QByteArray(s.settingsKey).isEmpty(), "settingsKey 不应为空");
            QVERIFY2(!seen.contains(QByteArray(s.settingsKey)),
                     qPrintable(QString("settingsKey 重复: %1").arg(s.settingsKey)));
            seen.insert(QByteArray(s.settingsKey));
        }
    }

    // ---- color/setColor 回环 ----
    void setColorThenColor_roundtrip() {
        const QColor original = StyleManager::color(StyleManager::StyleColorId::CaptureBorder);
        const QColor custom(12, 34, 56);

        StyleManager::setColor(StyleManager::StyleColorId::CaptureBorder, custom);
        QCOMPARE(StyleManager::color(StyleManager::StyleColorId::CaptureBorder), custom);

        // 恢复默认，避免影响其他用例
        StyleManager::setColor(StyleManager::StyleColorId::CaptureBorder, original);
        QCOMPARE(StyleManager::color(StyleManager::StyleColorId::CaptureBorder), original);
    }

    // ---- resetToDefaults 恢复 ----
    void resetToDefaults_restoresAllColors() {
        // 先把所有颜色改成测试色，再重置，逐项核对回到默认值
        const QColor testColor(1, 2, 3);
        const auto &table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            StyleManager::setColor(static_cast<StyleManager::StyleColorId>(i), testColor);
        }
        StyleManager::resetToDefaults();

        for (size_t i = 0; i < table.size(); ++i) {
            QCOMPARE(StyleManager::color(static_cast<StyleManager::StyleColorId>(i)),
                     table[i].getter());
        }
    }
};

QTEST_MAIN(StyleManagerTest)
#include "StyleManagerTest.moc"
