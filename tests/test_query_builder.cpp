// EverythingQueryBuilder の単体テスト。仕様の中心なので table-driven で網羅する。
//
// Everything に依存しないため常に実行する (環境依存の skip は無い)。
#include <QSet>
#include <QtTest>

#include "backend/everything/EverythingQueryBuilder.h"
#include "core/FileKinds.h"

using efs::FileKind;
using efs::SearchQuery;

// QTest::addColumn で使うため。SearchTypes.h 側に置くと、テスト以外に不要な
// メタタイプ登録が増えるのでここで宣言する。
Q_DECLARE_METATYPE(efs::FileKind)

class TestQueryBuilder : public QObject {
    Q_OBJECT

private slots:
    void buildsExpectedQuery_data();
    void buildsExpectedQuery();
    void p0RegressionExtPlusRegex();
    void p2RegressionRegexWithSpaceIsQuoted();
    void fileKindIsAHardConstraint();
    void regexWhitespaceContractMatchesHasSearchConstraint_data();
    void regexWhitespaceContractMatchesHasSearchConstraint();
    void extensionListsAreDistinctAndLowerCase_data();
    void extensionListsAreDistinctAndLowerCase();
};

void TestQueryBuilder::buildsExpectedQuery_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<FileKind>("kind");
    QTest::addColumn<bool>("regex");
    QTest::addColumn<QString>("expected");

    const QString kImageExt =
        QStringLiteral("ext:jpg;jpeg;png;gif;bmp;webp;tif;tiff;heic;svg;ico;cr2;nef");
    const QString kVideoExt = QStringLiteral("ext:mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg;ts");
    const QString kAudioExt = QStringLiteral("ext:mp3;flac;wav;aac;m4a;ogg;opus;wma");
    const QString kDocumentExt =
        QStringLiteral("ext:pdf;doc;docx;xls;xlsx;ppt;pptx;txt;md;rtf;odt;ods;csv;epub");

    // --- テキストのみ ---------------------------------------------------------
    QTest::newRow("plain text") << QStringLiteral("report") << FileKind::All << false
                                << QStringLiteral("report");
    QTest::newRow("empty text") << QString() << FileKind::All << false << QString();
    QTest::newRow("whitespace only text")
        << QStringLiteral("   ") << FileKind::All << false << QString();
    QTest::newRow("text is trimmed")
        << QStringLiteral("  report  ") << FileKind::All << false << QStringLiteral("report");
    // `/` はパス区切りとして書かれたものと見て `\` へ揃える。Everything が
    // パス区切りとして解釈するのは `\` だけで、そのままでは何も当たらない。
    QTest::newRow("forward slashes become backslashes")
        << QStringLiteral("path/to/file.txt") << FileKind::All << false
        << QStringLiteral("path\\to\\file.txt");
    QTest::newRow("drive path with forward slashes")
        << QStringLiteral("C:/dev/soft") << FileKind::All << false
        << QStringLiteral("C:\\dev\\soft");
    // 日付の `/` はパス区切りではないので触らない。
    QTest::newRow("date function keeps its slashes")
        << QStringLiteral("dm:2026/01/01 report") << FileKind::All << false
        << QStringLiteral("dm:2026/01/01 report");
    QTest::newRow("regex off keeps text verbatim")
        << QStringLiteral("^IMG_\\d+") << FileKind::All << false << QStringLiteral("^IMG_\\d+");

    // --- Regex ON → インライン修飾子 regex:"..." -------------------------------
    // 引用符は Phase 2 冒頭の実機検証で確定した仕様 (README 参照)。空白を含まない
    // パターンでも無条件に囲む (囲んでも結果が変わらないことを実測済み)。
    QTest::newRow("regex on") << QStringLiteral("^IMG_\\d+") << FileKind::All << true
                              << QStringLiteral("regex:\"^IMG_\\d+\"");
    QTest::newRow("regex on with empty text") << QString() << FileKind::All << true << QString();
    // Regex ON はユーザーのパターンを一字も変えない。正規表現の `/` はただの
    // 文字で、`\` はエスケープなので、入れ替えるとパターンの意味が変わる。
    QTest::newRow("regex on keeps forward slashes")
        << QStringLiteral("a/b") << FileKind::All << true << QStringLiteral("regex:\"a/b\"");

    // --- Regex ON の空白はユーザーのパターンの一部。trim してはならない --------
    // 引用の内側では前後の空白も TAB も意味を持つことを実測済み。空文字だけが
    // 「テキスト条件なし」で、空白だけのパターンは有効な検索条件。
    QTest::newRow("regex leading space is preserved")
        << QStringLiteral(" alpha") << FileKind::All << true << QStringLiteral("regex:\" alpha\"");
    QTest::newRow("regex trailing space is preserved")
        << QStringLiteral("alpha ") << FileKind::All << true << QStringLiteral("regex:\"alpha \"");
    QTest::newRow("regex leading and trailing space are preserved")
        << QStringLiteral("  ^a b  ") << FileKind::All << true
        << QStringLiteral("regex:\"  ^a b  \"");
    QTest::newRow("regex one space only")
        << QStringLiteral(" ") << FileKind::All << true << QStringLiteral("regex:\" \"");
    QTest::newRow("regex multiple spaces only")
        << QStringLiteral("   ") << FileKind::All << true << QStringLiteral("regex:\"   \"");
    QTest::newRow("regex tab only")
        << QStringLiteral("\t") << FileKind::All << true << QStringLiteral("regex:\"\t\"");
    QTest::newRow("regex mixed whitespace only")
        << QStringLiteral(" \t ") << FileKind::All << true << QStringLiteral("regex:\" \t \"");
    QTest::newRow("regex whitespace only + FileKind")
        << QStringLiteral(" ") << FileKind::Image << true
        << kImageExt + QStringLiteral(" regex:\" \"");

    // --- Regex ON + 空白 (Phase 2 の中心仕様) ---------------------------------
    QTest::newRow("regex with one space") << QStringLiteral("^IMG \\d+") << FileKind::All << true
                                          << QStringLiteral("regex:\"^IMG \\d+\"");
    QTest::newRow("regex with multiple spaces") << QStringLiteral("^a b c$") << FileKind::All
                                                << true << QStringLiteral("regex:\"^a b c$\"");
    QTest::newRow("regex with consecutive spaces")
        << QStringLiteral("a  b") << FileKind::All << true << QStringLiteral("regex:\"a  b\"");
    // TAB も Everything の項区切りなので、引用の内側に入っていること。
    // (NTFS のファイル名に TAB は入れられないので一致はしないが、項が割れて
    //  別のものが引っかかる方が悪い。実測で raw な TAB は分割された。)
    QTest::newRow("regex with tab inside")
        << QStringLiteral("a\tb") << FileKind::All << true << QStringLiteral("regex:\"a\tb\"");
    QTest::newRow("regex with space + FileKind")
        << QStringLiteral("^IMG \\d+") << FileKind::Image << true
        << kImageExt + QStringLiteral(" regex:\"^IMG \\d+\"");
    QTest::newRow("regex with space + Directory")
        << QStringLiteral("^My Documents") << FileKind::Directory << true
        << QStringLiteral("folder: regex:\"^My Documents\"");

    // --- 引用符 / バックスラッシュを含むパターン -------------------------------
    // バックスラッシュのエスケープは引用の内側でそのまま働く (実測)。
    QTest::newRow("regex with backslash escapes")
        << QStringLiteral("^efs\\.txt$") << FileKind::All << true
        << QStringLiteral("regex:\"^efs\\.txt$\"");
    QTest::newRow("regex with character class containing space")
        << QStringLiteral("a[ _]b") << FileKind::All << true << QStringLiteral("regex:\"a[ _]b\"");
    // パターン自体が " を含む場合は補正しない (エスケープの追加はしない)。
    // NTFS のファイル名に " は入らないので実用上意味のあるパターンではない。
    QTest::newRow("regex containing a double quote is passed through unmodified")
        << QStringLiteral("a\"b") << FileKind::All << true << QStringLiteral("regex:\"a\"b\"");

    // --- 種別のみ -------------------------------------------------------------
    QTest::newRow("All has no prefix") << QString() << FileKind::All << false << QString();
    QTest::newRow("Image") << QString() << FileKind::Image << false << kImageExt;
    QTest::newRow("Video") << QString() << FileKind::Video << false << kVideoExt;
    QTest::newRow("Audio") << QString() << FileKind::Audio << false << kAudioExt;
    QTest::newRow("Document") << QString() << FileKind::Document << false << kDocumentExt;
    QTest::newRow("Directory") << QString() << FileKind::Directory << false
                               << QStringLiteral("folder:");

    // --- 種別 + テキスト ------------------------------------------------------
    // 種別項があるときは、ユーザー式を <> で囲んで種別を外側の AND に固定する
    // (演算子の優先順位設定に依らず種別を hard constraint にするため)。
    QTest::newRow("Image + text") << QStringLiteral("holiday") << FileKind::Image << false
                                  << kImageExt + QStringLiteral(" <holiday>");
    QTest::newRow("Directory + text") << QStringLiteral("build") << FileKind::Directory << false
                                      << QStringLiteral("folder: <build>");
    QTest::newRow("Image + OR expression is grouped")
        << QStringLiteral("holiday|vacation") << FileKind::Image << false
        << kImageExt + QStringLiteral(" <holiday|vacation>");
    QTest::newRow("Image + AND expression is grouped")
        << QStringLiteral("holiday 2026") << FileKind::Image << false
        << kImageExt + QStringLiteral(" <holiday 2026>");
    QTest::newRow("Image + negation is grouped")
        << QStringLiteral("!draft") << FileKind::Image << false
        << kImageExt + QStringLiteral(" <!draft>");
    // All のときは種別項が無いので囲む理由が無い。クエリを無駄に飾らない。
    QTest::newRow("All + OR expression is not grouped")
        << QStringLiteral("holiday|vacation") << FileKind::All << false
        << QStringLiteral("holiday|vacation");

    // --- 種別 + Regex (Phase 0 で実機確定した形) ------------------------------
    QTest::newRow("Image + regex") << QStringLiteral("^a00") << FileKind::Image << true
                                   << kImageExt + QStringLiteral(" regex:\"^a00\"");
    QTest::newRow("Directory + regex") << QStringLiteral("^tmp") << FileKind::Directory << true
                                       << QStringLiteral("folder: regex:\"^tmp\"");

    // --- 特殊文字はエスケープせずそのまま渡す (Everything の検索構文をそのまま
    //     使わせるための意図的な設計) ----------------------------------------
    QTest::newRow("special characters are passed through")
        << QStringLiteral("a b*c?d\"e\" !f") << FileKind::All << false
        << QStringLiteral("a b*c?d\"e\" !f");
    QTest::newRow("regex metacharacters are passed through")
        << QStringLiteral("^(a|b)[0-9]{2}\\.txt$") << FileKind::All << true
        << QStringLiteral("regex:\"^(a|b)[0-9]{2}\\.txt$\"");
    QTest::newRow("non ascii text") << QStringLiteral("報告書") << FileKind::Document << false
                                    << kDocumentExt + QStringLiteral(" <報告書>");
}

void TestQueryBuilder::buildsExpectedQuery()
{
    QFETCH(QString, text);
    QFETCH(FileKind, kind);
    QFETCH(bool, regex);
    QFETCH(QString, expected);

    SearchQuery query;
    query.text = text;
    query.kind = kind;
    query.regex = regex;

    QCOMPARE(efs::buildQueryString(query), expected);
}

// Phase 0 の spike (削除済み) が実機で確認した結論の回帰テスト。
//
// Everything 1.4.1.1022 では `ext:jpg;... regex:...` の 2 項が AND 結合され、
// Everything_SetRegex は FALSE のままでよい。したがってクエリ文字列は
// 「ext: 前置詞が regex: パターンに飲み込まれていない」形でなければならない。
// 「拡張子を正規表現へ畳み込むフォールバック」へ退化していないことを固定する。
void TestQueryBuilder::p0RegressionExtPlusRegex()
{
    SearchQuery query;
    query.kind = FileKind::Image;
    query.regex = true;
    query.text = QStringLiteral("^a00");

    const QString built = efs::buildQueryString(query);

    QCOMPARE(built, QStringLiteral("ext:jpg;jpeg;png;gif;bmp;webp;tif;tiff;heic;svg;ico;cr2;nef "
                                   "regex:\"^a00\""));
    // ext: は独立した先頭の項であり、regex: の後ろに埋め込まれていないこと。
    QVERIFY(built.startsWith(QStringLiteral("ext:")));
    QVERIFY(built.contains(QStringLiteral(" regex:")));
    QVERIFY(built.indexOf(QStringLiteral("ext:")) < built.indexOf(QStringLiteral("regex:")));
    // 拡張子を正規表現へ畳み込んだ形 (.*\.(jpg|png)$) になっていないこと。
    QVERIFY(!built.contains(QStringLiteral("(jpg|")));
}

// Phase 2 冒頭の実機検証 (tmp/ の spike。削除済み) が確定した結論の回帰テスト。
//
// Everything 1.4.1.1022 の search-term parser は空白/TAB を regex エンジンより
// 先に項の区切りとして解釈する。したがって `regex:^efsspike alpha` は
// `regex:^efsspike` AND `alpha` の 2 項になり、実測でも正解集合 3 件に対して
// 4 件を返した。引用した `regex:"^efsspike alpha"` は 3 件で一致した。
// 「引用符が外れる」形へ退化していないことを固定する。
void TestQueryBuilder::p2RegressionRegexWithSpaceIsQuoted()
{
    SearchQuery query;
    query.regex = true;
    query.text = QStringLiteral("^IMG \\d+");

    const QString built = efs::buildQueryString(query);

    QCOMPARE(built, QStringLiteral("regex:\"^IMG \\d+\""));
    // 空白がパターンの内側にあり、項として露出していないこと。
    const qsizetype openQuote = built.indexOf(u'"');
    const qsizetype closeQuote = built.lastIndexOf(u'"');
    QVERIFY(openQuote >= 0);
    QVERIFY(closeQuote > openQuote);
    QVERIFY(built.indexOf(u' ') > openQuote);
    QVERIFY(built.lastIndexOf(u' ') < closeQuote);
    // 種別フィルタと併用しても、ext: 項だけが引用の外に残ること (P0 の結論)。
    query.kind = FileKind::Image;
    const QString withKind = efs::buildQueryString(query);
    QVERIFY(withKind.startsWith(QStringLiteral("ext:")));
    QVERIFY(withKind.endsWith(QStringLiteral(" regex:\"^IMG \\d+\"")));
}

// 種別フィルタは hard constraint。ユーザー式に OR が含まれていても、種別項が
// OR の片側にしか掛からない形へ退化していないこと。
void TestQueryBuilder::fileKindIsAHardConstraint()
{
    SearchQuery query;
    query.kind = FileKind::Image;
    query.text = QStringLiteral("alpha|beta");

    const QString built = efs::buildQueryString(query);

    QVERIFY(built.startsWith(QStringLiteral("ext:")));
    // ユーザー式全体がグルーピングの内側にあること。
    QVERIFY2(built.endsWith(QStringLiteral(" <alpha|beta>")), qPrintable(built));
    // OR がグルーピングの外へ漏れていないこと。
    const qsizetype openGroup = built.indexOf(u'<');
    const qsizetype closeGroup = built.lastIndexOf(u'>');
    QVERIFY(openGroup > built.indexOf(QStringLiteral("ext:")));
    QVERIFY(built.indexOf(u'|') > openGroup);
    QVERIFY(built.indexOf(u'|') < closeGroup);
}

// buildQueryString() と hasSearchConstraint() は空白について同じ契約でなければ
// ならない。ずれると「検索条件はあると判定したのにクエリが空」あるいはその逆に
// なる。両者を同じ入力で突き合わせて固定する。
void TestQueryBuilder::regexWhitespaceContractMatchesHasSearchConstraint_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("regex");
    QTest::addColumn<bool>("expectConstraint");

    QTest::newRow("regex off, empty") << QString() << false << false;
    QTest::newRow("regex off, space only") << QStringLiteral(" ") << false << false;
    QTest::newRow("regex off, tab only") << QStringLiteral("\t") << false << false;
    QTest::newRow("regex off, text") << QStringLiteral("a") << false << true;
    QTest::newRow("regex on, empty") << QString() << true << false;
    QTest::newRow("regex on, space only") << QStringLiteral(" ") << true << true;
    QTest::newRow("regex on, multiple spaces only") << QStringLiteral("   ") << true << true;
    QTest::newRow("regex on, tab only") << QStringLiteral("\t") << true << true;
    QTest::newRow("regex on, leading space") << QStringLiteral(" a") << true << true;
    QTest::newRow("regex on, trailing space") << QStringLiteral("a ") << true << true;
}

void TestQueryBuilder::regexWhitespaceContractMatchesHasSearchConstraint()
{
    QFETCH(QString, text);
    QFETCH(bool, regex);
    QFETCH(bool, expectConstraint);

    SearchQuery query; // kind は All のまま (テキストだけで判定させる)
    query.text = text;
    query.regex = regex;

    QCOMPARE(efs::hasSearchConstraint(query), expectConstraint);
    // 「条件あり」と判定したなら、クエリ文字列も空であってはならない。
    QCOMPARE(!efs::buildQueryString(query).isEmpty(), expectConstraint);
}

void TestQueryBuilder::extensionListsAreDistinctAndLowerCase_data()
{
    QTest::addColumn<FileKind>("kind");
    QTest::addColumn<bool>("expectEmpty");

    QTest::newRow("All") << FileKind::All << true;
    QTest::newRow("Image") << FileKind::Image << false;
    QTest::newRow("Video") << FileKind::Video << false;
    QTest::newRow("Audio") << FileKind::Audio << false;
    QTest::newRow("Document") << FileKind::Document << false;
    QTest::newRow("Directory") << FileKind::Directory << true;
}

void TestQueryBuilder::extensionListsAreDistinctAndLowerCase()
{
    QFETCH(FileKind, kind);
    QFETCH(bool, expectEmpty);

    const QStringList extensions = efs::extensionsFor(kind);
    QCOMPARE(extensions.isEmpty(), expectEmpty);

    for (const QString& extension : extensions) {
        QVERIFY2(!extension.startsWith(u'.'), qPrintable(extension));
        QCOMPARE(extension, extension.toLower());
        // ext: の区切り文字を含んでいたらクエリが壊れる。
        QVERIFY2(!extension.contains(u';'), qPrintable(extension));
        QVERIFY2(!extension.contains(u' '), qPrintable(extension));
    }
    // 重複があっても動くが、クエリが無駄に長くなるので気づけるようにする。
    QCOMPARE(QSet<QString>(extensions.begin(), extensions.end()).size(), extensions.size());
}

QTEST_GUILESS_MAIN(TestQueryBuilder)
#include "test_query_builder.moc"
