// 設定の永続化 (Phase 3 / F9)。
//
// 実ユーザーの %APPDATA%\efs\efs.ini を壊さないよう、QStandardPaths のテスト
// モードで書き込み先を隔離する。各テストの前に必ず clear() して「保存されて
// いない状態」から始める。
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

#include "app/Settings.h"

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void defaultsWhenNothingSaved();
    void roundTrip();
    void themeRoundTrip_data();
    void themeRoundTrip();
    void kindRoundTrip_data();
    void kindRoundTrip();
    void sortKeyRoundTrip_data();
    void sortKeyRoundTrip();
    void sortOrderRoundTrip_data();
    void sortOrderRoundTrip();
    void corruptedEnumsFallBackToDefaults();
    void unknownSchemaVersionFallsBackToDefaults();
    void corruptedGeometryAndHeaderStateDoNotCrash();
    void enumsAreStoredAsStableStrings();
    void searchTextIsNotPersisted();
};

Q_DECLARE_METATYPE(efs::ThemeMode)
Q_DECLARE_METATYPE(efs::FileKind)
Q_DECLARE_METATYPE(efs::SortKey)
Q_DECLARE_METATYPE(efs::SortOrder)

void TestSettings::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication::setOrganizationName(QStringLiteral("efs-test"));
    QCoreApplication::setApplicationName(QStringLiteral("efs-test"));
}

void TestSettings::init()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

// 初回起動。Dark / All / Regex OFF / Name 昇順。
void TestSettings::defaultsWhenNothingSaved()
{
    const efs::Settings loaded = efs::Settings::load();

    QCOMPARE(loaded.theme, efs::ThemeMode::Dark);
    QCOMPARE(loaded.options.kind, efs::FileKind::All);
    QCOMPARE(loaded.options.regex, false);
    QCOMPARE(loaded.options.sortKey, efs::SortKey::Name);
    QCOMPARE(loaded.options.sortOrder, efs::SortOrder::Asc);
    QVERIFY(loaded.windowGeometry.isEmpty());
    QVERIFY(loaded.windowState.isEmpty());
    QVERIFY(loaded.headerState.isEmpty());
}

void TestSettings::roundTrip()
{
    efs::Settings saved;
    saved.theme = efs::ThemeMode::Light;
    saved.options.kind = efs::FileKind::Video;
    saved.options.regex = true;
    saved.options.sortKey = efs::SortKey::Size;
    saved.options.sortOrder = efs::SortOrder::Desc;
    saved.windowGeometry = QByteArrayLiteral("\x01\x02geometry");
    saved.windowState = QByteArrayLiteral("\x03\x04state");
    saved.headerState = QByteArrayLiteral("\x05\x06header");
    saved.save();

    const efs::Settings loaded = efs::Settings::load();

    QCOMPARE(loaded.theme, saved.theme);
    QCOMPARE(loaded.options.kind, saved.options.kind);
    QCOMPARE(loaded.options.regex, saved.options.regex);
    QCOMPARE(loaded.options.sortKey, saved.options.sortKey);
    QCOMPARE(loaded.options.sortOrder, saved.options.sortOrder);
    QCOMPARE(loaded.windowGeometry, saved.windowGeometry);
    QCOMPARE(loaded.windowState, saved.windowState);
    QCOMPARE(loaded.headerState, saved.headerState);
}

void TestSettings::themeRoundTrip_data()
{
    QTest::addColumn<efs::ThemeMode>("value");
    QTest::newRow("System") << efs::ThemeMode::System;
    QTest::newRow("Dark") << efs::ThemeMode::Dark;
    QTest::newRow("Light") << efs::ThemeMode::Light;
}

void TestSettings::themeRoundTrip()
{
    QFETCH(efs::ThemeMode, value);
    efs::Settings saved;
    saved.theme = value;
    saved.save();
    QCOMPARE(efs::Settings::load().theme, value);
}

void TestSettings::kindRoundTrip_data()
{
    QTest::addColumn<efs::FileKind>("value");
    QTest::newRow("All") << efs::FileKind::All;
    QTest::newRow("Image") << efs::FileKind::Image;
    QTest::newRow("Video") << efs::FileKind::Video;
    QTest::newRow("Audio") << efs::FileKind::Audio;
    QTest::newRow("Document") << efs::FileKind::Document;
    QTest::newRow("Directory") << efs::FileKind::Directory;
}

void TestSettings::kindRoundTrip()
{
    QFETCH(efs::FileKind, value);
    efs::Settings saved;
    saved.options.kind = value;
    saved.save();
    QCOMPARE(efs::Settings::load().options.kind, value);
}

void TestSettings::sortKeyRoundTrip_data()
{
    QTest::addColumn<efs::SortKey>("value");
    QTest::newRow("Name") << efs::SortKey::Name;
    QTest::newRow("Path") << efs::SortKey::Path;
    QTest::newRow("Size") << efs::SortKey::Size;
    QTest::newRow("DateModified") << efs::SortKey::DateModified;
}

void TestSettings::sortKeyRoundTrip()
{
    QFETCH(efs::SortKey, value);
    efs::Settings saved;
    saved.options.sortKey = value;
    saved.save();
    QCOMPARE(efs::Settings::load().options.sortKey, value);
}

void TestSettings::sortOrderRoundTrip_data()
{
    QTest::addColumn<efs::SortOrder>("value");
    QTest::newRow("Asc") << efs::SortOrder::Asc;
    QTest::newRow("Desc") << efs::SortOrder::Desc;
}

void TestSettings::sortOrderRoundTrip()
{
    QFETCH(efs::SortOrder, value);
    efs::Settings saved;
    saved.options.sortOrder = value;
    saved.save();
    QCOMPARE(efs::Settings::load().options.sortOrder, value);
}

// 手編集で壊れた / 将来消えた列挙子が残っている INI。既定値へ戻ること。
void TestSettings::corruptedEnumsFallBackToDefaults()
{
    {
        QSettings settings;
        settings.setValue(QStringLiteral("settingsVersion"), 1);
        settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("neon"));
        settings.setValue(QStringLiteral("search/kind"), QStringLiteral("spreadsheet"));
        settings.setValue(QStringLiteral("search/sortKey"), QStringLiteral("relevance"));
        settings.setValue(QStringLiteral("search/sortOrder"), QStringLiteral("sideways"));
        settings.setValue(QStringLiteral("search/regex"), QStringLiteral("maybe"));
        settings.sync();
    }

    const efs::Settings loaded = efs::Settings::load();
    QCOMPARE(loaded.theme, efs::ThemeMode::Dark);
    QCOMPARE(loaded.options.kind, efs::FileKind::All);
    QCOMPARE(loaded.options.sortKey, efs::SortKey::Name);
    QCOMPARE(loaded.options.sortOrder, efs::SortOrder::Asc);
    QCOMPARE(loaded.options.regex, false);
}

void TestSettings::unknownSchemaVersionFallsBackToDefaults()
{
    {
        QSettings settings;
        settings.setValue(QStringLiteral("settingsVersion"), 99);
        settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("light"));
        settings.setValue(QStringLiteral("search/kind"), QStringLiteral("video"));
        settings.sync();
    }

    // 読めないスキーマは丸ごと既定値。汎用 migration framework は作らない。
    const efs::Settings loaded = efs::Settings::load();
    QCOMPARE(loaded.theme, efs::ThemeMode::Dark);
    QCOMPARE(loaded.options.kind, efs::FileKind::All);
}

// ジオメトリ / ヘッダ状態が壊れていても load は落ちない。実際の復元は Qt の
// restoreGeometry / restoreState が false を返すだけで、既定値が残る。
void TestSettings::corruptedGeometryAndHeaderStateDoNotCrash()
{
    {
        QSettings settings;
        settings.setValue(QStringLiteral("settingsVersion"), 1);
        settings.setValue(QStringLiteral("window/geometry"), QStringLiteral("not-a-byte-array"));
        settings.setValue(QStringLiteral("window/headerState"), 12345);
        settings.setValue(QStringLiteral("window/state"),
                          QByteArrayLiteral("\xde\xad\xbe\xef garbage"));
        settings.sync();
    }

    const efs::Settings loaded = efs::Settings::load();
    // 中身は問わない。落ちないことと、検索オプションが既定へ戻ることだけ見る。
    QCOMPARE(loaded.options.kind, efs::FileKind::All);
    QCOMPARE(loaded.theme, efs::ThemeMode::Dark);
}

// enum は int の生値ではなく安定した文字列で書く。列挙子の順序を入れ替えても
// 既存の INI が黙って別の意味にならないこと。
void TestSettings::enumsAreStoredAsStableStrings()
{
    efs::Settings saved;
    saved.theme = efs::ThemeMode::Light;
    saved.options.kind = efs::FileKind::Image;
    saved.options.sortKey = efs::SortKey::DateModified;
    saved.options.sortOrder = efs::SortOrder::Desc;
    saved.save();

    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("light"));
    QCOMPARE(settings.value(QStringLiteral("search/kind")).toString(), QStringLiteral("image"));
    QCOMPARE(settings.value(QStringLiteral("search/sortKey")).toString(),
             QStringLiteral("dateModified"));
    QCOMPARE(settings.value(QStringLiteral("search/sortOrder")).toString(), QStringLiteral("desc"));
}

// 検索文字列 / 検索履歴 / 結果行は保存しない (search history と意味が混ざるため
// Phase 3 では行わない、という確定事項)。Settings に置き場所すら無いことを、
// 保存後の INI のキー一覧で固定する。
void TestSettings::searchTextIsNotPersisted()
{
    efs::Settings saved;
    saved.options.kind = efs::FileKind::Document;
    saved.save();

    QSettings settings;
    const QStringList keys = settings.allKeys();
    const QStringList expected{
        QStringLiteral("settingsVersion"), QStringLiteral("appearance/theme"),
        QStringLiteral("search/kind"),     QStringLiteral("search/regex"),
        QStringLiteral("search/sortKey"),  QStringLiteral("search/sortOrder"),
        QStringLiteral("window/geometry"), QStringLiteral("window/headerState"),
        QStringLiteral("window/state"),
    };

    QStringList sorted = keys;
    sorted.sort();
    QStringList sortedExpected = expected;
    sortedExpected.sort();
    QCOMPARE(sorted, sortedExpected);

    for (const QString& key : keys) {
        QVERIFY2(!key.contains(QStringLiteral("text"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("検索文字列らしきキーが保存されている: %1").arg(key)));
        QVERIFY2(!key.contains(QStringLiteral("history"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("検索履歴らしきキーが保存されている: %1").arg(key)));
        QVERIFY2(
            !key.contains(QStringLiteral("recent"), Qt::CaseInsensitive),
            qPrintable(QStringLiteral("最近の query らしきキーが保存されている: %1").arg(key)));
    }
}

QTEST_GUILESS_MAIN(TestSettings)
#include "test_settings.moc"
