// Regex の構文チェック (Phase 3)。**UI の best-effort advisory validation。**
//
// 背景: Everything 1.4 は不正な正規表現を error として返さず、単に 0 件を返す
// (P2 で実機確定。GetLastError() は EVERYTHING_OK)。したがって backend error
// とは別の問題として扱うしかなく、「0 件だから invalid」と判定してもならない。
//
// そこで QRegularExpression でローカルに構文だけ見る。Everything 側の regex
// エンジンと**完全同一だと仮定しない**。Phase 3 冒頭の互換性 probe で、
// 日常的な構文 (アンカー / 選択 / グループ / 文字クラス / \d / {n,m} /
// エスケープ / 空白 / \s / 先読み / 後読み / (?i) / Unicode) が Qt・Everything
// の双方で同じように通ることと、Qt が invalid と言うものが Everything でも
// 0 件になることを確認した (README の「Phase 3 の検証結果」)。
//
// 守る契約:
//   - **ユーザーの pattern を書き換えない。** invalid と判定しても検索は既存
//     経路でそのまま Everything へ渡す。この関数は backend の authority ではない。
//   - Regex OFF では何も検証しない (表示も解除する)。
//   - 空白だけの pattern は有効 (P2 の whitespace 契約。regex:" " = 名前に空白を含む)。
#pragma once

#include <QString>
#include <QtGlobal>

namespace efs {

struct RegexValidation {
    // false = 検証していない (Regex OFF、または pattern が空)。UI は表示を消す。
    bool checked = false;
    bool valid = true;
    // checked && !valid のときだけ意味がある。
    QString errorString;
    // QRegularExpression::patternErrorOffset() に合わせて qsizetype。
    qsizetype errorOffset = -1;
};

[[nodiscard]] RegexValidation validateRegex(const QString& pattern, bool regexEnabled);

} // namespace efs
