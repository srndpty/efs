#include "app/MainWindow.h"

#include "app/FileActions.h"
#include "app/ResultTableModel.h"
#include "app/SearchController.h"
#include "app/Theme.h"
#include "app/ToolbarIcons.h"
#include "core/Formatting.h"

#include <QActionGroup>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>

namespace efs {

namespace {

struct KindEntry {
    FileKind kind;
    const char* label;
};

// ツールバーの並び。All を先頭に置き、起動時の既定にする。
constexpr std::array<KindEntry, 6> kKinds{{
    {.kind = FileKind::All, .label = "All"},
    {.kind = FileKind::Image, .label = "Image"},
    {.kind = FileKind::Video, .label = "Video"},
    {.kind = FileKind::Audio, .label = "Audio"},
    {.kind = FileKind::Document, .label = "Document"},
    {.kind = FileKind::Directory, .label = "Directory"},
}};

// 列 → ソートキー。対応しない列では sort を変更しない。
bool columnToSortKey(int column, SortKey& key)
{
    switch (column) {
    case ResultTableModel::ColumnName:
        key = SortKey::Name;
        return true;
    case ResultTableModel::ColumnPath:
        key = SortKey::Path;
        return true;
    case ResultTableModel::ColumnSize:
        key = SortKey::Size;
        return true;
    case ResultTableModel::ColumnDateModified:
        key = SortKey::DateModified;
        return true;
    default:
        return false;
    }
}

int sortKeyToColumn(SortKey key)
{
    switch (key) {
    case SortKey::Name:
        return ResultTableModel::ColumnName;
    case SortKey::Path:
        return ResultTableModel::ColumnPath;
    case SortKey::Size:
        return ResultTableModel::ColumnSize;
    case SortKey::DateModified:
        return ResultTableModel::ColumnDateModified;
    }
    return ResultTableModel::ColumnName;
}

} // namespace

MainWindow::MainWindow(std::unique_ptr<ISearchBackend> backend, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("efs"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search…"));
    m_searchEdit->setClearButtonEnabled(true);

    m_model = new ResultTableModel(this);

    // controller はツールバー構築より先に必要 (action が繋ぎ込む)。
    m_controller =
        new SearchController(std::move(backend), SearchController::kDefaultDebounceMs, this);

    buildTable();
    buildToolBar();
    buildRowActions();

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_tableView);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(m_searchEdit, &QLineEdit::textChanged, m_controller, &SearchController::setText);
    connect(m_searchEdit, &QLineEdit::returnPressed, m_controller, &SearchController::searchNow);
    connect(m_controller, &SearchController::resultsReady, this, &MainWindow::onResultsReady);
    connect(m_controller, &SearchController::cleared, this, &MainWindow::onCleared);

    m_searchEdit->setFocus();

    // ネイティブのタイトルバーは Qt の palette では暗くならない。
    applyDarkTitleBar(this);
}

void MainWindow::buildToolBar()
{
    auto* toolBar = addToolBar(QStringLiteral("Filters"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // 6 種別は排他。QActionGroup に任せ、自前で checked を管理しない。
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    int shortcutIndex = 0;
    for (const KindEntry& entry : kKinds) {
        auto* action = toolBar->addAction(kindIcon(entry.kind), QString::fromLatin1(entry.label));
        action->setCheckable(true);
        action->setShortcut(
            QKeySequence(Qt::ALT | static_cast<Qt::Key>(Qt::Key_1 + shortcutIndex)));
        action->setToolTip(
            QStringLiteral("%1 (%2)").arg(QString::fromLatin1(entry.label),
                                          action->shortcut().toString(QKeySequence::NativeText)));
        action->setChecked(entry.kind == m_controller->kind());
        group->addAction(action);

        const FileKind kind = entry.kind;
        connect(action, &QAction::triggered, this, [this, kind] { m_controller->setKind(kind); });
        ++shortcutIndex;
    }

    toolBar->addSeparator();

    // Regex は種別とは独立したトグル。
    m_regexAction = toolBar->addAction(regexIcon(), QStringLiteral("Regex"));
    m_regexAction->setCheckable(true);
    m_regexAction->setChecked(m_controller->regex());
    m_regexAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_regexAction->setToolTip(
        QStringLiteral("Regular expression (%1)")
            .arg(m_regexAction->shortcut().toString(QKeySequence::NativeText)));
    connect(m_regexAction, &QAction::toggled, this,
            [this](bool on) { m_controller->setRegex(on); });
    // ツールバーの action はウィンドウ全体のショートカットとして効かせる。
    addAction(m_regexAction);
}

void MainWindow::buildTable()
{
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    // ソートは backend 側で行う。打ち切られた 5,000 行内だけを並べ替えると
    // 全体の正しい上位 N 件と食い違うため、view には並べ替えさせない。
    // QSortFilterProxyModel も入れない。ヘッダのクリックは自前で受ける。
    m_tableView->setSortingEnabled(false);
    m_tableView->verticalHeader()->hide();
    // 行高固定。大量行でも view が全行を測らずに済む。
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    QHeaderView* header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ResultTableModel::ColumnPath, QHeaderView::Stretch);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setSortIndicator(sortKeyToColumn(m_controller->sortKey()),
                             m_controller->sortOrder() == SortOrder::Asc ? Qt::AscendingOrder
                                                                         : Qt::DescendingOrder);
    connect(header, &QHeaderView::sectionClicked, this, &MainWindow::onHeaderClicked);

    m_tableView->setColumnWidth(ResultTableModel::ColumnName, 280);
    m_tableView->setColumnWidth(ResultTableModel::ColumnSize, 90);
    m_tableView->setColumnWidth(ResultTableModel::ColumnDateModified, 150);

    connect(m_tableView, &QTableView::doubleClicked, this,
            [this](const QModelIndex&) { openCurrent(); });
    connect(m_tableView, &QTableView::customContextMenuRequested, this,
            &MainWindow::onContextMenuRequested);
}

void MainWindow::buildRowActions()
{
    // ショートカットはテーブルに focus があるときだけ効かせる。検索欄の Enter は
    // 既存どおり「即時検索」であり、ファイルを開く操作と競合させない。
    const auto addRowAction = [this](const QString& text, const QList<QKeySequence>& shortcuts,
                                     void (MainWindow::*slot)()) {
        auto* action = new QAction(text, this);
        action->setShortcuts(shortcuts);
        action->setShortcutContext(Qt::WidgetShortcut);
        connect(action, &QAction::triggered, this, slot);
        m_tableView->addAction(action);
        return action;
    };

    m_openAction = addRowAction(QStringLiteral("Open"),
                                {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)},
                                &MainWindow::openCurrent);
    m_revealAction = addRowAction(
        QStringLiteral("Show in Explorer"),
        {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)},
        &MainWindow::revealCurrent);
    m_copyPathAction =
        addRowAction(QStringLiteral("Copy Full Path"), {QKeySequence(Qt::CTRL | Qt::Key_C)},
                     &MainWindow::copyCurrentFullPath);
    m_copyNameAction = addRowAction(QStringLiteral("Copy Name"), {}, &MainWindow::copyCurrentName);
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

void MainWindow::onHeaderClicked(int column)
{
    SortKey key = SortKey::Name;
    if (!columnToSortKey(column, key))
        return;

    // 同じ列を再度クリックしたら昇順/降順を反転。別の列ならその列の昇順から。
    SortOrder order = SortOrder::Asc;
    if (key == m_controller->sortKey() && m_controller->sortOrder() == SortOrder::Asc)
        order = SortOrder::Desc;

    m_tableView->horizontalHeader()->setSortIndicator(
        column, order == SortOrder::Asc ? Qt::AscendingOrder : Qt::DescendingOrder);
    // 5,000 行の局所ソートではなく backend への再検索で全体を並べ替える。
    m_controller->setSort(key, order);
}

void MainWindow::onContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid())
        return;
    // 右クリックした行を current row として扱う。Phase 2 の action は
    // current row 1 件だけを対象にする (複数選択の一括処理は作らない)。
    m_tableView->setCurrentIndex(index);

    QMenu menu(this);
    menu.addAction(m_openAction);
    menu.addAction(m_revealAction);
    menu.addSeparator();
    menu.addAction(m_copyPathAction);
    menu.addAction(m_copyNameAction);
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

QString MainWindow::currentFullPath() const
{
    const QModelIndex index = m_tableView->currentIndex();
    if (!index.isValid())
        return {};
    return index.data(ResultTableModel::FullPathRole).toString();
}

void MainWindow::openCurrent()
{
    const QString path = currentFullPath();
    if (path.isEmpty())
        return;
    if (!openPath(path))
        statusBar()->showMessage(QStringLiteral("Failed to open: %1").arg(path));
}

void MainWindow::revealCurrent()
{
    const QString path = currentFullPath();
    if (path.isEmpty())
        return;
    if (!revealInExplorer(path))
        statusBar()->showMessage(QStringLiteral("Failed to show in Explorer: %1").arg(path));
}

void MainWindow::copyCurrentFullPath()
{
    const QString path = currentFullPath();
    if (path.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(path);
    statusBar()->showMessage(QStringLiteral("Copied: %1").arg(path));
}

void MainWindow::copyCurrentName()
{
    const QModelIndex index = m_tableView->currentIndex();
    if (!index.isValid())
        return;
    const QString name =
        index.siblingAtColumn(ResultTableModel::ColumnName).data(Qt::DisplayRole).toString();
    if (name.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(name);
    statusBar()->showMessage(QStringLiteral("Copied: %1").arg(name));
}

} // namespace efs
