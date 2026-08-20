#include "core/MatchHighlight.h"

#include <algorithm>

namespace efs {

namespace {

// Everything の関数構文 (`ext:jpg`、`size:>1mb`、`dm:today` 等) か。
//
// 値そのものを強調しても意味が無い (ファイル名の文字列ではない) ので落とす。
// `C:\dev` のようなドライブ付きパスと区別するため、名前部分は 2 文字以上とし、
// 値がパス区切りで始まるものは除く。
bool isFunctionTerm(const QString& term)
{
    static const QRegularExpression re(QStringLiteral(R"(^[A-Za-z][A-Za-z0-9_-]+:(?![\\/]))"));
    return re.match(term).hasMatch();
}

// ユーザー入力を Everything の項へ分解する。
//
// 区切りは空白 (= AND) と `|` (= OR)。`"..."` の内側では空白を区切りにしない。
// グルーピングの `<` `>` は項ではないので区切りとして捨てる。
QList<QString> splitTerms(const QString& text)
{
    QList<QString> terms;
    QString current;
    bool inQuote = false;

    const auto flush = [&] {
        if (!current.isEmpty())
            terms.append(current);
        current.clear();
    };

    for (const QChar ch : text) {
        if (ch == u'"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && (ch.isSpace() || ch == u'|' || ch == u'<' || ch == u'>')) {
            flush();
            continue;
        }
        current.append(ch);
    }
    flush();
    return terms;
}

// 1 項 → 正規表現。強調できない項 (除外・関数構文) では無効な値を返す。
QRegularExpression patternForTerm(const QString& term, QRegularExpression::PatternOptions options)
{
    // 除外項は「一致しない行」を選ぶための条件なので、強調する対象が無い。
    if (term.startsWith(u'!') || isFunctionTerm(term))
        return {};

    const QString escaped = QRegularExpression::escape(term);
    if (!term.contains(u'*') && !term.contains(u'?'))
        return QRegularExpression(escaped, options);

    // ワイルドカードを含む項は、Everything では名前**全体**との一致になる。
    QString pattern = escaped;
    pattern.replace(QLatin1String("\\*"), QLatin1String(".*"));
    pattern.replace(QLatin1String("\\?"), QLatin1String("."));
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern), options);
}

} // namespace

MatchHighlighter::MatchHighlighter(const QString& queryText, bool regex, bool matchCase)
{
    const QRegularExpression::PatternOptions options =
        matchCase ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;

    if (regex) {
        // Regex ON では入力を一字も解釈し直さない (EverythingQueryBuilder と同じ契約)。
        if (queryText.isEmpty())
            return;
        const QRegularExpression re(queryText, options);
        if (re.isValid())
            m_patterns.append(re);
        return;
    }

    for (const QString& term : splitTerms(queryText)) {
        const QRegularExpression re = patternForTerm(term, options);
        if (re.isValid() && !re.pattern().isEmpty())
            m_patterns.append(re);
    }
}

QList<MatchRange> MatchHighlighter::ranges(const QString& text) const
{
    if (m_patterns.isEmpty() || text.isEmpty())
        return {};

    QList<MatchRange> found;
    for (const QRegularExpression& re : m_patterns) {
        QRegularExpressionMatchIterator it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int length = static_cast<int>(match.capturedLength());
            // 空一致 (`a*` が 0 文字に当たる等) は強調できないので捨てる。
            if (length > 0)
                found.append({static_cast<int>(match.capturedStart()), length});
        }
    }
    if (found.isEmpty())
        return {};

    std::sort(found.begin(), found.end(),
              [](const MatchRange& a, const MatchRange& b) { return a.start < b.start; });

    // 項どうしの重なりをそのまま返すと、描画側が同じ文字を二重に扱うことになる。
    QList<MatchRange> merged;
    merged.append(found.first());
    for (const MatchRange& range : found) {
        MatchRange& last = merged.last();
        const int lastEnd = last.start + last.length;
        if (range.start <= lastEnd)
            last.length = std::max(lastEnd, range.start + range.length) - last.start;
        else
            merged.append(range);
    }
    return merged;
}

} // namespace efs
