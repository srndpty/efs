// UI: 検索欄 + メッセージ行 + フィルタツールバー + 結果テーブル + ステータスバー
// (計画 7 / 10)。
//
// Phase 3 で足したもの: 設定の復元と保存 / テーマ切替 / backend error の非モーダル
// 表示 / Regex の advisory validation / 結果行のアイコン / Ctrl+L・Ctrl+F・Esc。
#pragma once

#include "app/Settings.h"
#include "core/ISearchBackend.h"
#include "core/SearchTypes.h"

#include <QMainWindow>

#include <array>
#include <memory>

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPoint;
class QSystemTrayIcon;
class QTableView;
class QToolBar;
class QToolButton;

namespace efs {

class GlobalHotkey;
class IconCache;
class IconDelegate;
class ResultTableModel;
class SearchController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // settings は値で受けてメンバへ move する (QByteArray 3 本を持つため)。
    //
    // trayRequested は `--tray` (= ログオン時のスタートアップ) かどうか。
    // トレイが使えるなら「復元はするが初回クエリは最初に表示されるまで出さない」
    // に切り替える。誰も見ていない状態で数百万件の filter-only クエリを走らせ
    // ないため。main() は window を作らないと hasTrayIcon() を呼べないので、
    // 判定に必要な引数だけを ctor へ渡す形にしている。
    explicit MainWindow(std::unique_ptr<ISearchBackend> backend, Settings settings,
                        bool trayRequested = false, QWidget* parent = nullptr);

    // ホットキー / トレイのクリック / 2 個目の起動 から呼ばれる共通の入口。
    // 隠れていれば出し、最小化されていれば戻し、前面へ持ってきて検索欄を選択する。
    // (接続は関数ポインタ構文なので slot にする必要は無い。)
    void showAndActivate();

    // トレイに常駐できているか。false なら閉じる = 終了であり、`--tray` で
    // 起動しても隠れたままにしてはいけない (呼び出す手段が無くなる)。
    [[nodiscard]] bool hasTrayIcon() const { return m_tray != nullptr; }

    // **意図的な終了の唯一の経路。** トレイの Quit と `efs.exe --quit` の
    // IPC が両方ここへ入る。設定を保存してからイベントループを抜ける。
    void quitApplication();

protected:
    // **閉じるボタンでは終了しない** (常駐アプリ)。設定を保存して隠すだけ。
    // 終了するのは quitApplication() だけ (トレイの Quit と `--quit` IPC)。
    void closeEvent(QCloseEvent* event) override;

private:
    void buildToolBar();
    void buildThemeMenu(QToolBar* toolBar);
    void buildTable();
    void buildRowActions();
    void buildSearchShortcuts();
    void buildTrayIcon();
    // 設定をメンバへ集めて INI へ書く。隠すときと終了するときの両方で呼ぶ
    // (閉じる = 終了ではなくなったので、closeEvent だけでは足りない)。
    void saveSettings();
    // 起動時とホットキー変更時に呼ぶ。失敗しても起動は止めず、理由を出すだけ。
    void applyHotkey();

    // クエリを発行した時点で表示を現在の query に合わせる (古い結果を残さない)。
    void onSearchStarted();
    void onResultsReady(const efs::SearchResults& results);
    void onCleared();
    void onHeaderClicked(int column);
    void onContextMenuRequested(const QPoint& pos);

    void setTheme(ThemeMode mode);
    // ツールバーのアイコンは QPainter で描いた固定色のピクスマップなので、
    // テーマを変えたら描き直す必要がある (Light の地に薄い線を描くと消える)。
    void refreshToolbarIcons();
    // Regex の構文を best-effort で見て検索欄の見た目とメッセージを更新する。
    // **ユーザーの pattern には触れない。検索そのものは既存経路のまま。**
    void updateRegexValidation();
    // 保存の失敗 (最優先) / backend error / regex warning / ホットキーの警告を
    // 1 行のメッセージへまとめる。検索のたびに modal を出さないための非モーダル表示。
    void updateMessage();

    // 現在行のフルパス。行が無ければ空。
    [[nodiscard]] QString currentFullPath() const;
    void openCurrent();
    void revealCurrent();
    void copyCurrentFullPath();
    void copyCurrentName();

    // 起動時に読み込んだ値。終了時にウィンドウ状態と現在のオプションを載せて保存する。
    Settings m_settings;

    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_messageLabel = nullptr;
    QTableView* m_tableView = nullptr;
    ResultTableModel* m_model = nullptr;
    SearchController* m_controller = nullptr;
    IconCache* m_iconCache = nullptr;
    IconDelegate* m_iconDelegate = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    GlobalHotkey* m_hotkey = nullptr;

    // 直近の検索が失敗した理由。正常に完了した検索で解除する。
    QString m_backendError;
    QString m_regexWarning;
    // ホットキーを登録できなかった理由 (他アプリと衝突している等)。
    QString m_hotkeyWarning;
    // 設定を INI へ書けなかった理由。**黙って捨てない** — 閉じる = 隠す なので、
    // 保存できていないことに気づく機会がこの 1 行しかない。
    QString m_saveError;

    // `--tray` で隠れて起動したため初回クエリを保留している。最初に
    // showAndActivate() が呼ばれた時点で 1 回だけ発行する。
    bool m_initialSearchPending = false;

    // テーマ変更時にアイコンを描き直すために保持する。
    std::array<QAction*, 6> m_kindActions{};
    QToolButton* m_themeButton = nullptr;

    QAction* m_regexAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_revealAction = nullptr;
    QAction* m_copyPathAction = nullptr;
    QAction* m_copyNameAction = nullptr;
};

} // namespace efs
