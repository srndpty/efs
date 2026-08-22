#include "core/PathUtils.h"

#include <QRegularExpression>

namespace efs {

namespace {

constexpr QChar kSeparator = u'\\';

bool endsWithSeparator(const QString& path)
{
    return path.endsWith(u'\\') || path.endsWith(u'/');
}

// 項の先頭が Everything の関数構文か (`dm:2024/01/01`、`size:>1mb` 等)。
//
// 名前部分を 2 文字以上とすることで、ドライブ付きパス (`C:/dev`) を外す。
// 先頭の `!` (否定) は項の一部ではないので読み飛ばす。
bool startsFunctionTerm(QStringView term)
{
    static const QRegularExpression re(QStringLiteral(R"(^!*[A-Za-z][A-Za-z0-9_-]+:)"));
    return re.matchView(term).hasMatch();
}

// "C:" のようなドライブ指定 1 つだけか。
bool isBareDrive(const QString& name)
{
    return name.size() == 2 && name.at(1) == u':' && name.at(0).isLetter();
}

} // namespace

QString fullPath(const ResultRow& row)
{
    if (row.name.isEmpty())
        return row.path;

    if (row.path.isEmpty()) {
        // ドライブそのもの。裸の "C:" は相対パスなので必ず区切りを付ける。
        if (isBareDrive(row.name))
            return row.name + kSeparator;
        return row.name;
    }

    if (endsWithSeparator(row.path))
        return row.path + row.name;

    return row.path + kSeparator + row.name;
}

QString normalizeQuerySeparators(const QString& text)
{
    QString normalized = text;
    qsizetype termStart = 0; // 走査中の項の先頭 (関数構文かの判定に使う)

    for (qsizetype i = 0; i < normalized.size(); ++i) {
        const QChar ch = normalized.at(i);
        // Everything の項区切り (空白 = AND、`|` = OR) とグルーピングの `<` `>`。
        // 引用は項を割らないが、`"..."` の内側にも関数構文は書けないので、
        // ここでは引用の状態を追わない (項の切れ目が多めに入るだけで害が無い)。
        if (ch.isSpace() || ch == u'|' || ch == u'<' || ch == u'>') {
            termStart = i + 1;
            continue;
        }
        if (ch == u'/' &&
            !startsFunctionTerm(QStringView(normalized).mid(termStart, i - termStart)))
            normalized[i] = kSeparator;
    }
    return normalized;
}

} // namespace efs
