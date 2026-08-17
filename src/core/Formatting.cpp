#include "core/Formatting.h"

#include <QLocale>

namespace efs {

QString formatSize(qint64 size)
{
    if (size < 0)
        return {};
    return QLocale().formattedDataSize(size);
}

QString formatModified(const QDateTime& modified)
{
    if (!modified.isValid())
        return QStringLiteral("-");
    return QLocale().toString(modified, QLocale::ShortFormat);
}

QString formatStatus(const SearchResults& results)
{
    // 「正常に 0 件」と「検索できなかった」を取り違えないよう、失敗は必ず
    // "Search failed:" で始める。0 件は "0 results / N ms" のままなので、
    // ステータスバーの文言だけで区別できる (Phase 3)。
    // backend の詳細診断 (DLL のパス等) は qWarning へ流してあり、ここには出さない。
    if (!results.error.isEmpty())
        return QStringLiteral("Search failed: %1").arg(results.error);

    const QLocale locale;
    const auto shown = static_cast<quint64>(results.rows.size());
    const QString count = QStringLiteral("%1 %2").arg(
        locale.toString(results.totalMatches),
        results.totalMatches == 1 ? QStringLiteral("result") : QStringLiteral("results"));
    const QString elapsed = QStringLiteral("%1 ms").arg(locale.toString(results.elapsedMs));

    if (!results.truncated)
        return QStringLiteral("%1 / %2").arg(count, elapsed);

    return QStringLiteral("%1 (showing first %2) / %3").arg(count, locale.toString(shown), elapsed);
}

} // namespace efs
