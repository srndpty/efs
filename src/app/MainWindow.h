// Phase 2 の UI: 検索欄 + フィルタツールバー + 結果テーブル + ステータスバー
// (計画 7 / 10)。
//
// 設定の永続化 (ウィンドウ位置・列幅・最後のフィルタ・テーマ切替) と結果行の
// ファイルアイコンは Phase 3。ここでは先回りしない。
#pragma once

#include "core/ISearchBackend.h"
#include "core/SearchTypes.h"

#include <QMainWindow>

#include <memory>

class QAction;
class QLineEdit;
class QModelIndex;
class QPoint;
class QTableView;

namespace efs {

class ResultTableModel;
class SearchController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::unique_ptr<ISearchBackend> backend, QWidget* parent = nullptr);

private:
    void buildToolBar();
    void buildTable();
    void buildRowActions();

    void onResultsReady(const efs::SearchResults& results);
    void onCleared();
    void onHeaderClicked(int column);
    void onContextMenuRequested(const QPoint& pos);

    // 現在行のフルパス。行が無ければ空。
    [[nodiscard]] QString currentFullPath() const;
    void openCurrent();
    void revealCurrent();
    void copyCurrentFullPath();
    void copyCurrentName();

    QLineEdit* m_searchEdit = nullptr;
    QTableView* m_tableView = nullptr;
    ResultTableModel* m_model = nullptr;
    SearchController* m_controller = nullptr;

    QAction* m_regexAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_revealAction = nullptr;
    QAction* m_copyPathAction = nullptr;
    QAction* m_copyNameAction = nullptr;
};

} // namespace efs
