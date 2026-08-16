// 結果行のファイル種別アイコンのキャッシュ (Phase 3)。
//
// **最重要の制約: 結果の paint / data() から実ファイルへ同期 I/O を行わない。**
// DecorationRole のたびに QFileInfo / QFileIconProvider を実パスへ呼ぶと、
// 5,000 行のスクロールで UI がディスク I/O に張り付く。したがって
//   - lookup の単位は「ファイル」ではなく「種別」(ディレクトリ / 拡張子)
//   - lookup は専用の worker thread 1 本で行う
//   - 結果は QImage (thread-safe な値) でコピーして返す
// という形にしてある。同じ .txt が 500 行あっても shell lookup は 1 回。
//
// Everything backend / SearchWorker はアイコンに一切関与しない (SearchWorker は
// Everything 専用のまま)。実際の shell lookup (Win32) は app 層の ShellIcon.cpp
// にあり、ここへは IconLoader として注入される — この分離のおかげで efs_core に
// Win32 が漏れず、テストはフェイクの loader で決定的に書ける。
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class QThread;

namespace efs {

// アイコンキーの中身。文字列表現 (iconKeyFor) と解釈 (parseIconKey) を
// 同じ場所に置く — 片方だけ変えると lookup が静かに壊れるため。
struct IconKey {
    bool isDirectory = false;
    QString extension; // 小文字。空 = 拡張子なし
};

// 行 → キー。ファイル固有ではなく**種別**のキーであることが要点。
//   ディレクトリ            → "dir:"
//   report.TXT / report.txt → "ext:txt"   (大小同一)
//   archive.tar.gz          → "ext:gz"    (最後のドットのみ)
//   README                  → "ext:"      (拡張子なし)
[[nodiscard]] QString iconKeyFor(bool isDirectory, const QString& name);
[[nodiscard]] IconKey parseIconKey(const QString& key);

// worker thread 上で呼ばれる。実ファイルには触らず、種別だけからアイコンを得ること。
using IconLoader = std::function<QImage(const IconKey& key)>;

class IconCache : public QObject {
    Q_OBJECT

public:
    explicit IconCache(IconLoader loader, QObject* parent = nullptr);
    ~IconCache() override;

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;
    IconCache(IconCache&&) = delete;
    IconCache& operator=(IconCache&&) = delete;

    // UI スレッドからのみ呼ぶこと (m_cache / m_pending に lock を持たせない前提)。
    //
    // hit: true を返し *out に画像を入れる (lookup は起きない)。
    // miss: false を返し、worker へ要求を出す。**同じキーの要求は重複させない。**
    //       失敗した lookup も null 画像として記録するので、無限に再試行しない。
    bool image(const QString& key, QImage* out);

signals:
    // 1 つ以上のアイコンが cache に入った。**行番号を持たない**のが要点 —
    // 受け手は viewport を再描画するだけでよく、完了通知が来た時点でモデルが
    // reset 済み / 行が消えていても不整合が起きない。
    void imagesReady();

    // worker への要求 (queued)。外から emit しない。
    void loadRequested(const QString& key);

private:
    void onLoaded(const QString& key, const QImage& image);

    QThread* m_thread = nullptr;
    QHash<QString, QImage> m_cache;
    QSet<QString> m_pending;
};

} // namespace efs
