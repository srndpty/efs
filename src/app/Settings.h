// 設定の永続化 (計画 8 / F9)。Phase 3。
//
// QSettings を触ってよいのはこの 1 ファイルだけ。MainWindow の各所から
// 直接 QSettings を呼び散らさない。逆に、設定フレームワーク / repository
// pattern / migration framework も作らない — 平の struct と load/save の 2 関数
// だけで足りる。
//
// enum は int の生値ではなく安定した文字列 ("image" / "name" / "desc" …) で
// 書く。値の順序を入れ替えたときに INI が黙って別の意味になるのを防ぐため。
// 未知の値・壊れた値はすべて既定値へ戻す。
#pragma once

#include "app/Theme.h"
#include "core/SearchTypes.h"

#include <QByteArray>

namespace efs {

struct Settings {
    // 初回既定。Dark はこのアプリを作った理由の 1 つなので変えない。
    ThemeMode theme = ThemeMode::Dark;
    SearchOptions options; // All / Regex OFF / Name 昇順

    // Qt の saveGeometry / saveState / QHeaderView::saveState をそのまま持つ。
    // 自前でジオメトリを分解して画面外補正する機構は作らない
    // (restoreGeometry の挙動に任せる)。
    QByteArray windowGeometry;
    QByteArray windowState;
    QByteArray headerState;

    // 保存されていない / 壊れている項目は既定値のまま返る。
    [[nodiscard]] static Settings load();
    void save() const;
};

} // namespace efs
