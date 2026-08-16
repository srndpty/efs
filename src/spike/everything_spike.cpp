// Phase 0 technical spike (plan sections 6.1 / 6.2, acceptance B + C).
//
// Two questions, answered against the live Everything 1.4.1 instance:
//   B. Can Everything64.dll be loaded dynamically and queried over IPC?
//   C. Does the inline `regex:` modifier compose with an `ext:` filter, so that
//      EverythingQueryBuilder can keep Everything_SetRegex(FALSE) permanently
//      and express regex as a query term?
//
// The plan's literal example probes (`ext:jpg`, `regex:^IMG_\d+`, and their
// combination) are run verbatim for the record. Those can legitimately return 0
// results on a machine with no IMG_*.jpg files, which would prove nothing, so
// the PASS/FAIL verdict is decided by adaptive probes whose expected outcome is
// derived from files that actually exist on this machine.
//
// This program is Phase 0 scaffolding and is removed when Phase 1 lands the real
// EverythingBackend.
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
    bool        queryReturnedTrue = false;
    DWORD       errorCode = EVERYTHING_OK;
    DWORD       numResults = 0;   // capped by SetMax
    DWORD       totResults = 0;   // total matches before the cap
    QStringList names;            // file names of the returned results
};

// One full SetSearch -> Query -> GetResult* cycle with the defaults decided for
// the MVP: matchPath=false, matchCase=false, whole word off, global regex off.
QueryOutcome runQuery(efs::EverythingApi& api,
                      const QString& query,
                      BOOL globalRegexFlag,
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
    api.SetRequestFlags(EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH
                        | EVERYTHING_REQUEST_SIZE
                        | EVERYTHING_REQUEST_DATE_MODIFIED
                        | EVERYTHING_REQUEST_ATTRIBUTES);
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
        // Valid only until the next Query, so copy immediately.
        r.names << QString::fromWCharArray(api.GetResultFileNameW(i));
    }
    return r;
}

void report(const QString& label, const QString& query, const QueryOutcome& r,
            BOOL globalRegexFlag = FALSE, int sampleCount = 3)
{
    out() << "  " << label << '\n';
    out() << "    query            : \"" << query << "\"\n";
    out() << "    Everything_SetRegex: " << (globalRegexFlag ? "TRUE" : "FALSE") << '\n';
    out() << "    QueryW returned  : " << (r.queryReturnedTrue ? "TRUE" : "FALSE") << '\n';
    out() << "    GetLastError     : " << r.errorCode << " ("
          << efs::everythingErrorText(r.errorCode) << ")\n";
    out() << "    GetNumResults    : " << r.numResults << '\n';
    out() << "    GetTotResults    : " << r.totResults << '\n';
    for (int i = 0; i < r.names.size() && i < sampleCount; ++i)
        out() << "    sample[" << i << "]        : " << r.names.at(i) << '\n';
    out() << '\n';
    out().flush();
}

// Leading `len` characters of the first name that starts with that many ASCII
// alphanumerics, so the derived regex needs no metacharacter escaping.
QString pickAlnumPrefix(const QStringList& names, int len)
{
    for (const QString& n : names) {
        if (n.size() < len)
            continue;
        bool ok = true;
        for (int i = 0; i < len; ++i) {
            const QChar c = n.at(i);
            if (!(c.unicode() < 128 && c.isLetterOrNumber())) {
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

    out() << "=== efs Phase 0 spike: Everything SDK ===\n\n";

    // --- B. dynamic load ------------------------------------------------------
    efs::EverythingApi api;
    if (!api.load()) {
        out() << "[B] FAIL - could not load Everything64.dll\n";
        out() << "    searched: " << api.searchedPaths().join(QStringLiteral(", ")) << '\n';
        out() << "    reason  : " << api.loadError() << '\n';
        out().flush();
        return 2;
    }
    out() << "[B] DLL loaded: " << api.dllPath() << '\n';
    out() << "    all required exports resolved via GetProcAddress\n";

    // Also an IPC round-trip: the version numbers come from Everything.exe, so
    // a failure here means the client is not running.
    const DWORD major = api.GetMajorVersion();
    const DWORD minor = api.GetMinorVersion();
    const DWORD rev = api.GetRevision();
    const DWORD build = api.GetBuildNumber();
    if (major == 0 && minor == 0 && rev == 0 && build == 0) {
        const DWORD err = api.GetLastError();
        out() << "[B] FAIL - no IPC connection to Everything.exe\n";
        out() << "    GetLastError: " << err << " (" << efs::everythingErrorText(err) << ")\n";
        out() << "    Everything is not running; efs does not start it automatically.\n";
        out().flush();
        return 3;
    }
    out() << "    Everything client version (via IPC): " << major << '.' << minor
          << '.' << rev << '.' << build << "\n\n";
    out().flush();

    constexpr DWORD kMaxResults = 5000; // MVP default

    // --- Fixed probes from the plan, run verbatim for the record --------------
    out() << "--- Fixed probes (plan section 6.2 examples) ---\n\n";

    const QString qText  = QStringLiteral("Everything");
    const QString qExt   = QStringLiteral("ext:jpg");
    const QString qRegex = QStringLiteral("regex:^IMG_\\d+");
    const QString qCombo = QStringLiteral("ext:jpg;jpeg;png;gif;bmp;webp regex:^IMG_\\d+");

    const QueryOutcome rText  = runQuery(api, qText, FALSE, kMaxResults);
    report(QStringLiteral("P1 ordinary text (control)"), qText, rText);
    const QueryOutcome rExt   = runQuery(api, qExt, FALSE, kMaxResults);
    report(QStringLiteral("P2 ext: filter only"), qExt, rExt);
    const QueryOutcome rRegex = runQuery(api, qRegex, FALSE, kMaxResults);
    report(QStringLiteral("P3 regex: modifier only"), qRegex, rRegex);
    const QueryOutcome rCombo = runQuery(api, qCombo, FALSE, kMaxResults);
    report(QStringLiteral("P4 ext: + regex: combined"), qCombo, rCombo);

    // Global-regex control: proves that Everything_SetRegex(TRUE) is the wrong
    // lever when a kind filter is present (the whole string becomes the pattern).
    const QueryOutcome rGlobal = runQuery(api, QStringLiteral("ext:jpg ^IMG_\\d+"), TRUE, kMaxResults);
    report(QStringLiteral("P5 Everything_SetRegex(TRUE) control"),
           QStringLiteral("ext:jpg ^IMG_\\d+"), rGlobal, TRUE);

    // --- Adaptive probes: these decide the verdict ----------------------------
    out() << "--- Adaptive probes (verdict) ---\n\n";

    QString ext;
    QueryOutcome extProbe;
    for (const QString& candidate : {QStringLiteral("jpg"), QStringLiteral("png"),
                                     QStringLiteral("txt"), QStringLiteral("dll"),
                                     QStringLiteral("log"), QStringLiteral("exe")}) {
        const QString q = QStringLiteral("ext:") + candidate;
        QueryOutcome r = runQuery(api, q, FALSE, kMaxResults);
        if (r.queryReturnedTrue && r.numResults > 0) {
            ext = candidate;
            extProbe = std::move(r);
            break;
        }
    }
    if (ext.isEmpty()) {
        out() << "[C] INCONCLUSIVE - no candidate extension returned any result.\n";
        out().flush();
        return 4;
    }

    QString prefix = pickAlnumPrefix(extProbe.names, 3);
    if (prefix.isEmpty()) {
        // The name-sorted first page can be entirely punctuation-leading on a
        // machine with very many files of this extension. Narrow with a plain
        // wildcard term (ordinary Everything syntax, no regex involved) to reach
        // names that start with a given alphanumeric character.
        for (const QChar c : QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789")) {
            const QString q = QStringLiteral("ext:%1 %2*").arg(ext).arg(c);
            const QueryOutcome r = runQuery(api, q, FALSE, 64);
            if (!r.queryReturnedTrue || r.numResults == 0)
                continue;
            prefix = pickAlnumPrefix(r.names, 3);
            if (!prefix.isEmpty()) {
                out() << "  prefix source    : \"" << q
                      << "\" (wildcard narrowing; the name-sorted ext: page was "
                         "entirely punctuation-leading)\n";
                break;
            }
        }
    }
    if (prefix.isEmpty()) {
        out() << "[C] INCONCLUSIVE - could not derive a 3-char ASCII alphanumeric "
                 "file-name prefix for ext:" << ext << " to build a regex from.\n";
        out().flush();
        return 4;
    }

    out() << "  probe extension  : " << ext << " (ext:" << ext << " -> "
          << extProbe.numResults << " results)\n";
    out() << "  derived prefix   : \"" << prefix << "\" (from an existing file name)\n\n";

    const QString aExt      = QStringLiteral("ext:%1").arg(ext);
    const QString aRegex    = QStringLiteral("regex:^%1").arg(prefix);
    const QString aCombo    = QStringLiteral("ext:%1 regex:^%2").arg(ext, prefix);
    const QString aComboNeg = QStringLiteral("ext:%1 regex:^ZZQXNOMATCH").arg(ext);

    report(QStringLiteral("A1 ext: only"), aExt, extProbe);
    const QueryOutcome aR = runQuery(api, aRegex, FALSE, kMaxResults);
    report(QStringLiteral("A2 regex: only"), aRegex, aR);
    const QueryOutcome aC = runQuery(api, aCombo, FALSE, kMaxResults);
    report(QStringLiteral("A3 ext: + regex: (positive)"), aCombo, aC);
    const QueryOutcome aN = runQuery(api, aComboNeg, FALSE, kMaxResults);
    report(QStringLiteral("A4 ext: + regex: (negative control)"), aComboNeg, aN);

    // --- Verdict --------------------------------------------------------------
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
        {"A2 regex: only returns at least one result",
         aR.queryReturnedTrue && aR.numResults > 0,
         QStringLiteral("num=%1").arg(aR.numResults)},
        {"A2 every regex:-only row matches the pattern (regex is honoured, not literal)",
         regexOnlyWrongPrefix == 0,
         QStringLiteral("%1 of %2 rows violate ^%3")
             .arg(regexOnlyWrongPrefix).arg(aR.names.size()).arg(prefix)},
        {"A3 combination returns at least one result",
         aC.queryReturnedTrue && aC.numResults > 0,
         QStringLiteral("num=%1").arg(aC.numResults)},
        {"A3 every combined row satisfies the ext: term",
         comboWrongExt == 0,
         QStringLiteral("%1 of %2 rows do not end in %3")
             .arg(comboWrongExt).arg(aC.names.size()).arg(dotExt)},
        {"A3 every combined row satisfies the regex: term",
         comboWrongPrefix == 0,
         QStringLiteral("%1 of %2 rows do not match ^%3")
             .arg(comboWrongPrefix).arg(aC.names.size()).arg(prefix)},
        {"A3 combination is a strict narrowing of A1 (regex term is not ignored)",
         aC.totResults <= extProbe.totResults,
         QStringLiteral("combo tot=%1 <= ext tot=%2")
             .arg(aC.totResults).arg(extProbe.totResults)},
        {"A3 combination is a narrowing of A2 (ext term is not ignored)",
         aC.totResults <= aR.totResults,
         QStringLiteral("combo tot=%1 <= regex tot=%2")
             .arg(aC.totResults).arg(aR.totResults)},
        {"A4 negative control returns zero results (terms are AND-ed)",
         aN.queryReturnedTrue && aN.numResults == 0,
         QStringLiteral("num=%1").arg(aN.numResults)},
    };

    out() << "--- Verdict: ext: + regex: composability (plan section 6.2) ---\n\n";
    bool allPassed = true;
    for (const Check& c : checks) {
        out() << "  [" << (c.passed ? "PASS" : "FAIL") << "] " << c.description
              << "  -- " << c.observed << '\n';
        allPassed = allPassed && c.passed;
    }
    if (!firstBadRow.isEmpty())
        out() << "\n  first violating combined row: " << firstBadRow << '\n';

    out() << '\n';
    if (allPassed) {
        out() << "[C] PASS - the inline regex: modifier composes with ext:.\n"
                 "    EverythingQueryBuilder can keep Everything_SetRegex(FALSE)\n"
                 "    permanently and emit \"<extPrefix> regex:<pattern>\".\n";
    } else {
        out() << "[C] FAIL - the ext: + regex: combination does not behave as the plan assumes.\n"
                 "    Stopping without implementing a fallback; see the evidence above.\n";
    }
    out().flush();
    return allPassed ? 0 : 1;
}
