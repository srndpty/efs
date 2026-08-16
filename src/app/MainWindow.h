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
class QTableView;
class QToolBar;
class QToolButton;

namespace efs {

class IconCache;
class IconDelegate;
class ResultTableModel;
class SearchController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // settings は値で受けてメンバへ move する (QByteArray 3 本を持つため)。
    explicit MainWindow(std::unique_ptr<ISearchBackend> backend, Settings settings,
                        QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildToolBar();
    void buildThemeMenu(QToolBar* toolBar);
    void buildTable();
    void buildRowActions();
    void buildSearchShortcuts();

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
    // backend error (優先) と regex warning を 1 行のメッセージへまとめる。
    // 検索のたびに modal を出さないための非モーダル表示。
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

    // 直近の検索が失敗した理由。正常に完了した検索で解除する。
    QString m_backendError;
    QString m_regexWarning;

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
