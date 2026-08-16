// 検索の要求と結果を表す backend 非依存の値型 (計画 5)。
//
// Qt Core のみに依存する。Widgets / Win32 / Everything SDK を持ち込まない。
// UI スレッドと検索スレッドの間を Qt::QueuedConnection で値渡しするため
// Q_DECLARE_METATYPE している。QVector は暗黙共有なのでコピーは安い。
#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace efs {

enum class FileKind { All, Image, Video, Audio, Document, Directory };
enum class SortKey { Name, Path, Size, DateModified };
enum class SortOrder { Asc, Desc };

// 1 回の検索で受け取る最大件数。超えた分は表示せず、総ヒット数だけを出す。
inline constexpr int kDefaultMaxResults = 5000;

struct SearchQuery {
    quint64 id = 0; // 単調増加。stale 判定に使う
    QString text;   // ユーザー入力そのまま (Everything の検索構文をそのまま許す)
    FileKind kind = FileKind::All;
    bool regex = false;
    bool matchCase = false;
    bool matchPath = false; // false = ファイル名のみ照合
    SortKey sortKey = SortKey::Name;
    SortOrder sortOrder = SortOrder::Asc;
    int maxResults = kDefaultMaxResults;
};

// 絞り込み条件が 1 つでもあるか。無ければ検索そのものを行わない。
//
// backend 非依存に判定する。SearchController から buildQueryString() を呼んで
// 空判定する設計にはしない (UI 側の状態機械が Everything 固有の文字列組み立てに
// 依存してしまうため)。判定規則は「テキストが実質空 かつ FileKind::All」だけ。
[[nodiscard]] inline bool hasSearchConstraint(const SearchQuery& query)
{
    return query.kind != FileKind::All || !query.text.trimmed().isEmpty();
}

struct ResultRow {
    QString name;
    QString path;       // 親ディレクトリのみ (フルパスは path + '\' + name)
    qint64 size = -1;   // ディレクトリ・不明は -1 → 表示は空欄
    QDateTime modified; // 無効な場合あり → 表示は "-"
    bool isDir = false;
};

struct SearchResults {
    quint64 id = 0;
    QVector<ResultRow> rows;
    quint64 totalMatches = 0; // 打ち切り前の全ヒット数
    bool truncated = false;   // totalMatches > rows.size()
    qint64 elapsedMs = 0;
    // 空でなければ失敗。ステータスバーへそのまま出すため英語 (AGENTS.md 言語ポリシー)。
    QString error;
};

} // namespace efs

Q_DECLARE_METATYPE(efs::SearchQuery)
Q_DECLARE_METATYPE(efs::SearchResults)
