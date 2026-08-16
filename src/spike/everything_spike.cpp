// Phase 0 の技術調査 (計画 6.1 / 6.2、受け入れ条件 B + C)。
//
// 起動中の Everything 1.4.1 に対して、2つの問いを実機で確かめる:
//   B. Everything64.dll を動的ロードし、IPC でクエリできるか。
//   C. インライン修飾子 `regex:` は `ext:` フィルタと併用できるか。できるなら
//      EverythingQueryBuilder は Everything_SetRegex(FALSE) を維持したまま、
//      正規表現をクエリ項として表現できる。
//
// 計画に書かれた例 (`ext:jpg`、`regex:^IMG_\d+`、およびその併用) はそのまま記録
// として実行する。ただしそれらは IMG_*.jpg が1つも無い環境では 0 件になり得て、
// 0 件では何も証明できない。そのため PASS/FAIL の判定は、この機に実在する
// ファイルから期待値を導出する適応型プローブで行う。
//
// 本プログラムは Phase 0 限定。Phase 1 で本物の EverythingBackend が入った時点で
// 削除する。
#include "backend/everything/EverythingApi.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <windows.h>

#include "Everything.h"

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

struct QueryOutcome {
    bool queryReturnedTrue = false;
    DWORD errorCode = EVERYTHING_OK;
    DWORD numResults = 0; // SetMax で打ち切られた件数
    DWORD totResults = 0; // 打ち切り前の総ヒット数
    QStringList names;    // 返ってきた結果のファイル名
};

// SetSearch -> Query -> GetResult* を1周する。照合設定は MVP の既定値
// (matchPath=false / matchCase=false / 単語一致なし / グローバル regex なし)。
QueryOutcome runQuery(efs::EverythingApi& api, const QString& query, BOOL globalRegexFlag,
                      DWORD maxResults)
{
    QueryOutcome r;

    const std::wstring w = query.toStdWString();
    api.SetSearchW(w.c_str());
    api.SetRegex(globalRegexFlag);
    api.SetMatchCase(FALSE);
    api.SetMatchPath(FALSE);
    api.SetMatchWholeWord(FALSE);
    api.SetSort(EVERYTHING_SORT_NAME_ASCENDING);
    api.SetRequestFlags(EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH |
                        EVERYTHING_REQUEST_SIZE | EVERYTHING_REQUEST_DATE_MODIFIED |
                        EVERYTHING_REQUEST_ATTRIBUTES);
    api.SetOffset(0);
    api.SetMax(maxResults);

    r.queryReturnedTrue = api.QueryW(TRUE) != FALSE;
    r.errorCode = api.GetLastError();
    if (!r.queryReturnedTrue)
        return r;

    r.numResults = api.GetNumResults();
    r.totResults = api.GetTotResults();
    r.names.reserve(static_cast<int>(r.numResults));
    for (DWORD i = 0; i < r.numResults; ++i) {
        // 次の Query まで有効なポインタなので、その場でコピーする。
        r.names << QString::fromWCharArray(api.GetResultFileNameW(i));
    }
    return r;
}

void report(const QString& label, const QString& query, const QueryOutcome& r,
            BOOL globalRegexFlag = FALSE, int sampleCount = 3)
{
    out() << "  " << label << '\n';
    out() << "    query              : \"" << query << "\"\n";
    out() << "    Everything_SetRegex: " << (globalRegexFlag ? "TRUE" : "FALSE") << '\n';
    out() << "    QueryW             : " << (r.queryReturnedTrue ? "TRUE" : "FALSE") << '\n';
    out() << "    GetLastError       : " << r.errorCode << " ("
          << efs::everythingErrorText(r.errorCode) << ")\n";
    out() << "    GetNumResults      : " << r.numResults << '\n';
    out() << "    GetTotResults      : " << r.totResults << '\n';
    for (int i = 0; i < r.names.size() && i < sampleCount; ++i)
        out() << "    sample[" << i << "]          : " << r.names.at(i) << '\n';
    out() << '\n';
    out().flush();
}

// 先頭 len 文字がすべて ASCII 英数字である最初の名前から、その先頭部分を返す。
// 正規表現メタ文字のエスケープを不要にするため。
QString pickAlnumPrefix(const QStringList& names, int len)
{
    for (const QString& n : names) {
        if (n.size() < len)
            continue;
        bool ok = true;
        for (int i = 0; i < len; ++i) {
            const QChar c = n.at(i);
            if (c.unicode() >= 128 || !c.isLetterOrNumber()) {
                ok = false;
                break;
            }
        }
        if (ok)
            return n.left(len);
    }
    return {};
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::SetConsoleOutputCP(CP_UTF8);
    out().setEncoding(QStringConverter::Utf8);

    out() << "=== efs Phase 0 調査: Everything SDK ===\n\n";

    // --- B. 動的ロード --------------------------------------------------------
    efs::EverythingApi api;
    if (!api.load()) {
        out() << "[B] FAIL - Everything64.dll をロードできない\n";
        out() << "    探索した場所: " << api.searchedPaths().join(QStringLiteral(", ")) << '\n';
        out() << "    理由        : " << api.loadError() << '\n';
        out().flush();
        return 2;
    }
    out() << "[B] DLL ロード成功: " << api.dllPath() << '\n';
    out() << "    必要なエクスポートはすべて GetProcAddress で解決済み\n";

    // バージョン番号は Everything.exe から IPC で取得されるため、ここが 0 なら
    // クライアントが起動していない。
    const DWORD major = api.GetMajorVersion();
    const DWORD minor = api.GetMinorVersion();
    const DWORD rev = api.GetRevision();
    const DWORD build = api.GetBuildNumber();
    if (major == 0 && minor == 0 && rev == 0 && build == 0) {
        const DWORD err = api.GetLastError();
        out() << "[B] FAIL - Everything.exe と IPC 接続できない\n";
        out() << "    GetLastError: " << err << " (" << efs::everythingErrorText(err) << ")\n";
        out() << "    Everything が起動していない。efs は自動起動しない方針。\n";
        out().flush();
        return 3;
    }
    out() << "    Everything クライアントのバージョン (IPC 経由): " << major << '.' << minor << '.'
          << rev << '.' << build << "\n\n";
    out().flush();

    constexpr DWORD kMaxResults = 5000; // MVP の既定値

    // --- 計画に書かれた固定プローブ (記録用) ----------------------------------
    out() << "--- 固定プローブ (計画 6.2 の例) ---\n\n";

    const QString qText = QStringLiteral("Everything");
    const QString qExt = QStringLiteral("ext:jpg");
    const QString qRegex = QStringLiteral("regex:^IMG_\\d+");
    const QString qCombo = QStringLiteral("ext:jpg;jpeg;png;gif;bmp;webp regex:^IMG_\\d+");

    const QueryOutcome rText = runQuery(api, qText, FALSE, kMaxResults);
    report(QStringLiteral("P1 通常のテキスト検索 (対照)"), qText, rText);
    const QueryOutcome rExt = runQuery(api, qExt, FALSE, kMaxResults);
    report(QStringLiteral("P2 ext: フィルタ単独"), qExt, rExt);
    const QueryOutcome rRegex = runQuery(api, qRegex, FALSE, kMaxResults);
    report(QStringLiteral("P3 regex: 修飾子単独"), qRegex, rRegex);
    const QueryOutcome rCombo = runQuery(api, qCombo, FALSE, kMaxResults);
    report(QStringLiteral("P4 ext: + regex: 併用"), qCombo, rCombo);

    // グローバル regex の対照実験。種別フィルタがある場合に
    // Everything_SetRegex(TRUE) が使えないことを示す。
    const QueryOutcome rGlobal =
        runQuery(api, QStringLiteral("ext:jpg ^IMG_\\d+"), TRUE, kMaxResults);
    report(QStringLiteral("P5 Everything_SetRegex(TRUE) の対照"),
           QStringLiteral("ext:jpg ^IMG_\\d+"), rGlobal, TRUE);

    // --- 適応型プローブ (判定はこちらで行う) ----------------------------------
    out() << "--- 適応型プローブ (判定) ---\n\n";

    QString ext;
    QueryOutcome extProbe;
    for (const QString& candidate :
         {QStringLiteral("jpg"), QStringLiteral("png"), QStringLiteral("txt"),
          QStringLiteral("dll"), QStringLiteral("log"), QStringLiteral("exe")}) {
        const QString q = QStringLiteral("ext:") + candidate;
        QueryOutcome r = runQuery(api, q, FALSE, kMaxResults);
        if (r.queryReturnedTrue && r.numResults > 0) {
            ext = candidate;
            extProbe = std::move(r);
            break;
        }
    }
    if (ext.isEmpty()) {
        out() << "[C] INCONCLUSIVE - どの候補拡張子でも結果が 0 件だった。\n";
        out().flush();
        return 4;
    }

    QString prefix = pickAlnumPrefix(extProbe.names, 3);
    if (prefix.isEmpty()) {
        // その拡張子のファイルが非常に多い環境では、名前昇順の先頭ページが記号
        // 始まりの名前で埋まることがある。正規表現を使わない通常のワイルドカード
        // 項で絞り込み、英数字始まりの名前まで到達する。
        for (const QChar c : QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789")) {
            const QString q = QStringLiteral("ext:%1 %2*").arg(ext).arg(c);
            const QueryOutcome r = runQuery(api, q, FALSE, 64);
            if (!r.queryReturnedTrue || r.numResults == 0)
                continue;
            prefix = pickAlnumPrefix(r.names, 3);
            if (!prefix.isEmpty()) {
                out() << "  接頭辞の取得元   : \"" << q
                      << "\" (ワイルドカードで絞り込み。ext: の名前昇順先頭ページが"
                         "すべて記号始まりだったため)\n";
                break;
            }
        }
    }
    if (prefix.isEmpty()) {
        out() << "[C] INCONCLUSIVE - ext:" << ext
              << " から正規表現用の 3 文字 ASCII 英数字接頭辞を導出できなかった。\n";
        out().flush();
        return 4;
    }

    out() << "  判定用の拡張子   : " << ext << " (ext:" << ext << " -> " << extProbe.numResults
          << " 件)\n";
    out() << "  導出した接頭辞   : \"" << prefix << "\" (実在するファイル名から取得)\n\n";

    const QString aExt = QStringLiteral("ext:%1").arg(ext);
    const QString aRegex = QStringLiteral("regex:^%1").arg(prefix);
    const QString aCombo = QStringLiteral("ext:%1 regex:^%2").arg(ext, prefix);
    const QString aComboNeg = QStringLiteral("ext:%1 regex:^ZZQXNOMATCH").arg(ext);

    report(QStringLiteral("A1 ext: 単独"), aExt, extProbe);
    const QueryOutcome aR = runQuery(api, aRegex, FALSE, kMaxResults);
    report(QStringLiteral("A2 regex: 単独"), aRegex, aR);
    const QueryOutcome aC = runQuery(api, aCombo, FALSE, kMaxResults);
    report(QStringLiteral("A3 ext: + regex: 併用 (陽性)"), aCombo, aC);
    const QueryOutcome aN = runQuery(api, aComboNeg, FALSE, kMaxResults);
    report(QStringLiteral("A4 ext: + regex: 併用 (陰性対照)"), aComboNeg, aN);

    // --- 判定 -----------------------------------------------------------------
    const QString dotExt = QStringLiteral(".") + ext;

    int comboWrongExt = 0;
    int comboWrongPrefix = 0;
    QString firstBadRow;
    for (const QString& n : aC.names) {
        const bool extOk = n.endsWith(dotExt, Qt::CaseInsensitive);
        const bool preOk = n.startsWith(prefix, Qt::CaseInsensitive);
        if (!extOk)
            ++comboWrongExt;
        if (!preOk)
            ++comboWrongPrefix;
        if ((!extOk || !preOk) && firstBadRow.isEmpty())
            firstBadRow = n;
    }

    int regexOnlyWrongPrefix = 0;
    for (const QString& n : aR.names) {
        if (!n.startsWith(prefix, Qt::CaseInsensitive))
            ++regexOnlyWrongPrefix;
    }

    struct Check {
        const char* description;
        bool passed;
        QString observed;
    };

    const QList<Check> checks = {
        {.description = "A2 regex: 単独が 1 件以上返る",
         .passed = aR.queryReturnedTrue && aR.numResults > 0,
         .observed = QStringLiteral("num=%1").arg(aR.numResults)},
        {.description = "A2 regex: 単独の全行がパターンに一致 (リテラル扱いされていない)",
         .passed = regexOnlyWrongPrefix == 0,
         .observed = QStringLiteral("%1 / %2 行が ^%3 に不一致")
                         .arg(regexOnlyWrongPrefix)
                         .arg(aR.names.size())
                         .arg(prefix)},
        {.description = "A3 併用が 1 件以上返る",
         .passed = aC.queryReturnedTrue && aC.numResults > 0,
         .observed = QStringLiteral("num=%1").arg(aC.numResults)},
        {.description = "A3 併用の全行が ext: 条件を満たす",
         .passed = comboWrongExt == 0,
         .observed = QStringLiteral("%1 / %2 行が %3 で終わらない")
                         .arg(comboWrongExt)
                         .arg(aC.names.size())
                         .arg(dotExt)},
        {.description = "A3 併用の全行が regex: 条件を満たす",
         .passed = comboWrongPrefix == 0,
         .observed = QStringLiteral("%1 / %2 行が ^%3 に不一致")
                         .arg(comboWrongPrefix)
                         .arg(aC.names.size())
                         .arg(prefix)},
        {.description = "A3 併用は A1 の絞り込みになっている (regex: 項が無視されていない)",
         .passed = aC.totResults <= extProbe.totResults,
         .observed = QStringLiteral("併用 tot=%1 <= ext tot=%2")
                         .arg(aC.totResults)
                         .arg(extProbe.totResults)},
        {.description = "A3 併用は A2 の絞り込みになっている (ext: 項が無視されていない)",
         .passed = aC.totResults <= aR.totResults,
         .observed =
             QStringLiteral("併用 tot=%1 <= regex tot=%2").arg(aC.totResults).arg(aR.totResults)},
        {.description = "A4 陰性対照が 0 件 (2 つの項が AND 結合されている)",
         .passed = aN.queryReturnedTrue && aN.numResults == 0,
         .observed = QStringLiteral("num=%1").arg(aN.numResults)},
    };

    out() << "--- 判定: ext: と regex: の併用可否 (計画 6.2) ---\n\n";
    bool allPassed = true;
    for (const Check& c : checks) {
        out() << "  [" << (c.passed ? "PASS" : "FAIL") << "] " << c.description << "  -- "
              << c.observed << '\n';
        allPassed = allPassed && c.passed;
    }
    if (!firstBadRow.isEmpty())
        out() << "\n  最初に条件を満たさなかった行: " << firstBadRow << '\n';

    out() << '\n';
    if (allPassed) {
        out() << "[C] PASS - インライン修飾子 regex: は ext: と併用できる。\n"
                 "    EverythingQueryBuilder は Everything_SetRegex(FALSE) を維持したまま\n"
                 "    \"<拡張子前置詞> regex:<パターン>\" を組み立ててよい。\n";
    } else {
        out() << "[C] FAIL - ext: + regex: の併用が計画の前提どおりに動作しない。\n"
                 "    フォールバックは実装せず停止する。根拠は上記の観測結果を参照。\n";
    }
    out().flush();
    return allPassed ? 0 : 1;
}
