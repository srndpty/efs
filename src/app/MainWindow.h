// Phase 1 の最小 UI: 検索欄 + 結果テーブル + ステータスバー (計画 7)。
//
// 種別フィルタ・Regex トグル・ヘッダソート・コンテキストメニュー・テーマは
// Phase 2 以降。ここでは先回りしない。
#pragma once

#include "core/ISearchBackend.h"
#include "core/SearchTypes.h"

#include <QMainWindow>

#include <memory>

class QLineEdit;
class QTableView;

namespace efs {

class ResultTableModel;
class SearchController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::unique_ptr<ISearchBackend> backend, QWidget* parent = nullptr);

private:
    void onResultsReady(const efs::SearchResults& results);
    void onCleared();

    QLineEdit* m_searchEdit = nullptr;
    QTableView* m_tableView = nullptr;
    ResultTableModel* m_model = nullptr;
    SearchController* m_controller = nullptr;
};

} // namespace efs
