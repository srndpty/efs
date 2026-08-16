// Name 列にファイル種別アイコンを描く delegate (Phase 3)。
//
// アイコンをモデルの DecorationRole ではなく delegate 側へ置いたのは、
// ResultTableModel を QtGui 非依存のまま (行データだけを持つ) 保つため。
// モデルが提供するのは plain data の IconKeyRole (ただの QString) だけ。
//
// initStyleOption だけを差し替え、描画そのものは QStyledItemDelegate に任せる。
// 自前 paint を書くと選択・交互行・省略記号の扱いを全部再実装することになる。
#pragma once

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace efs {

class IconCache;

class IconDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit IconDelegate(IconCache* cache, QObject* parent = nullptr);

    // IconCache::imagesReady を受けたら呼ぶ。placeholder のまま描かれた行を
    // 次の paint で本物に差し替えるため、変換済み QPixmap を捨てる。
    void invalidate();

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    [[nodiscard]] QPixmap pixmapFor(const QString& key) const;

    IconCache* m_cache = nullptr;
    // QImage → QPixmap の変換結果 (UI スレッド専用の 2 段目のキャッシュ)。
    mutable QHash<QString, QPixmap> m_pixmaps;
    // cache miss の間に描く汎用アイコン。起動時に 1 回だけ同期取得する
    // (行ごとではないので UI をブロックしない)。
    QPixmap m_genericFile;
    QPixmap m_genericDirectory;
};

} // namespace efs
