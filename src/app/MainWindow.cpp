#include "app/MainWindow.h"

#include "app/ResultTableModel.h"
#include "app/SearchController.h"
#include "core/Formatting.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

namespace efs {

MainWindow::MainWindow(std::unique_ptr<ISearchBackend> backend, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("efs"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search…"));
    m_searchEdit->setClearButtonEnabled(true);

    m_model = new ResultTableModel(this);

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(true);
    // ソートは backend 側で行う。打ち切られた 5,000 行内だけを並べ替えると
    // 全体の正しい上位 N 件と食い違うため、view には並べ替えさせない。
    m_tableView->setSortingEnabled(false);
    m_tableView->verticalHeader()->hide();
    // 行高固定。大量行でも view が全行を測らずに済む。
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    QHeaderView* header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ResultTableModel::ColumnPath, QHeaderView::Stretch);
    m_tableView->setColumnWidth(ResultTableModel::ColumnName, 280);
    m_tableView->setColumnWidth(ResultTableModel::ColumnSize, 90);
    m_tableView->setColumnWidth(ResultTableModel::ColumnDateModified, 150);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_tableView);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready"));

    m_controller =
        new SearchController(std::move(backend), SearchController::kDefaultDebounceMs, this);
    connect(m_searchEdit, &QLineEdit::textChanged, m_controller, &SearchController::setText);
    connect(m_searchEdit, &QLineEdit::returnPressed, m_controller, &SearchController::searchNow);
    connect(m_controller, &SearchController::resultsReady, this, &MainWindow::onResultsReady);
    connect(m_controller, &SearchController::cleared, this, &MainWindow::onCleared);

    m_searchEdit->setFocus();
}

void MainWindow::onResultsReady(const efs::SearchResults& results)
{
    m_model->setRows(results.rows);
    statusBar()->showMessage(formatStatus(results));
}

void MainWindow::onCleared()
{
    m_model->setRows({});
    statusBar()->showMessage(QStringLiteral("Ready"));
}

} // namespace efs
