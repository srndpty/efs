#include "backend/everything/EverythingBackend.h"

#include "backend/everything/EverythingQueryBuilder.h"

#include <QElapsedTimer>
#include <QTimeZone>
#include <QtGlobal>

#include <string>

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
    if (fileTime < kFileTimeEpochDelta)
        return {};
    const auto msecs = static_cast<qint64>((fileTime - kFileTimeEpochDelta) / 10000ULL);
    return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC).toLocalTime();
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
    results.truncated = results.totalMatches > static_cast<quint64>(results.rows.size());
    results.elapsedMs = timer.elapsed();
    return results;
}

} // namespace efs
