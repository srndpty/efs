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
    QTest::newRow("regex off keeps text verbatim")
        << QStringLiteral("^IMG_\\d+") << FileKind::All << false << QStringLiteral("^IMG_\\d+");

    // --- Regex ON → インライン修飾子 regex: -----------------------------------
    QTest::newRow("regex on") << QStringLiteral("^IMG_\\d+") << FileKind::All << true
                              << QStringLiteral("regex:^IMG_\\d+");
    QTest::newRow("regex on with empty text") << QString() << FileKind::All << true << QString();
    QTest::newRow("regex on with whitespace only text")
        << QStringLiteral(" \t ") << FileKind::All << true << QString();

    // --- 種別のみ -------------------------------------------------------------
    QTest::newRow("All has no prefix") << QString() << FileKind::All << false << QString();
    QTest::newRow("Image") << QString() << FileKind::Image << false << kImageExt;
    QTest::newRow("Video") << QString() << FileKind::Video << false << kVideoExt;
    QTest::newRow("Audio") << QString() << FileKind::Audio << false << kAudioExt;
    QTest::newRow("Document") << QString() << FileKind::Document << false << kDocumentExt;
    QTest::newRow("Directory") << QString() << FileKind::Directory << false
                               << QStringLiteral("folder:");

    // --- 種別 + テキスト ------------------------------------------------------
    QTest::newRow("Image + text") << QStringLiteral("holiday") << FileKind::Image << false
                                  << kImageExt + QStringLiteral(" holiday");
    QTest::newRow("Directory + text") << QStringLiteral("build") << FileKind::Directory << false
                                      << QStringLiteral("folder: build");

    // --- 種別 + Regex (Phase 0 で実機確定した形) ------------------------------
    QTest::newRow("Image + regex") << QStringLiteral("^a00") << FileKind::Image << true
                                   << kImageExt + QStringLiteral(" regex:^a00");
    QTest::newRow("Directory + regex") << QStringLiteral("^tmp") << FileKind::Directory << true
                                       << QStringLiteral("folder: regex:^tmp");

    // --- 特殊文字はエスケープせずそのまま渡す (Everything の検索構文をそのまま
    //     使わせるための意図的な設計) ----------------------------------------
    QTest::newRow("special characters are passed through")
        << QStringLiteral("a b*c?d\"e\" !f") << FileKind::All << false
        << QStringLiteral("a b*c?d\"e\" !f");
    QTest::newRow("regex metacharacters are passed through")
        << QStringLiteral("^(a|b)[0-9]{2}\\.txt$") << FileKind::All << true
        << QStringLiteral("regex:^(a|b)[0-9]{2}\\.txt$");
    QTest::newRow("non ascii text") << QStringLiteral("報告書") << FileKind::Document << false
                                    << kDocumentExt + QStringLiteral(" 報告書");
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
                                   "regex:^a00"));
    // ext: は独立した先頭の項であり、regex: の後ろに埋め込まれていないこと。
    QVERIFY(built.startsWith(QStringLiteral("ext:")));
    QVERIFY(built.contains(QStringLiteral(" regex:")));
    QVERIFY(built.indexOf(QStringLiteral("ext:")) < built.indexOf(QStringLiteral("regex:")));
    // 拡張子を正規表現へ畳み込んだ形 (.*\.(jpg|png)$) になっていないこと。
    QVERIFY(!built.contains(QStringLiteral("(jpg|")));
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
