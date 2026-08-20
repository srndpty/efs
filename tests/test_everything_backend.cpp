// EverythingBackend のテスト。
//
// FILETIME 変換は Everything 非依存の純粋関数なので常に実行する。
// 実際の検索は起動中の Everything を要求するため、利用できない環境では QSKIP
// する (CI には Everything が無い)。単体テストを環境依存で skip はしない。
#include <QTimeZone>
#include <QtTest>

#include <limits>

#include "backend/everything/EverythingBackend.h"
#include "core/FileKinds.h"

#include <algorithm>

// QTest::addColumn で使うため。
Q_DECLARE_METATYPE(efs::FileKind)

class TestEverythingBackend : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- 常に実行する単体テスト ---------------------------------------------
    void fileTimeToDateTime_data();
    void fileTimeToDateTime();

    // --- Everything が必要な統合テスト --------------------------------------
    void searchReturnsResults();
    void searchRespectsMaxResults();
    void searchReturnsMetadata();
    void directoryFilterReturnsOnlyDirectories();
    void extAndRegexAreCombinedWithAnd();
    void filterOnlyQueryReturnsMatchingExtensions_data();
    void filterOnlyQueryReturnsMatchingExtensions();
    void regexWithSpaceIsOneTerm();
    void regexWhitespaceIsNotTrimmed();
    void fileKindConstrainsOrExpressions();
    void sortIsAppliedByBackend();
    void sortAppliesToWholeResultSetNotJustReturnedRows();

private:
    // 空でなければ統合テストを skip する理由。
    QString m_skipReason;
    efs::EverythingBackend m_backend;
};

void TestEverythingBackend::initTestCase()
{
    QString reason;
    if (!m_backend.isAvailable(&reason)) {
        m_skipReason = QStringLiteral("Everything SDK を利用できない: %1").arg(reason);
        return;
    }

    // DLL があっても Everything.exe が起動していなければ IPC は失敗する。
    efs::SearchQuery probe;
    probe.text = QStringLiteral("a");
    probe.maxResults = 1;
    const efs::SearchResults results = m_backend.search(probe);
    if (!results.error.isEmpty())
        m_skipReason = QStringLiteral("Everything へ問い合わせできない: %1").arg(results.error);
}

#define SKIP_WITHOUT_EVERYTHING()                                                                  \
    do {                                                                                           \
        if (!m_skipReason.isEmpty())                                                               \
            QSKIP(qPrintable(m_skipReason));                                                       \
    } while (false)

void TestEverythingBackend::fileTimeToDateTime_data()
{
    QTest::addColumn<quint64>("fileTime");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qint64>("msecsSinceEpoch");

    // FILETIME の 0 だけが「日時なし」。
    QTest::newRow("zero") << quint64(0) << false << qint64(0);
    // qint64 で表せない値は日時ではない。1601 年付近へ化けさせず無効を返す。
    QTest::newRow("not a date (all bits set)")
        << std::numeric_limits<quint64>::max() << false << qint64(0);

    QTest::newRow("unix epoch") << quint64(116444736000000000ULL) << true << qint64(0);
    QTest::newRow("1 tick before unix epoch")
        << quint64(116444736000000000ULL - 1) << true << qint64(0);
    QTest::newRow("1s before unix epoch")
        << quint64(116444736000000000ULL - 10000000ULL) << true << qint64(-1000);
    // 1970 より前も負の Unix epoch time として正しく変換する。
    const qint64 msecs1960 =
        QDateTime(QDate(1960, 3, 4), QTime(5, 6, 7), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("1960-03-04T05:06:07Z")
        << quint64(116444736000000000ULL - static_cast<quint64>(-msecs1960) * 10000ULL) << true
        << msecs1960;
    // FILETIME の原点付近 (1601-01-01T00:00:00.001Z)。下端でも破綻しないこと。
    QTest::newRow("filetime epoch + 1ms")
        << quint64(10000) << true
        << QDateTime(QDate(1601, 1, 1), QTime(0, 0, 0, 1), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("unix epoch + 1s")
        << quint64(116444736000000000ULL + 10000000ULL) << true << qint64(1000);
    // 実日時。期待値は QDateTime から導出し、テスト側で暦計算をしない。
    const qint64 msecs =
        QDateTime(QDate(2026, 8, 16), QTime(9, 30), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("2026-08-16T09:30Z")
        << quint64(116444736000000000ULL + (static_cast<quint64>(msecs) * 10000ULL)) << true
        << msecs;
}

void TestEverythingBackend::fileTimeToDateTime()
{
    QFETCH(quint64, fileTime);
    QFETCH(bool, valid);
    QFETCH(qint64, msecsSinceEpoch);

    const QDateTime converted = efs::fileTimeToDateTime(fileTime);
    QCOMPARE(converted.isValid(), valid);
    if (valid)
        QCOMPARE(converted.toMSecsSinceEpoch(), msecsSinceEpoch);
}

void TestEverythingBackend::searchReturnsResults()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("exe");
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());
    QVERIFY(results.totalMatches >= static_cast<quint64>(results.rows.size()));
    QCOMPARE(results.truncated, results.totalMatches > static_cast<quint64>(results.rows.size()));
    QVERIFY(results.elapsedMs >= 0);
}

void TestEverythingBackend::searchRespectsMaxResults()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("e"); // 大量にヒットさせて打ち切りを起こす
    query.id = 42;
    query.maxResults = 10;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QCOMPARE(results.id, quint64(42)); // 要求 id が結果に載って返ること
    QVERIFY(results.rows.size() <= 10);
    QVERIFY(results.totalMatches >= static_cast<quint64>(results.rows.size()));
    if (results.totalMatches > 10)
        QVERIFY(results.truncated);
}

void TestEverythingBackend::searchReturnsMetadata()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("exe");
    query.maxResults = 20;
    // 名前昇順の先頭はディレクトリで埋まりうる (実測)。サイズ降順にして
    // ファイルを確実に含め、ついでにソート指定が backend へ渡ることも見る。
    query.sortKey = efs::SortKey::Size;
    query.sortOrder = efs::SortOrder::Desc;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());

    int withSize = 0;
    int withDate = 0;
    for (const efs::ResultRow& row : results.rows) {
        QVERIFY(!row.name.isEmpty());
        QVERIFY(!row.path.isEmpty());
        if (row.isDir)
            QCOMPARE(row.size, qint64(-1)); // ディレクトリのサイズは表示しない
        else if (row.size >= 0)
            ++withSize;
        // **更新日時の「もっともらしさ」は検査しない。** 実データにはツールが
        // 展開したまま mtime を設定しなかったファイルが普通に存在し (実測:
        // 265MB の claude.exe が 1970-01-01T09:00 = epoch 0)、「1980 年以降」の
        // ような閾値は FILETIME 変換の契約ではなくインデックスの中身に依存する。
        // 変換の正しさの authority は synthetic な fileTimeToDateTime() の方。
        // ここで見るのは「metadata を取得できているか」だけにする。
        if (row.modified.isValid())
            ++withDate;
    }
    QVERIFY(withSize > 0);
    QVERIFY(withDate > 0);
    // サイズ降順が効いていること。
    QVERIFY(results.rows.first().size >= results.rows.last().size);
}

void TestEverythingBackend::directoryFilterReturnsOnlyDirectories()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.kind = efs::FileKind::Directory;
    query.maxResults = 50;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());
    for (const efs::ResultRow& row : results.rows)
        QVERIFY2(row.isDir, qPrintable(row.name));
}

// Phase 0 の spike (削除済み) が実機で確認した結論の回帰テスト。
//
// `ext:...` と `regex:...` は Everything_SetRegex(FALSE) のまま AND 結合される。
// 陽性 (全行が拡張子条件を満たす) と陰性対照 (一致しない正規表現で 0 件) の
// 両方を見る。片方だけでは「regex: 項が無視されている」場合と区別できない。
void TestEverythingBackend::extAndRegexAreCombinedWithAnd()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery positive;
    positive.kind = efs::FileKind::Document;
    positive.regex = true;
    positive.text = QStringLiteral("^.");
    positive.maxResults = 100;
    const efs::SearchResults positiveResults = m_backend.search(positive);

    QVERIFY(positiveResults.error.isEmpty());
    if (positiveResults.rows.isEmpty())
        QSKIP("この環境には Document 種別のファイルが無く、併用を判定できない");

    const QStringList extensions = efs::extensionsFor(efs::FileKind::Document);
    for (const efs::ResultRow& row : positiveResults.rows) {
        const QString suffix = row.name.section(u'.', -1).toLower();
        QVERIFY2(extensions.contains(suffix), qPrintable(row.name));
    }

    // 陰性対照。ext: 項が生きていても regex: 項が AND されていれば 0 件になる。
    efs::SearchQuery negative = positive;
    negative.text = QStringLiteral("^ZZQXNOMATCH");
    const efs::SearchResults negativeResults = m_backend.search(negative);

    QVERIFY(negativeResults.error.isEmpty());
    QVERIFY(negativeResults.rows.isEmpty());
    QCOMPARE(negativeResults.totalMatches, quint64(0));
}

// --- Phase 2 -----------------------------------------------------------------

// テキスト無し・種別のみのクエリ (Phase 2 の「空検索」の意味の変更)。
void TestEverythingBackend::filterOnlyQueryReturnsMatchingExtensions_data()
{
    QTest::addColumn<efs::FileKind>("kind");

    QTest::newRow("Image") << efs::FileKind::Image;
    QTest::newRow("Video") << efs::FileKind::Video;
    QTest::newRow("Audio") << efs::FileKind::Audio;
    QTest::newRow("Document") << efs::FileKind::Document;
}

void TestEverythingBackend::filterOnlyQueryReturnsMatchingExtensions()
{
    SKIP_WITHOUT_EVERYTHING();
    QFETCH(efs::FileKind, kind);

    efs::SearchQuery query;
    query.kind = kind; // text は空のまま
    query.maxResults = 100;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    if (results.rows.isEmpty())
        QSKIP("この環境にはこの種別のファイルが無い");

    const QStringList extensions = efs::extensionsFor(kind);
    for (const efs::ResultRow& row : results.rows) {
        QVERIFY2(!row.isDir, qPrintable(row.name));
        QVERIFY2(extensions.contains(row.name.section(u'.', -1).toLower()), qPrintable(row.name));
    }
}

// 空白を含む Regex が 1 つの項として渡ること (Phase 2 冒頭の実機検証の回帰)。
//
// この PC に実在する「空白を含むファイル名」から pattern を適応的に作り、
// 名前全体を ^...$ で固定する。項が空白で割れると `regex:^<先頭語>` と
// 残りのプレーン項に分かれ、`$` を含む項は literal 扱いになって 0 件になる。
// つまり「1 件以上返り、かつ全行がその名前と一致する」ことが、割れていない
// ことの証拠になる。
void TestEverythingBackend::regexWithSpaceIsOneTerm()
{
    SKIP_WITHOUT_EVERYTHING();

    // 空白を含む名前を 1 つ探す。**この探索自体は Regex を使わない。**
    // 検証対象の機能で対象を探すと、機能が壊れたときに「見つからないので
    // QSKIP」となり回帰を素通りさせてしまう (実際にそうなることを確認した)。
    // Everything の素の構文では "..." が引用付きの部分一致になる。
    efs::SearchQuery probe;
    probe.text = QStringLiteral("\" \"");
    probe.maxResults = 20;
    const efs::SearchResults probeResults = m_backend.search(probe);
    QVERIFY(probeResults.error.isEmpty());

    QString name;
    for (const efs::ResultRow& row : probeResults.rows) {
        if (row.name.contains(u' ')) {
            name = row.name;
            break;
        }
    }
    if (name.isEmpty())
        QSKIP("この環境に空白を含むファイル名が見つからない");

    // 正規表現のメタ文字だけを退避する。空白は退避しない (それが検証対象)。
    QString escaped;
    for (const QChar ch : name) {
        if (QStringLiteral("\\^$.|?*+()[]{}").contains(ch))
            escaped += u'\\';
        escaped += ch;
    }

    efs::SearchQuery positive;
    positive.regex = true;
    positive.text = u'^' + escaped + u'$';
    positive.maxResults = 100;
    const efs::SearchResults positiveResults = m_backend.search(positive);

    QVERIFY(positiveResults.error.isEmpty());
    QVERIFY2(!positiveResults.rows.isEmpty(), qPrintable(positive.text));
    for (const efs::ResultRow& row : positiveResults.rows)
        QCOMPARE(row.name.toLower(), name.toLower()); // matchCase=false

    // 陰性対照。空白の後ろを一致しない語に変えたら 0 件でなければならない。
    efs::SearchQuery negative = positive;
    negative.text = u'^' + escaped + QStringLiteral(" ZZQXNOMATCH$");
    const efs::SearchResults negativeResults = m_backend.search(negative);
    QVERIFY(negativeResults.error.isEmpty());
    QVERIFY(negativeResults.rows.isEmpty());
}

// Regex ON のパターンは前後の空白も含めて一字も変えずに渡ること。
//
// 陽性: パターン " " (空白 1 個だけ) は「名前に空白を含む」という有効な条件。
//       trim すると空パターンになり、テキスト項ごと消えて意味が変わる。
// 対照: 同じ入力で Regex OFF なら 0 件 (Everything 側で項にならない)。
void TestEverythingBackend::regexWhitespaceIsNotTrimmed()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery spaceOnly;
    spaceOnly.regex = true;
    spaceOnly.text = QStringLiteral(" ");
    spaceOnly.maxResults = 100;
    const efs::SearchResults spaceResults = m_backend.search(spaceOnly);

    QVERIFY(spaceResults.error.isEmpty());
    if (spaceResults.rows.isEmpty())
        QSKIP("この環境に空白を含むファイル名が見つからない");
    // 全行の名前に空白が含まれること。trim されて空パターンになっていたら、
    // 空白を含まない名前が返る。
    for (const efs::ResultRow& row : spaceResults.rows)
        QVERIFY2(row.name.contains(u' '), qPrintable(row.name));

    // 先頭に空白のあるパターンも保持されること。実在する名前から作る。
    const QString name = spaceResults.rows.first().name;
    const qsizetype firstSpace = name.indexOf(u' ');
    QVERIFY(firstSpace >= 0);
    const QString tail = name.mid(firstSpace); // 必ず空白で始まる

    QString escaped;
    for (const QChar ch : tail) {
        if (QStringLiteral("\\^$.|?*+()[]{}").contains(ch))
            escaped += u'\\';
        escaped += ch;
    }

    efs::SearchQuery leadingSpace;
    leadingSpace.regex = true;
    leadingSpace.text = escaped + u'$';
    leadingSpace.maxResults = 100;
    const efs::SearchResults leadingResults = m_backend.search(leadingSpace);

    QVERIFY(leadingResults.error.isEmpty());
    QVERIFY2(!leadingResults.rows.isEmpty(), qPrintable(leadingSpace.text));
    // 先頭の空白が落ちていれば、空白の無い名前も一致しうる。全行が空白込みの
    // 末尾を持つことを確認する。
    for (const efs::ResultRow& row : leadingResults.rows)
        QVERIFY2(row.name.endsWith(tail, Qt::CaseInsensitive), qPrintable(row.name));
}

// 種別フィルタは hard constraint。ユーザーが OR 式を書いても、返る行は
// すべて種別条件を満たすこと (計画 6.2 / P2 review)。
void TestEverythingBackend::fileKindConstrainsOrExpressions()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.kind = efs::FileKind::Image;
    // 片方だけでは画像に当たりにくい語を OR で並べる。優先順位が崩れて種別が
    // OR の片側から外れると、画像でない行が混ざる。
    query.text = QStringLiteral("a|b");
    query.maxResults = 200;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    if (results.rows.isEmpty())
        QSKIP("この環境には Image 種別のファイルが無い");

    const QStringList extensions = efs::extensionsFor(efs::FileKind::Image);
    for (const efs::ResultRow& row : results.rows) {
        QVERIFY2(!row.isDir, qPrintable(row.name));
        QVERIFY2(extensions.contains(row.name.section(u'.', -1).toLower()), qPrintable(row.name));
        // OR のどちらかには一致していること (種別だけで通っていない)。
        QVERIFY2(row.name.contains(u'a', Qt::CaseInsensitive) ||
                     row.name.contains(u'b', Qt::CaseInsensitive),
                 qPrintable(row.name));
    }

    // folder: 側も同じ扱いであること。
    efs::SearchQuery directories = query;
    directories.kind = efs::FileKind::Directory;
    const efs::SearchResults directoryResults = m_backend.search(directories);
    QVERIFY(directoryResults.error.isEmpty());
    for (const efs::ResultRow& row : directoryResults.rows)
        QVERIFY2(row.isDir, qPrintable(row.name));
}

// 打ち切りが起きない小さな結果集合で、ソート指定が backend に効いていること。
// 文字列の照合順序 (Everything の collation) には踏み込まず、Asc と Desc が
// 互いの逆順であることだけを見る。
void TestEverythingBackend::sortIsAppliedByBackend()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    // このリポジトリのファイル。数件しか無いので打ち切りが起きない。
    query.text = QStringLiteral("EverythingQueryBuilder");
    query.maxResults = 5000;

    const auto keysOf = [](const efs::SearchResults& results, efs::SortKey key) {
        QStringList keys;
        for (const efs::ResultRow& row : results.rows)
            keys << (key == efs::SortKey::Name ? row.name : row.path);
        return keys;
    };

    for (const efs::SortKey key : {efs::SortKey::Name, efs::SortKey::Path}) {
        efs::SearchQuery ascending = query;
        ascending.sortKey = key;
        ascending.sortOrder = efs::SortOrder::Asc;
        const efs::SearchResults ascendingResults = m_backend.search(ascending);
        QVERIFY(ascendingResults.error.isEmpty());
        if (ascendingResults.rows.isEmpty())
            QSKIP("この環境ではリポジトリのファイルが索引されていない");
        if (ascendingResults.truncated)
            QSKIP("結果が打ち切られており全体の逆順を比較できない");

        efs::SearchQuery descending = ascending;
        descending.sortOrder = efs::SortOrder::Desc;
        const efs::SearchResults descendingResults = m_backend.search(descending);
        QVERIFY(descendingResults.error.isEmpty());
        // 行数は「ほぼ同じ」しか要求しない。live index なので 2 回のクエリの間に
        // ファイルが増減しうる (実測でも 1 件の変動を観測している)。
        if (descendingResults.rows.size() != ascendingResults.rows.size())
            QSKIP("2 回のクエリの間に索引が変化した");

        const QStringList ascendingKeys = keysOf(ascendingResults, key);
        QStringList reversed = keysOf(descendingResults, key);
        std::reverse(reversed.begin(), reversed.end());

        // Everything の照合順序 (大小・数値・ロケールの扱い) には踏み込まない。
        // キーが一意なら「Desc の逆順 == Asc」が照合順序に依らず成立する。
        // 同値キーがあると tie の並びは規定されないので、その場合は集合として
        // 一致することだけを要求する。
        const QSet<QString> unique(ascendingKeys.begin(), ascendingKeys.end());
        if (unique.size() == ascendingKeys.size()) {
            QCOMPARE(reversed, ascendingKeys);
        } else {
            QStringList sortedAscending = ascendingKeys;
            QStringList sortedDescending = reversed;
            sortedAscending.sort();
            sortedDescending.sort();
            QCOMPARE(sortedDescending, sortedAscending);
        }
    }

    // サイズ・日時は単調性で見る (同値が並ぶので逆順比較はできない)。
    efs::SearchQuery bySize = query;
    bySize.sortKey = efs::SortKey::Size;
    bySize.sortOrder = efs::SortOrder::Desc;
    const efs::SearchResults bySizeResults = m_backend.search(bySize);
    QVERIFY(bySizeResults.error.isEmpty());
    qint64 previousSize = std::numeric_limits<qint64>::max();
    for (const efs::ResultRow& row : bySizeResults.rows) {
        if (row.isDir)
            continue; // ディレクトリのサイズは -1 に潰しているので比較しない
        QVERIFY2(row.size <= previousSize, qPrintable(row.name));
        previousSize = row.size;
    }

    efs::SearchQuery byDate = query;
    byDate.sortKey = efs::SortKey::DateModified;
    byDate.sortOrder = efs::SortOrder::Desc;
    const efs::SearchResults byDateResults = m_backend.search(byDate);
    QVERIFY(byDateResults.error.isEmpty());
    QDateTime previousDate;
    for (const efs::ResultRow& row : byDateResults.rows) {
        if (!row.modified.isValid())
            continue;
        if (previousDate.isValid())
            QVERIFY2(row.modified <= previousDate, qPrintable(row.name));
        previousDate = row.modified;
    }
}

// 打ち切りが起きる広いクエリでも、ソートは 5,000 行の中だけでなく**全体**に
// 効いていること。ここが local sort との決定的な違いで、計画が backend sort を
// 必須にしている理由 (計画 7)。
void TestEverythingBackend::sortAppliesToWholeResultSetNotJustReturnedRows()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery byName;
    byName.text = QStringLiteral("e"); // 全ドライブ規模。確実に打ち切られる
    byName.sortKey = efs::SortKey::Name;
    byName.sortOrder = efs::SortOrder::Asc;
    const efs::SearchResults byNameResults = m_backend.search(byName);
    QVERIFY(byNameResults.error.isEmpty());
    if (!byNameResults.truncated)
        QSKIP("打ち切りが起きるほど広いクエリにならなかった");

    qint64 largestOnFirstPage = -1;
    for (const efs::ResultRow& row : byNameResults.rows)
        largestOnFirstPage = std::max(largestOnFirstPage, row.size);

    efs::SearchQuery bySize = byName;
    bySize.sortKey = efs::SortKey::Size;
    bySize.sortOrder = efs::SortOrder::Desc;
    const efs::SearchResults bySizeResults = m_backend.search(bySize);
    QVERIFY(bySizeResults.error.isEmpty());
    QVERIFY(!bySizeResults.rows.isEmpty());
    // totalMatches の厳密一致は要求しない。live index なので 2 回のクエリの間に
    // 件数が動く (実測で 1 件の変動を観測している)。

    // 名前順の先頭 5,000 行に含まれる最大サイズと、サイズ降順の先頭を比べる。
    //   >  : 打ち切られた 5,000 行の外にあるファイルが先頭に来た = 全体ソートの証拠
    //   == : このデータでは全体ソートと局所ソートを区別できない (偶然、名前順の
    //        先頭ページに全体の最大サイズが含まれていた場合)。判定不能なので skip。
    //   <  : 全体の最大より小さいものが先頭に来た = ソートが全体に効いていない
    const qint64 top = bySizeResults.rows.first().size;
    const QString detail =
        QStringLiteral("size desc top=%1 / name asc page max=%2").arg(top).arg(largestOnFirstPage);
    QVERIFY2(top >= largestOnFirstPage, qPrintable(detail));
    if (top == largestOnFirstPage)
        QSKIP(qPrintable(QStringLiteral("全体ソートと局所ソートを識別できない (%1)").arg(detail)));
}

QTEST_GUILESS_MAIN(TestEverythingBackend)
#include "test_everything_backend.moc"
