#include "backend/everything/EverythingQueryBuilder.h"

#include "core/FileKinds.h"

#include <QStringList>

namespace efs {

QString buildQueryString(const SearchQuery& query)
{
    QStringList terms;

    if (query.kind == FileKind::Directory) {
        terms << QStringLiteral("folder:");
    } else {
        const QStringList extensions = extensionsFor(query.kind);
        if (!extensions.isEmpty())
            terms << QStringLiteral("ext:") + extensions.join(u';');
    }

    const QString text = query.text.trimmed();
    if (!text.isEmpty()) {
        // Regex は必ず " " で囲む。囲まないと空白 (と TAB) が Everything の
        // 検索語区切りとして先に解釈され、1 つのパターンが複数の項へ割れる
        // (Phase 2 冒頭の実機検証。README 参照)。
        terms << (query.regex ? QStringLiteral("regex:\"") + text + u'"' : text);
    }

    return terms.join(u' ');
}

} // namespace efs
