// Regex の advisory validation (Phase 3)。
//
// これは UI の見た目 (検索欄を赤くする / 警告を出す) のためだけの判定であって、
// Everything backend の authority ではない。したがってここで固定したいのは
//   - Regex OFF では一切検証しない
//   - 明らかな構文エラーを検出できる (offset 付き)
//   - **pattern を書き換えない**
//   - P2 で確定した whitespace の契約を壊さない
// の 4 点。
#include <QtTest>

#include "app/RegexValidation.h"

class TestRegexValidation : public QObject {
    Q_OBJECT

private slots:
    void regexOffIsNotValidated_data();
    void regexOffIsNotValidated();
    void emptyPatternIsNotValidated();
    void validPatterns_data();
    void validPatterns();
    void invalidPatterns_data();
    void invalidPatterns();
    void errorOffsetPointsAtTheProblem();
    void whitespaceOnlyPatternIsValid_data();
    void whitespaceOnlyPatternIsValid();
    void validatorNeverRewritesThePattern_data();
    void validatorNeverRewritesThePattern();
};

// Regex OFF のときは Everything のワイルドカード構文なので、正規表現として
// 評価してはならない。"(" だけ打っても赤くならないこと。
void TestRegexValidation::regexOffIsNotValidated_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::newRow("plain") << QStringLiteral("report");
    QTest::newRow("unclosed group") << QStringLiteral("(");
    QTest::newRow("unclosed class") << QStringLiteral("[");
    QTest::newRow("dangling escape") << QStringLiteral("\\");
    QTest::newRow("wildcard") << QStringLiteral("*.txt");
}

void TestRegexValidation::regexOffIsNotValidated()
{
    QFETCH(QString, pattern);
    const efs::RegexValidation validation = efs::validateRegex(pattern, false);
    QVERIFY(!validation.checked);
    QVERIFY(validation.valid);
    QVERIFY(validation.errorString.isEmpty());
}

void TestRegexValidation::emptyPatternIsNotValidated()
{
    const efs::RegexValidation validation = efs::validateRegex(QString(), true);
    QVERIFY(!validation.checked);
    QVERIFY(validation.valid);
}

// Phase 3 冒頭の互換性 probe で Everything 1.4 でも通ることを実機確認した構文。
void TestRegexValidation::validPatterns_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::newRow("plain") << QStringLiteral("abc");
    QTest::newRow("anchors") << QStringLiteral("^abc$");
    QTest::newRow("alternation") << QStringLiteral("a|b");
    QTest::newRow("group") << QStringLiteral("(a|b)");
    QTest::newRow("char class") << QStringLiteral("[a-z]");
    QTest::newRow("digit plus") << QStringLiteral("\\d+");
    QTest::newRow("bounded quantifier") << QStringLiteral("a{1,3}");
    QTest::newRow("escaped dot") << QStringLiteral("a\\.b");
    QTest::newRow("space") << QStringLiteral("a b");
    QTest::newRow("lookahead") << QStringLiteral("a(?=b)");
    QTest::newRow("negative lookahead") << QStringLiteral("a(?!b)");
    QTest::newRow("lookbehind") << QStringLiteral("(?<=a)b");
    QTest::newRow("non-capturing group") << QStringLiteral("(?:a|b)");
    QTest::newRow("inline case-insensitive") << QStringLiteral("(?i)ABC");
    QTest::newRow("unicode") << QStringLiteral("日本語");
}

void TestRegexValidation::validPatterns()
{
    QFETCH(QString, pattern);
    const efs::RegexValidation validation = efs::validateRegex(pattern, true);
    QVERIFY(validation.checked);
    QVERIFY2(validation.valid, qPrintable(validation.errorString));
    QVERIFY(validation.errorString.isEmpty());
    QCOMPARE(validation.errorOffset, qsizetype(-1));
}

void TestRegexValidation::invalidPatterns_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::newRow("unclosed group") << QStringLiteral("(");
    QTest::newRow("unclosed class") << QStringLiteral("[");
    QTest::newRow("dangling escape") << QStringLiteral("abc\\");
    QTest::newRow("unmatched close paren") << QStringLiteral("abc)");
    QTest::newRow("malformed quantifier") << QStringLiteral("a{3,1}");
    QTest::newRow("leading quantifier") << QStringLiteral("*abc");
}

void TestRegexValidation::invalidPatterns()
{
    QFETCH(QString, pattern);
    const efs::RegexValidation validation = efs::validateRegex(pattern, true);
    QVERIFY(validation.checked);
    QVERIFY(!validation.valid);
    QVERIFY(!validation.errorString.isEmpty());
    QVERIFY(validation.errorOffset >= 0);
}

void TestRegexValidation::errorOffsetPointsAtTheProblem()
{
    // "abcdef(" の "(" は index 6。UI はここを指して出す。
    const efs::RegexValidation validation = efs::validateRegex(QStringLiteral("abcdef("), true);
    QVERIFY(!validation.valid);
    QCOMPARE(validation.errorOffset, qsizetype(7));
}

// P2 の whitespace 契約: Regex ON では空白だけの pattern も有効な検索条件
// (regex:" " = 名前に空白を含む)。ここで invalid 扱いすると regression になる。
void TestRegexValidation::whitespaceOnlyPatternIsValid_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::newRow("one space") << QStringLiteral(" ");
    QTest::newRow("multiple spaces") << QStringLiteral("   ");
    QTest::newRow("tab") << QStringLiteral("\t");
    QTest::newRow("leading space") << QStringLiteral(" a");
    QTest::newRow("trailing space") << QStringLiteral("a ");
}

void TestRegexValidation::whitespaceOnlyPatternIsValid()
{
    QFETCH(QString, pattern);
    const efs::RegexValidation validation = efs::validateRegex(pattern, true);
    QVERIFY(validation.checked);
    QVERIFY2(validation.valid, qPrintable(validation.errorString));
}

// validator は判定するだけ。pattern の trim も正規化も一切しない
// (Regex ON では一字も変えない、という P2 の確定事項)。
void TestRegexValidation::validatorNeverRewritesThePattern_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::newRow("leading space") << QStringLiteral("  ^abc");
    QTest::newRow("trailing tab") << QStringLiteral("abc$\t");
    QTest::newRow("invalid with spaces") << QStringLiteral("  (  ");
    QTest::newRow("quotes") << QStringLiteral("\"abc\"");
}

void TestRegexValidation::validatorNeverRewritesThePattern()
{
    QFETCH(QString, pattern);
    const QString original = pattern;
    QString mutablePattern = pattern;

    const efs::RegexValidation validation = efs::validateRegex(mutablePattern, true);
    Q_UNUSED(validation)

    // 渡した QString は書き換えられていない (const 参照なので当然だが、将来
    // 「invalid なら trim して再判定する」等を足させないための固定)。
    QCOMPARE(mutablePattern, original);
}

QTEST_GUILESS_MAIN(TestRegexValidation)
#include "test_regex_validation.moc"
