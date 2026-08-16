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

    // Regex ON ではユーザーのパターンを一字も変えない。引用の内側では前後の
    // 空白も TAB も意味を持つため (実機検証済み)、trim すると別のパターンになる。
    // Regex OFF の前後空白は Everything の項区切りでしかないので落とす。
    const QString text = query.regex ? query.text : query.text.trimmed();
    if (text.isEmpty())
        return terms.join(u' ');

    if (query.regex) {
        // Regex は必ず " " で囲む。囲まないと空白 (と TAB) が Everything の
        // 検索語区切りとして先に解釈され、1 つのパターンが複数の項へ割れる
        // (Phase 2 冒頭の実機検証。README 参照)。引用済みの 1 項なので、
        // 下の <> グルーピングは不要。
        terms << QStringLiteral("regex:\"") + text + u'"';
    } else if (terms.isEmpty()) {
        // 種別項が無いなら結合の優先順位を気にする必要が無い。入力をそのまま渡す。
        terms << text;
    } else {
        // 種別項があるときは、ユーザー式全体を <> で囲んで種別を外側の AND に
        // 固定する。Everything の演算子優先順位は設定で変えられるため、
        // `ext:... a|b` のままだと種別が OR の片側から外れうる。
        // (実測: 現在の既定設定では <> の有無で結果は変わらない。README 参照)
        terms << u'<' + text + u'>';
    }

    return terms.join(u' ');
}

} // namespace efs
