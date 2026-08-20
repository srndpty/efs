// MatchHighlighter の単体テスト (結果一覧の強調表示)。
//
// Qt Core 以外に依存しない純粋なコードなので、Everything の有無に関係なく走る。
#include <QtTest>

#include "core/MatchHighlight.h"

using efs::MatchHighlighter;
using efs::MatchRange;

namespace {

// 期待値を読みやすくするため、区間を "IMG" のような部分文字列へ戻して比べる。
QStringList slices(const QString& text, const QList<MatchRange>& ranges)
{
    QStringList out;
    out.reserve(ranges.size());
    for (const MatchRange& range : ranges)
        out.append(text.mid(range.start, range.length));
    return out;
}

} // namespace

class TestMatchHighlight : public QObject {
    Q_OBJECT

private slots:
    void plainTerms_data();
    void plainTerms();
    void regexPatterns_data();
    void regexPatterns();
    void emptyHighlighterMatchesNothing();
    void reportsPositionsNotJustText();
    void mergesOverlappingRanges();
};

void TestMatchHighlight::plainTerms_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("substring") << QStringLiteral("img") << QStringLiteral("IMG_0042.jpg")
                               << QStringList{QStringLiteral("IMG")};
    QTest::newRow("case insensitive by default")
        << QStringLiteral("IMG") << QStringLiteral("my_img_file.png")
        << QStringList{QStringLiteral("img")};
    QTest::newRow("all occurrences")
        << QStringLiteral("a") << QStringLiteral("banana")
        << QStringList{QStringLiteral("a"), QStringLiteral("a"), QStringLiteral("a")};
    // 空白は AND。項ごとに別々の場所が強調される。
    QTest::newRow("two terms") << QStringLiteral("img jpg") << QStringLiteral("IMG_0042.jpg")
                               << QStringList{QStringLiteral("IMG"), QStringLiteral("jpg")};
    QTest::newRow("term that does not occur")
        << QStringLiteral("img raw") << QStringLiteral("IMG_0042.jpg")
        << QStringList{QStringLiteral("IMG")};
    // 引用の内側の空白は項の区切りではない。
    QTest::newRow("quoted term keeps spaces")
        << QStringLiteral("\"my report\"") << QStringLiteral("my report 2026.xlsx")
        << QStringList{QStringLiteral("my report")};
    // OR とグルーピングは区切りとして扱う (どちらの候補も強調する)。
    QTest::newRow("or") << QStringLiteral("jpg|png") << QStringLiteral("a.png")
                        << QStringList{QStringLiteral("png")};
    QTest::newRow("grouping") << QStringLiteral("<img>") << QStringLiteral("IMG_1.jpg")
                              << QStringList{QStringLiteral("IMG")};
    // 関数構文は「値」であってファイル名の一部ではないので強調しない。
    QTest::newRow("function term is not highlighted")
        << QStringLiteral("ext:jpg") << QStringLiteral("jpg_notes.txt") << QStringList{};
    QTest::newRow("function term next to a real term")
        << QStringLiteral("ext:jpg img") << QStringLiteral("IMG_0042.jpg")
        << QStringList{QStringLiteral("IMG")};
    // ドライブ付きパスは関数構文ではない。
    QTest::newRow("drive letter is not a function")
        << QStringLiteral("c:\\dev") << QStringLiteral("c:\\dev\\soft")
        << QStringList{QStringLiteral("c:\\dev")};
    // 除外項に一致する部分は、そもそも結果に出ないか、強調する意味が無い。
    QTest::newRow("negated term is not highlighted")
        << QStringLiteral("!tmp") << QStringLiteral("tmp.txt") << QStringList{};
    // ワイルドカードは名前全体との一致 (Everything と同じ)。
    QTest::newRow("wildcard matches whole name")
        << QStringLiteral("*.jpg") << QStringLiteral("IMG_0042.jpg")
        << QStringList{QStringLiteral("IMG_0042.jpg")};
    QTest::newRow("wildcard anchored, so a substring alone does not match")
        << QStringLiteral("img*") << QStringLiteral("my IMG_0042.jpg") << QStringList{};
    QTest::newRow("question mark is a single character")
        << QStringLiteral("a?c.txt") << QStringLiteral("abc.txt")
        << QStringList{QStringLiteral("abc.txt")};
    // 正規表現の記号は Regex OFF ではただの文字。
    QTest::newRow("regex metacharacters stay literal")
        << QStringLiteral("a.txt") << QStringLiteral("axtxt a.txt")
        << QStringList{QStringLiteral("a.txt")};
    QTest::newRow("empty query") << QString() << QStringLiteral("a.txt") << QStringList{};
    QTest::newRow("whitespace only query")
        << QStringLiteral("   ") << QStringLiteral("a.txt") << QStringList{};
}

void TestMatchHighlight::plainTerms()
{
    QFETCH(QString, query);
    QFETCH(QString, text);
    QFETCH(QStringList, expected);

    const MatchHighlighter highlighter(query, /*regex=*/false, /*matchCase=*/false);
    QCOMPARE(slices(text, highlighter.ranges(text)), expected);
}

void TestMatchHighlight::regexPatterns_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("simple") << QStringLiteral("^IMG \\d+") << QStringLiteral("IMG 0042.jpg")
                            << QStringList{QStringLiteral("IMG 0042")};
    QTest::newRow("alternation") << QStringLiteral("jpe?g|png") << QStringLiteral("a.jpeg")
                                 << QStringList{QStringLiteral("jpeg")};
    // 全体一致ではなく部分一致 (Everything の regex: と同じ)。
    QTest::newRow("unanchored") << QStringLiteral("\\d{4}") << QStringLiteral("report 2026 v2.docx")
                                << QStringList{QStringLiteral("2026")};
    // 空一致は強調できないので落とす (行そのものは一致している)。
    QTest::newRow("empty match is dropped")
        << QStringLiteral("x*") << QStringLiteral("abc") << QStringList{};
    // 不正なパターンでも落ちない。強調が付かないだけ。
    QTest::newRow("invalid pattern")
        << QStringLiteral("(unclosed") << QStringLiteral("unclosed.txt") << QStringList{};
    // Regex ON では空白も `|` も項の区切りではない (パターンの一部)。
    QTest::newRow("spaces belong to the pattern")
        << QStringLiteral("a b") << QStringLiteral("a b c") << QStringList{QStringLiteral("a b")};
}

void TestMatchHighlight::regexPatterns()
{
    QFETCH(QString, query);
    QFETCH(QString, text);
    QFETCH(QStringList, expected);

    const MatchHighlighter highlighter(query, /*regex=*/true, /*matchCase=*/false);
    QCOMPARE(slices(text, highlighter.ranges(text)), expected);
}

void TestMatchHighlight::emptyHighlighterMatchesNothing()
{
    const MatchHighlighter none;
    QVERIFY(none.isEmpty());
    QVERIFY(none.ranges(QStringLiteral("anything")).isEmpty());

    // 強調できる項が 1 つも無いクエリも「空」として扱う (paint が従来の
    // 描画経路へ戻れるようにするため)。
    QVERIFY(MatchHighlighter(QStringLiteral("ext:jpg"), false, false).isEmpty());
    QVERIFY(MatchHighlighter(QString(), true, false).isEmpty());
}

void TestMatchHighlight::reportsPositionsNotJustText()
{
    const MatchHighlighter highlighter(QStringLiteral("42"), false, false);
    const QList<MatchRange> ranges = highlighter.ranges(QStringLiteral("IMG_0042.jpg"));
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.first(), (MatchRange{6, 2}));
}

void TestMatchHighlight::mergesOverlappingRanges()
{
    // "report" と "port" は重なる。描画側が同じ文字を二重に扱わないよう 1 区間へ。
    const MatchHighlighter highlighter(QStringLiteral("report port"), false, false);
    const QString text = QStringLiteral("report.txt");
    const QList<MatchRange> ranges = highlighter.ranges(text);
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.first(), (MatchRange{0, 6}));

    // 大文字小文字を跨いだ重なりも同じ。
    const MatchHighlighter other(QStringLiteral("ab bc"), false, false);
    QCOMPARE(slices(QStringLiteral("ABC"), other.ranges(QStringLiteral("ABC"))),
             QStringList{QStringLiteral("ABC")});
}

QTEST_GUILESS_MAIN(TestMatchHighlight)
#include "test_match_highlight.moc"
