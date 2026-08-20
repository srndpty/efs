// Name 列にファイル種別アイコンを描く delegate (Phase 3)。
//
// アイコンをモデルの DecorationRole ではなく delegate 側へ置いたのは、
// ResultTableModel を QtGui 非依存のまま (行データだけを持つ) 保つため。
// モデルが提供するのは plain data の IconKeyRole (ただの QString) だけ。
//
// 差し替えるのは initStyleOption が中心で、描画は基本的に QStyledItemDelegate へ
// 任せる。paint をまるごと自前で書くと選択・交互行・省略記号の扱いを全部
// 再実装することになる。
// 併せて、全列のテキストを文字単位で省略しておく (Qt 既定は単語境界で折り返して
// から省略するため、空白を含む長いパスが "C:\Program ..." のように切れる)。
//
// 検索クエリと一致した部分の強調表示 (一致部分に地色を敷く) だけは
// initStyleOption では表現できないので paint() を差し替える。ただし差し替えるのは
// **テキストの描画だけ**で、背景・選択・交互行・アイコンは今までどおり style へ
// 投げる (CE_ItemViewItem をテキスト無しで 1 回呼ぶ)。
#pragma once

#include "core/MatchHighlight.h"

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace efs {

class IconCache;

class IconDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit IconDelegate(IconCache* cache, QObject* parent = nullptr);

    // 表示中の結果に対応するクエリを渡す。以後の paint で一致部分が強調される。
    // 検索を出した時点で呼ぶこと (行と照合器が食い違わないようにするため)。
    void setHighlighter(MatchHighlighter highlighter);

    // IconCache::imagesReady に対して cache を捨てる必要は無い。
    // placeholder は m_pixmaps に**入れていない**ので、次の paint で
    // pixmapFor() が改めて IconCache に問い合わせ、届いた本物へ差し替わる。
    // 受け手は viewport()->update() だけでよい。ここで全 clear すると、
    // 別のキーのアイコンが 1 つ届くたびに解決済みの QImage→QPixmap 変換を
    // すべてやり直すことになる。

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    [[nodiscard]] QPixmap pixmapFor(const QString& key) const;
    // option.text (= 省略済みの表示文字列) を、一致部分に地色を敷いて描く。
    void drawHighlightedText(QPainter* painter, const QStyleOptionViewItem& option,
                             const QList<MatchRange>& ranges) const;

    IconCache* m_cache = nullptr;
    // QImage → QPixmap の変換結果 (UI スレッド専用の 2 段目のキャッシュ)。
    mutable QHash<QString, QPixmap> m_pixmaps;
    // cache miss の間に描く汎用アイコン。起動時に 1 回だけ同期取得する
    // (行ごとではないので UI をブロックしない)。
    QPixmap m_genericFile;
    QPixmap m_genericDirectory;
    // 空なら従来どおり QStyledItemDelegate::paint に任せる。
    MatchHighlighter m_highlighter;
};

} // namespace efs
