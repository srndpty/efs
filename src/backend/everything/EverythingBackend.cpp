#include "backend/everything/EverythingBackend.h"

#include "backend/everything/EverythingQueryBuilder.h"

#include <QElapsedTimer>
#include <QTimeZone>
#include <QtGlobal>

#include <limits>
#include <string>
#include <utility>

#include "Everything.h"

namespace efs {

namespace {

// FILETIME と Unix エポックの差 (100ns 単位)。
constexpr quint64 kFileTimeEpochDelta = 116444736000000000ULL;

DWORD toEverythingSort(SortKey key, SortOrder order)
{
    const bool asc = order == SortOrder::Asc;
    switch (key) {
    case SortKey::Name:
        return asc ? EVERYTHING_SORT_NAME_ASCENDING : EVERYTHING_SORT_NAME_DESCENDING;
    case SortKey::Path:
        return asc ? EVERYTHING_SORT_PATH_ASCENDING : EVERYTHING_SORT_PATH_DESCENDING;
    case SortKey::Size:
        return asc ? EVERYTHING_SORT_SIZE_ASCENDING : EVERYTHING_SORT_SIZE_DESCENDING;
    case SortKey::DateModified:
        return asc ? EVERYTHING_SORT_DATE_MODIFIED_ASCENDING
                   : EVERYTHING_SORT_DATE_MODIFIED_DESCENDING;
    }
    return EVERYTHING_SORT_NAME_ASCENDING;
}

// ステータスバーに出す英語メッセージ。everythingErrorText() は開発者向けの
// 日本語診断なので用途が別 (AGENTS.md 言語ポリシー)。
QString searchErrorText(DWORD code)
{
    switch (code) {
    case EVERYTHING_ERROR_IPC:
        return QStringLiteral("Everything is not running.");
    case EVERYTHING_ERROR_MEMORY:
        return QStringLiteral("Out of memory.");
    default:
        return QStringLiteral("Search failed (code=%1).").arg(code);
    }
}

quint64 toQuint64(const FILETIME& fileTime)
{
    return (static_cast<quint64>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}

} // namespace

QDateTime fileTimeToDateTime(quint64 fileTime)
{
    // 0 は「日時なし」。
    if (fileTime == 0)
        return {};
    // qint64 で表せない値は日時ではない (SDK が 0xFFFF... 等を返した場合)。
    // 黙って 1601 年付近の値へ化けさせず、無効として返す。
    if (fileTime > static_cast<quint64>(std::numeric_limits<qint64>::max()))
        return {};

    // 1970 より前は負の Unix epoch time になる。QDateTime は表現できるので、
    // 切り捨てずにそのまま変換する。
    const qint64 ticks = static_cast<qint64>(fileTime) - static_cast<qint64>(kFileTimeEpochDelta);
    return QDateTime::fromMSecsSinceEpoch(ticks / 10000, QTimeZone::UTC).toLocalTime();
}

QString EverythingBackend::name() const
{
    return QStringLiteral("Everything");
}

bool EverythingBackend::isAvailable(QString* reason) const
{
    if (m_api.load())
        return true;
    if (reason)
        *reason = m_api.loadError();
    return false;
}

SearchResults EverythingBackend::search(const SearchQuery& query)
{
    SearchResults results;
    results.id = query.id;

    QElapsedTimer timer;
    timer.start();

    QString reason;
    if (!isAvailable(&reason)) {
        // 詳細 (日本語) は開発者向けのログへ。UI には英語の要約だけを出す。
        qWarning("Everything64.dll をロードできない: %s", qUtf8Printable(reason));
        results.error = QStringLiteral("Everything SDK is not available.");
        results.elapsedMs = timer.elapsed();
        return results;
    }

    const std::wstring queryString = buildQueryString(query).toStdWString();
    m_api.SetSearchW(queryString.c_str());
    // 正規表現はクエリ文字列内の regex: 項で表現する (Phase 0 で確定)。
    m_api.SetRegex(FALSE);
    m_api.SetMatchCase(query.matchCase ? TRUE : FALSE);
    m_api.SetMatchPath(query.matchPath ? TRUE : FALSE);
    m_api.SetMatchWholeWord(FALSE);
    m_api.SetSort(toEverythingSort(query.sortKey, query.sortOrder));
    // size / modified はここで一括取得する。行ごとに QFileInfo を呼ぶと
    // ディスク I/O で固まるため厳禁 (計画 5 の補足)。
    m_api.SetRequestFlags(EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH |
                          EVERYTHING_REQUEST_SIZE | EVERYTHING_REQUEST_DATE_MODIFIED |
                          EVERYTHING_REQUEST_ATTRIBUTES);
    m_api.SetOffset(0);
    m_api.SetMax(query.maxResults > 0 ? static_cast<DWORD>(query.maxResults) : 0);

    if (!m_api.QueryW(TRUE)) {
        results.error = searchErrorText(m_api.GetLastError());
        results.elapsedMs = timer.elapsed();
        return results;
    }

    const DWORD count = m_api.GetNumResults();
    results.rows.reserve(static_cast<qsizetype>(count));
    for (DWORD i = 0; i < count; ++i) {
        ResultRow row;
        // GetResult* が返すポインタは次の Query まで有効。その場でコピーする。
        row.name = QString::fromWCharArray(m_api.GetResultFileNameW(i));
        row.path = QString::fromWCharArray(m_api.GetResultPathW(i));
        row.isDir = m_api.IsFolderResult(i) != FALSE;

        LARGE_INTEGER size{};
        if (!row.isDir && m_api.GetResultSize(i, &size))
            row.size = size.QuadPart;

        FILETIME modified{};
        if (m_api.GetResultDateModified(i, &modified))
            row.modified = fileTimeToDateTime(toQuint64(modified));

        results.rows.push_back(std::move(row));
    }

    results.totalMatches = m_api.GetTotResults();
    // totalMatches は符号なし、rows.size() は符号付き。キャストで黙らせず
    // std::cmp_greater で比較する。
    results.truncated = std::cmp_greater(results.totalMatches, results.rows.size());

    // 接続先の版を確認する。**結果を読み終えた後に行う** — 版の問い合わせも IPC
    // なので、GetResult* が指すバッファを触りうる呼び出しを間に挟まない。
    // 初回の 1 回だけなので、対象外だったときに 1 本ぶんの読み取りが無駄になる
    // 程度のコストで済む。
    if (const QString unsupported = checkServerVersion(); !unsupported.isEmpty()) {
        results.rows.clear();
        results.totalMatches = 0;
        results.truncated = false;
        results.error = unsupported;
    }

    results.elapsedMs = timer.elapsed();
    return results;
}

QString EverythingBackend::checkServerVersion()
{
    if (m_versionChecked)
        return m_versionError;
    m_versionChecked = true;

    // 対象は 1.4 系だけ (AGENTS.md「Everything 1.5 は対象外」)。regex の引用、
    // `<>` グルーピング、sort の意味論はどれも 1.4.1.1022 の実測で確定した契約
    // なので、別系統の版に黙って別の意味で動かさせない。**build number は gate に
    // しない** — 同じ 1.4 の別 build を弾く理由が無いのでログに出すだけ。
    constexpr DWORD kSupportedMajor = 1;
    constexpr DWORD kSupportedMinor = 4;

    const DWORD major = m_api.GetMajorVersion();
    const DWORD minor = m_api.GetMinorVersion();
    const QString version = QStringLiteral("%1.%2.%3.%4")
                                .arg(major)
                                .arg(minor)
                                .arg(m_api.GetRevision())
                                .arg(m_api.GetBuildNumber());

    if (major != kSupportedMajor || minor != kSupportedMinor) {
        qWarning("対象外の Everything に接続した: %s (efs が対象とするのは 1.4 系のみ)",
                 qUtf8Printable(version));
        m_versionError =
            QStringLiteral("Unsupported Everything version %1 (efs supports 1.4 only).")
                .arg(version);
        return m_versionError;
    }

    qInfo("Everything %s に接続した", qUtf8Printable(version));
    return {};
}

} // namespace efs
