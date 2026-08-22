// 検索クエリと一致した部分の位置を求める (結果一覧の強調表示)。
//
// 本家 Everything と同じく、結果の行でクエリに一致した部分を強調するための
// 下ごしらえ。**描画は一切しない** — 返すのは文字位置だけで、Qt Core 以外に
// 依存しない純粋なコードにしてある (単体テストの主戦場)。
//
// **意味は「backend が実際に照合した箇所」。** 単なる文字列の出現ではない。
// したがって照合対象がファイル名だけ (SearchQuery::matchPath == false) の間は、
// 一致するのは Name 列だけになる — Path 列に同じ文字列が見えていても、それは
// 検索条件が当たった箇所ではないので強調しない (どの列へ適用するかを決めるのは
// 呼び出し側 = IconDelegate)。
//
// これは表示のための best-effort な近似であり、Everything の検索エンジンの
// authority ではない。Everything の全構文 (関数構文、`<>` のグルーピング、
// 演算子の優先順位設定) を再実装はしない — 一致しない部分が強調されない
// ことはあっても、行そのものは正しく出る。**取りこぼし (false negative) は
// 許容し、誤った強調 (false positive) は避ける**方へ倒す。多段のフォールバックは
// 書かない (AGENTS.md「オーバーエンジニアリングを避ける」)。
#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace efs {

// 強調する 1 区間 (UTF-16 の文字位置)。
struct MatchRange {
    int start = 0;
    int length = 0;

    [[nodiscard]] friend bool operator==(const MatchRange&, const MatchRange&) = default;
};

// クエリ 1 本を「照合器」へ変換したもの。
//
// 検索のたびに 1 個作り、行ごと・列ごとに ranges() を呼ぶ。QRegularExpression の
// コンパイルは高くつくので、paint の中で毎回作り直さないための型でもある。
class MatchHighlighter {
public:
    MatchHighlighter() = default;

    // queryText はユーザー入力そのまま (EverythingQueryBuilder が受け取るのと
    // 同じ文字列)。regex / matchCase は SearchQuery の同名フィールド。
    //
    // Regex ON  — 入力全体を 1 本の正規表現として扱う。不正なパターンなら
    //             照合器は空になる (強調が付かないだけ)。
    // Regex OFF — `/` を `\` へ揃えてから (EverythingQueryBuilder と同じ変換)
    //             Everything の項区切り (空白 / `|`) で分解し、引用は外し、
    //             除外項 (`!foo`)・否定グループ (`!<foo bar>` は中身ごと)・
    //             関数構文 (`ext:jpg` 等) は落とす。
    //             残った項の `*` `?` はワイルドカードとして扱い、その場合だけ
    //             名前全体との一致を要求する (Everything と同じ)。ただし
    //             **引用の内側の `*` `?` はただの文字** (`"foo*bar"`)。
    MatchHighlighter(const QString& queryText, bool regex, bool matchCase);

    [[nodiscard]] bool isEmpty() const { return m_patterns.isEmpty(); }

    // text 内で一致した区間を、開始位置の昇順・重なりを併合した形で返す。
    // 空一致 (例: `a*`) は強調しようが無いので捨てる。
    [[nodiscard]] QList<MatchRange> ranges(const QString& text) const;

private:
    QList<QRegularExpression> m_patterns;
};

} // namespace efs
