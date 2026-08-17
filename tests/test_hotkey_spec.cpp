// グローバルホットキーの表記と解釈 (Phase 4)。
//
// Win32 の RegisterHotKey には触れない — MOD_* / 仮想キーへの写像は
// app/GlobalHotkey.cpp にあり、ここで見るのは「INI の文字列をどう読むか」だけ。
//
// 固定したいのは:
//   - 往復 (parse → format → parse) で崩れない
//   - 修飾なしを拒否する (OS 全体のキーを 1 つ奪ってしまう)
//   - 壊れた綴りを黙って別のホットキーに解釈しない
#include <QtTest>

#include "app/HotkeySpec.h"

class TestHotkeySpec : public QObject {
    Q_OBJECT

private slots:
    void defaultIsValid();
    void parsesModifiersAndKey_data();
    void parsesModifiersAndKey();
    void parseIsCaseInsensitiveAndAcceptsAliases_data();
    void parseIsCaseInsensitiveAndAcceptsAliases();
    void rejectsInvalid_data();
    void rejectsInvalid();
    void modifierOnlyIsRejected_data();
    void modifierOnlyIsRejected();
    void emptyTokensAreRejected_data();
    void emptyTokensAreRejected();
    void duplicateModifiersAreRejected_data();
    void duplicateModifiersAreRejected();
    void formatIsNormalized_data();
    void formatIsNormalized();
    void roundTrip_data();
    void roundTrip();
    void parserDoesNotModifyInput();
};

void TestHotkeySpec::defaultIsValid()
{
    // 既定が解釈できないと、初回起動でホットキーが黙って無効になる。
    const efs::HotkeySpec spec = efs::parseHotkey(efs::defaultHotkeyText());
    QVERIFY(spec.isValid());
    QCOMPARE(efs::formatHotkey(spec), efs::defaultHotkeyText());
}

void TestHotkeySpec::parsesModifiersAndKey_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<quint32>("modifiers");
    QTest::addColumn<quint32>("key");

    QTest::newRow("Ctrl+Alt+E") << QStringLiteral("Ctrl+Alt+E")
                                << quint32(efs::HotkeyModControl | efs::HotkeyModAlt)
                                << quint32(Qt::Key_E);
    QTest::newRow("Ctrl+Space") << QStringLiteral("Ctrl+Space") << quint32(efs::HotkeyModControl)
                                << quint32(Qt::Key_Space);
    QTest::newRow("Ctrl+Shift+F12")
        << QStringLiteral("Ctrl+Shift+F12") << quint32(efs::HotkeyModControl | efs::HotkeyModShift)
        << quint32(Qt::Key_F12);
    QTest::newRow("Meta+0") << QStringLiteral("Meta+0") << quint32(efs::HotkeyModMeta)
                            << quint32(Qt::Key_0);
    QTest::newRow("all modifiers") << QStringLiteral("Ctrl+Alt+Shift+Meta+A")
                                   << quint32(efs::HotkeyModControl | efs::HotkeyModAlt |
                                              efs::HotkeyModShift | efs::HotkeyModMeta)
                                   << quint32(Qt::Key_A);
    QTest::newRow("F24 (上限)") << QStringLiteral("Alt+F24") << quint32(efs::HotkeyModAlt)
                                << quint32(Qt::Key_F1 + 23);
}

void TestHotkeySpec::parsesModifiersAndKey()
{
    QFETCH(QString, text);
    QFETCH(quint32, modifiers);
    QFETCH(quint32, key);

    const efs::HotkeySpec spec = efs::parseHotkey(text);
    QVERIFY(spec.isValid());
    QCOMPARE(spec.modifiers, modifiers);
    QCOMPARE(spec.key, key);
}

// INI は手編集できる前提なので、綴りの揺れは受ける。
void TestHotkeySpec::parseIsCaseInsensitiveAndAcceptsAliases_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("lowercase") << QStringLiteral("ctrl+alt+e");
    QTest::newRow("uppercase") << QStringLiteral("CTRL+ALT+E");
    QTest::newRow("Control alias") << QStringLiteral("Control+Alt+E");
    QTest::newRow("spaces around +") << QStringLiteral(" Ctrl + Alt + E ");
}

void TestHotkeySpec::parseIsCaseInsensitiveAndAcceptsAliases()
{
    QFETCH(QString, text);
    // どう綴っても同じ 1 つのホットキーへ落ちること。
    QCOMPARE(efs::formatHotkey(efs::parseHotkey(text)), QStringLiteral("Ctrl+Alt+E"));
}

// 壊れた指定を黙って別のホットキーへ解釈しない。invalid として返す。
void TestHotkeySpec::rejectsInvalid_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << QString();
    QTest::newRow("whitespace") << QStringLiteral("   ");
    QTest::newRow("unknown modifier") << QStringLiteral("Hyper+E");
    QTest::newRow("unknown key") << QStringLiteral("Ctrl+Alt+Escape");
    QTest::newRow("two keys") << QStringLiteral("Ctrl+A+B");
    QTest::newRow("F0") << QStringLiteral("Ctrl+F0");
    QTest::newRow("F25 (上限超え)") << QStringLiteral("Ctrl+F25");
    QTest::newRow("punctuation") << QStringLiteral("Ctrl+;");
    QTest::newRow("trailing +") << QStringLiteral("Ctrl+Alt+");
    QTest::newRow("garbage") << QStringLiteral("not a hotkey");
}

void TestHotkeySpec::rejectsInvalid()
{
    QFETCH(QString, text);
    const efs::HotkeySpec spec = efs::parseHotkey(text);
    QVERIFY(!spec.isValid());
    QVERIFY(efs::formatHotkey(spec).isEmpty());
}

// 修飾キー無しのホットキーは OS 全体でそのキーを奪ってしまうので拒否する。
void TestHotkeySpec::modifierOnlyIsRejected_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("bare letter") << QStringLiteral("E");
    QTest::newRow("bare function key") << QStringLiteral("F1");
    QTest::newRow("modifiers only") << QStringLiteral("Ctrl+Alt");
    QTest::newRow("single modifier") << QStringLiteral("Ctrl");
}

void TestHotkeySpec::modifierOnlyIsRejected()
{
    QFETCH(QString, text);
    QVERIFY(!efs::parseHotkey(text).isValid());
}

// `+` の連続や先頭・末尾の `+` は打ち間違い。空の項を読み飛ばして正しい綴りと
// 同じに解釈すると、壊れた INI が黙って動いてしまうので invalid にする。
void TestHotkeySpec::emptyTokensAreRejected_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("double + (中)") << QStringLiteral("Ctrl++Alt+E");
    QTest::newRow("double + (キーの前)") << QStringLiteral("Ctrl+Alt++E");
    QTest::newRow("leading +") << QStringLiteral("+Ctrl+Alt+E");
    QTest::newRow("trailing +") << QStringLiteral("Ctrl+Alt+E+");
    QTest::newRow("+ だけ") << QStringLiteral("+");
    QTest::newRow("+ の連続だけ") << QStringLiteral("+++");
    QTest::newRow("空白だけの項") << QStringLiteral("Ctrl+ +E");
}

void TestHotkeySpec::emptyTokensAreRejected()
{
    QFETCH(QString, text);
    QVERIFY(!efs::parseHotkey(text).isValid());
}

// 同じ修飾キーの重複も打ち間違い。黙って 1 つに畳まない。
void TestHotkeySpec::duplicateModifiersAreRejected_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("Ctrl 2 回") << QStringLiteral("Ctrl+Ctrl+E");
    QTest::newRow("別名で 2 回") << QStringLiteral("Ctrl+Control+E");
    QTest::newRow("別名で 2 回 (大小混在)") << QStringLiteral("control+CTRL+E");
    QTest::newRow("Alt 2 回") << QStringLiteral("Alt+Alt+F1");
    QTest::newRow("Meta と Win") << QStringLiteral("Meta+Win+K");
    QTest::newRow("3 個の途中で重複") << QStringLiteral("Ctrl+Alt+Ctrl+E");
}

void TestHotkeySpec::duplicateModifiersAreRejected()
{
    QFETCH(QString, text);
    QVERIFY(!efs::parseHotkey(text).isValid());
}

// 出力の並びは常に Ctrl+Alt+Shift+Meta+<key>。INI の見た目が入力順で揺れない。
void TestHotkeySpec::formatIsNormalized_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("reordered") << QStringLiteral("Alt+Ctrl+E") << QStringLiteral("Ctrl+Alt+E");
    QTest::newRow("Win alias") << QStringLiteral("Win+Shift+K") << QStringLiteral("Shift+Meta+K");
    QTest::newRow("lowercase key") << QStringLiteral("Ctrl+Alt+e") << QStringLiteral("Ctrl+Alt+E");
}

void TestHotkeySpec::formatIsNormalized()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(efs::formatHotkey(efs::parseHotkey(text)), expected);
}

void TestHotkeySpec::roundTrip_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("Ctrl+Alt+E") << QStringLiteral("Ctrl+Alt+E");
    QTest::newRow("Ctrl+Space") << QStringLiteral("Ctrl+Space");
    QTest::newRow("Shift+Meta+F7") << QStringLiteral("Shift+Meta+F7");
    QTest::newRow("Ctrl+Alt+Shift+Meta+9") << QStringLiteral("Ctrl+Alt+Shift+Meta+9");
    QTest::newRow("Alt+F1") << QStringLiteral("Alt+F1");
    QTest::newRow("Alt+F24") << QStringLiteral("Alt+F24");
}

void TestHotkeySpec::roundTrip()
{
    QFETCH(QString, text);

    const efs::HotkeySpec first = efs::parseHotkey(text);
    QVERIFY(first.isValid());
    const QString formatted = efs::formatHotkey(first);
    QCOMPARE(formatted, text);

    // もう 1 往復しても変わらない。
    const efs::HotkeySpec second = efs::parseHotkey(formatted);
    QCOMPARE(second.modifiers, first.modifiers);
    QCOMPARE(second.key, first.key);
    QCOMPARE(efs::formatHotkey(second), formatted);
}

// 解釈するだけ。渡した文字列は書き換えない。
void TestHotkeySpec::parserDoesNotModifyInput()
{
    QString text = QStringLiteral(" ctrl + alt + e ");
    const QString original = text;
    const efs::HotkeySpec spec = efs::parseHotkey(text);
    QVERIFY(spec.isValid());
    QCOMPARE(text, original);
}

QTEST_GUILESS_MAIN(TestHotkeySpec)
#include "test_hotkey_spec.moc"
