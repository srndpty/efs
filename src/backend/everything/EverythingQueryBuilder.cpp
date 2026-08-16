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
    if (!text.isEmpty())
        terms << (query.regex ? QStringLiteral("regex:") + text : text);

    return terms.join(u' ');
}

} // namespace efs
