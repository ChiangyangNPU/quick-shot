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
        // 先把所有颜色改成测试色，再重置，逐项核对回到表内默认色。
        // 注意必须比对 defaultColor：getter() 的实现就是 color(id)，
        // 与左侧相同（恒真断言测不出任何回归）。
        const QColor testColor(1, 2, 3);
        const auto &table = StyleManager::colorSettingTable();
        for (size_t i = 0; i < table.size(); ++i) {
            StyleManager::setColor(static_cast<StyleManager::StyleColorId>(i), testColor);
        }
        StyleManager::resetToDefaults();

        for (size_t i = 0; i < table.size(); ++i) {
            QCOMPARE(StyleManager::color(static_cast<StyleManager::StyleColorId>(i)),
                     table[i].defaultColor);
        }
    }

    // ---- 液态玻璃主题（liquid） ----
    void liquidTheme_setThemeAndMode() {
        const QString original = StyleManager::theme();

        StyleManager::setTheme("liquid");
        QCOMPARE(StyleManager::theme(), QStringLiteral("liquid"));
        QVERIFY(StyleManager::isLiquid());
        // 外观按系统明暗解析：两种取值都合法，且与 systemDark 一致
        const bool expectDark = StyleManager::systemDark();
        QCOMPARE(StyleManager::themeMode() == StyleManager::ThemeMode::Dark, expectDark);

        // 未知值回落浅色（旧版本读到 liquid 之外的非法值的双向兼容路径）
        StyleManager::setTheme("nonsense");
        QCOMPARE(StyleManager::theme(), StyleManager::DEFAULT_THEME);
        QVERIFY(!StyleManager::isLiquid());
        QCOMPARE(StyleManager::themeMode(), StyleManager::ThemeMode::Light);

        StyleManager::setTheme(original);
    }

    void liquidTheme_applyThemeFillsGlassPalette() {
        const QString original = StyleManager::theme();

        StyleManager::applyTheme("liquid", nullptr);
        QCOMPARE(StyleManager::theme(), QStringLiteral("liquid"));
        QVERIFY(StyleManager::isLiquid());

        // 玻璃底色应为半透明（原生虚化可用时低 alpha 透出磨砂）
        const QColor toolbarBg = StyleManager::color(StyleManager::StyleColorId::ToolbarBg);
        QVERIFY2(toolbarBg.alpha() < 255,
                 qPrintable(QStringLiteral("液态玻璃 ToolbarBg 应为半透明，实际 %1")
                                .arg(toolbarBg.name(QColor::HexArgb))));
        // 屏幕叠加物/红色语义不随主题：截屏框保持默认蓝
        QCOMPARE(StyleManager::color(StyleManager::StyleColorId::CaptureBorder),
                 StyleManager::DEFAULT_CAPTURE_BORDER_COLOR);

        // 深浅玻璃文字色二分：深色玻璃近白、浅色玻璃近黑
        if (StyleManager::systemDark()) {
            QCOMPARE(StyleManager::color(StyleManager::StyleColorId::ToolbarText),
                     QColor("#f2f2f2"));
        } else {
            QCOMPARE(StyleManager::color(StyleManager::StyleColorId::ToolbarText),
                     QColor("#1d1d1f"));
        }

        // 切回深色主题后玻璃特征消失（ToolbarBg 回到实色 HUD 底）
        StyleManager::applyTheme("dark", nullptr);
        QVERIFY(!StyleManager::isLiquid());
        StyleManager::applyTheme(original, nullptr);
    }

    void liquidTheme_refreshLiquidPalette_followsAppearance() {
        const QString original = StyleManager::theme();
        StyleManager::applyTheme("liquid", nullptr);

        // refreshLiquidPalette 幂等：重铺后主题标识与玻璃特征保持
        StyleManager::refreshLiquidPalette();
        QCOMPARE(StyleManager::theme(), QStringLiteral("liquid"));
        QVERIFY(StyleManager::color(StyleManager::StyleColorId::ToolbarBg).alpha() < 255);

        StyleManager::applyTheme(original, nullptr);
        // 非液态玻璃主题下 refreshLiquidPalette 不改变色板
        const QColor before = StyleManager::color(StyleManager::StyleColorId::ToolbarBg);
        StyleManager::refreshLiquidPalette();
        QCOMPARE(StyleManager::color(StyleManager::StyleColorId::ToolbarBg), before);
    }
};

QTEST_MAIN(StyleManagerTest)
#include "StyleManagerTest.moc"
