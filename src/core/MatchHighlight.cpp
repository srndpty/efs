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

// 分解した 1 項。
//
// wildcard を項ごとの flag で持つのは、引用の内側の `*` `?` をただの文字として
// 扱うため。`"foo*bar"` は Everything ではワイルドカードではなくリテラルであり、
// 走査を終えた後に text だけを見ても両者は区別できない。
struct Term {
    QString text;
    bool wildcard = false;
};

// ユーザー入力を Everything の項へ分解する。
//
// 区切りは空白 (= AND) と `|` (= OR)。`"..."` の内側では空白を区切りにしない。
// グルーピングの `<` `>` は項ではないので区切りとして捨てる。
//
// 落とすもの (どれも「一致しない行」を選ぶための条件で、強調する対象が無い):
//   `!foo`      除外項
//   `!<foo bar>` 否定されたグループ。**中身ごと**捨てる — `!` と `foo` と `bar` に
//               割ると、除外条件のはずの語を positive な一致として強調してしまう。
//               対応する `>` が無いまま入力が終わったら、そこから先は解釈できない
//               ものとして丸ごと捨てる。Everything のグループ意味論を再実装は
//               しない (強調し損ねるのは許容、誤って強調するのは避ける)。
QList<Term> splitTerms(const QString& text)
{
    QList<Term> terms;
    Term current;
    bool inQuote = false;
    bool quoted = false;  // この項に引用が 1 文字でも含まれていたか
    bool negated = false; // この項が `!` で始まっていた
    int skipDepth = 0;    // 否定されたグループを読み飛ばしている深さ

    const auto flush = [&] {
        // 引用が混じる項ではワイルドカード化しない。`"a*"b*` のような混在を
        // 正確に扱うには項の内部構造が要るが、そこまでの parser は作らない。
        if (!current.text.isEmpty() && !negated) {
            current.wildcard = current.wildcard && !quoted;
            terms.append(current);
        }
        current = {};
        quoted = false;
        negated = false;
    };

    for (const QChar ch : text) {
        if (skipDepth > 0) {
            // 否定グループの内側。引用の中の `<` `>` は括弧として数えない。
            if (ch == u'"')
                inQuote = !inQuote;
            else if (!inQuote && ch == u'<')
                ++skipDepth;
            else if (!inQuote && ch == u'>')
                --skipDepth;
            continue;
        }
        if (ch == u'"') {
            inQuote = !inQuote;
            quoted = true;
            continue;
        }
        if (inQuote) {
            current.text.append(ch);
            continue;
        }
        if (ch.isSpace() || ch == u'|' || ch == u'<' || ch == u'>') {
            if (ch == u'<' && negated && current.text.isEmpty()) {
                skipDepth = 1;
                negated = false;
                continue;
            }
            flush();
            continue;
        }
        if (ch == u'!' && current.text.isEmpty() && !negated) {
            negated = true;
            continue;
        }
        if (ch == u'*' || ch == u'?')
            current.wildcard = true;
        current.text.append(ch);
    }
    flush();
    return terms;
}

// 1 項 → 正規表現。強調できない項 (関数構文) では無効な値を返す。
QRegularExpression patternForTerm(const Term& term, QRegularExpression::PatternOptions options)
{
    if (isFunctionTerm(term.text))
        return {};

    const QString escaped = QRegularExpression::escape(term.text);
    if (!term.wildcard)
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

    for (const Term& term : splitTerms(queryText)) {
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
                found.append({.start = static_cast<int>(match.capturedStart()), .length = length});
        }
    }
    if (found.isEmpty())
        return {};

    std::ranges::sort(found,
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
