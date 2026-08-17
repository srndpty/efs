#include "app/MainWindow.h"

#include "app/FileActions.h"
#include "app/IconCache.h"
#include "app/IconDelegate.h"
#include "app/RegexValidation.h"
#include "app/ResultTableModel.h"
#include "app/SearchController.h"
#include "app/ShellIcon.h"
#include "app/Theme.h"
#include "app/ToolbarIcons.h"
#include "core/Formatting.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

namespace efs {

namespace {

struct KindEntry {
    FileKind kind;
    const char* label;
};

// ツールバーの並び。All を先頭に置き、初回起動の既定にする。
constexpr std::array<KindEntry, 6> kKinds{{
    {.kind = FileKind::All, .label = "All"},
    {.kind = FileKind::Image, .label = "Image"},
    {.kind = FileKind::Video, .label = "Video"},
    {.kind = FileKind::Audio, .label = "Audio"},
    {.kind = FileKind::Document, .label = "Document"},
    {.kind = FileKind::Directory, .label = "Directory"},
}};

struct ThemeEntry {
    ThemeMode mode;
    const char* label;
};

constexpr std::array<ThemeEntry, 3> kThemes{{
    {.mode = ThemeMode::System, .label = "System"},
    {.mode = ThemeMode::Dark, .label = "Dark"},
    {.mode = ThemeMode::Light, .label = "Light"},
}};

// 警告色。Dark / Light どちらの地でも読めるので 1 色で通す。
constexpr auto kWarning = "#e05252";

// 結果テーブルのアイコン表示サイズ。shell の小アイコンと同じ 16px。
constexpr int kIconPixels = 16;

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

MainWindow::MainWindow(std::unique_ptr<ISearchBackend> backend, Settings settings, QWidget* parent)
    : QMainWindow(parent), m_settings(std::move(settings))
{
    setWindowTitle(QStringLiteral("efs"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search…"));
    m_searchEdit->setClearButtonEnabled(true);

    // backend error と Regex の構文警告を出す 1 行。空のときは畳む。
    // 検索 UI なので modal は出さない (検索のたびに popup を連打しないため)。
    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(kWarning)));
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_messageLabel->hide();

    m_model = new ResultTableModel(this);

    // controller はツールバー構築より先に必要 (action が状態を読む)。
    m_controller =
        new SearchController(std::move(backend), SearchController::kDefaultDebounceMs, this);

    // **接続とステータスの初期化は restoreOptions() より先に行う。**
    // searchStarted は dispatch の中で同期に発火するので、後から繋ぐと復元時の
    // 初回クエリだけ "Searching…" を取りこぼす。ここで繋ぐ相手 (m_model /
    // statusBar / m_messageLabel) はすべて生成済み。
    connect(m_controller, &SearchController::searchStarted, this, &MainWindow::onSearchStarted);
    connect(m_controller, &SearchController::resultsReady, this, &MainWindow::onResultsReady);
    connect(m_controller, &SearchController::cleared, this, &MainWindow::onCleared);

    statusBar()->showMessage(QStringLiteral("Ready"));

    // 永続化された種別 / Regex / ソートをここでまとめて戻す。個別の setter を
    // 順に呼ぶと復元だけで最大 3 本のクエリが飛ぶ。検索欄は起動時に空なので、
    // 発行されるのは「復元された kind が All 以外」のときの 1 本だけ
    // (All なら 0 本 = cleared)。
    m_controller->restoreOptions(m_settings.options);

    buildTable();
    buildToolBar();
    buildRowActions();
    buildSearchShortcuts();

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_tableView);
    setCentralWidget(central);

    connect(m_searchEdit, &QLineEdit::textChanged, m_controller, &SearchController::setText);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::updateRegexValidation);
    connect(m_searchEdit, &QLineEdit::returnPressed, m_controller, &SearchController::searchNow);

    m_searchEdit->setFocus();

    // 復元。壊れている / 古い / 空のいずれでも restoreGeometry は false を返す
    // だけなので、その場合は既定のサイズを使う。画面外補正を自前で計算する
    // 機構は作らない (Qt の挙動に任せる)。
    if (!restoreGeometry(m_settings.windowGeometry))
        resize(1000, 640);
    if (!m_settings.windowState.isEmpty())
        restoreState(m_settings.windowState);

    setTheme(m_settings.theme);
}

void MainWindow::buildToolBar()
{
    auto* toolBar = addToolBar(QStringLiteral("Filters"));
    // saveState / restoreState は objectName で対応付ける。無いと復元できない。
    toolBar->setObjectName(QStringLiteral("filterToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // 6 種別は排他。QActionGroup に任せ、自前で checked を管理しない。
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    const QColor ink = palette().color(QPalette::ButtonText);

    int shortcutIndex = 0;
    for (const KindEntry& entry : kKinds) {
        auto* action =
            toolBar->addAction(kindIcon(entry.kind, ink), QString::fromLatin1(entry.label));
        m_kindActions.at(static_cast<std::size_t>(shortcutIndex)) = action;
        action->setCheckable(true);
        action->setShortcut(
            QKeySequence(Qt::ALT | static_cast<Qt::Key>(Qt::Key_1 + shortcutIndex)));
        action->setToolTip(
            QStringLiteral("%1 (%2)").arg(QString::fromLatin1(entry.label),
                                          action->shortcut().toString(QKeySequence::NativeText)));
        // 復元済みの controller の状態に合わせる (controller が authority)。
        action->setChecked(entry.kind == m_controller->kind());
        group->addAction(action);

        const FileKind kind = entry.kind;
        connect(action, &QAction::triggered, this, [this, kind] { m_controller->setKind(kind); });
        ++shortcutIndex;
    }

    toolBar->addSeparator();

    // Regex は種別とは独立したトグル。
    m_regexAction = toolBar->addAction(regexIcon(ink), QStringLiteral("Regex"));
    m_regexAction->setCheckable(true);
    m_regexAction->setChecked(m_controller->regex());
    m_regexAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_regexAction->setToolTip(
        QStringLiteral("Regular expression (%1)")
            .arg(m_regexAction->shortcut().toString(QKeySequence::NativeText)));
    connect(m_regexAction, &QAction::toggled, this, [this](bool on) {
        m_controller->setRegex(on);
        // Regex OFF に戻したら構文の指摘は消す。
        updateRegexValidation();
    });
    // ツールバーの action はウィンドウ全体のショートカットとして効かせる。
    addAction(m_regexAction);

    buildThemeMenu(toolBar);
}

void MainWindow::buildThemeMenu(QToolBar* toolBar)
{
    // 設定ダイアログは作らない。テーマだけは触れる必要があるので、ツールバー
    // 右端の小さなドロップダウンで済ませる。
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    auto* menu = new QMenu(this);
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    for (const ThemeEntry& entry : kThemes) {
        auto* action = menu->addAction(QString::fromLatin1(entry.label));
        action->setCheckable(true);
        action->setChecked(entry.mode == m_settings.theme);
        group->addAction(action);

        const ThemeMode mode = entry.mode;
        connect(action, &QAction::triggered, this, [this, mode] { setTheme(mode); });
    }

    m_themeButton = new QToolButton(this);
    m_themeButton->setText(QStringLiteral("Theme"));
    m_themeButton->setIcon(themeIcon(palette().color(QPalette::ButtonText)));
    m_themeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_themeButton->setPopupMode(QToolButton::InstantPopup);
    m_themeButton->setMenu(menu);
    toolBar->addWidget(m_themeButton);
}

void MainWindow::refreshToolbarIcons()
{
    // 描線の色は palette から取る。テーマごとに色定数を持ち歩かない。
    const QColor ink = palette().color(QPalette::ButtonText);

    for (std::size_t i = 0; i < kKinds.size(); ++i) {
        if (m_kindActions.at(i))
            m_kindActions.at(i)->setIcon(kindIcon(kKinds.at(i).kind, ink));
    }
    if (m_regexAction)
        m_regexAction->setIcon(regexIcon(ink));
    if (m_themeButton)
        m_themeButton->setIcon(themeIcon(ink));
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
    m_tableView->setIconSize(QSize(kIconPixels, kIconPixels));

    // アイコン。lookup は種別 (ディレクトリ / 拡張子) 単位で専用スレッド 1 本に
    // 出し、結果は cache へ入る。**paint / data() から実ファイルを見に行かない。**
    m_iconCache = new IconCache(&shellIconImage, this);
    m_iconDelegate = new IconDelegate(m_iconCache, this);
    m_tableView->setItemDelegate(m_iconDelegate);
    // 完了通知は行番号を持たない。viewport を塗り直すだけなので、通知が届いた
    // 時点でモデルが reset 済みでも / 行が消えていても不整合が起きない。
    // 塗り直すだけでよい。placeholder は delegate 側にキャッシュしていないので、
    // 次の paint で本物へ差し替わる (解決済みのアイコンは再変換されない)。
    connect(m_iconCache, &IconCache::imagesReady, m_tableView->viewport(),
            qOverload<>(&QWidget::update));

    QHeaderView* header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ResultTableModel::ColumnPath, QHeaderView::Stretch);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);

    // Phase 2 の既定の列幅。復元に失敗したときはこれが残る。
    m_tableView->setColumnWidth(ResultTableModel::ColumnName, 280);
    m_tableView->setColumnWidth(ResultTableModel::ColumnSize, 90);
    m_tableView->setColumnWidth(ResultTableModel::ColumnDateModified, 150);
    if (!m_settings.headerState.isEmpty())
        header->restoreState(m_settings.headerState);

    // ソートインジケータは header state ではなく controller の値に合わせる
    // (検索状態の authority は controller)。
    header->setSortIndicator(sortKeyToColumn(m_controller->sortKey()),
                             m_controller->sortOrder() == SortOrder::Asc ? Qt::AscendingOrder
                                                                         : Qt::DescendingOrder);
    connect(header, &QHeaderView::sectionClicked, this, &MainWindow::onHeaderClicked);

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

void MainWindow::buildSearchShortcuts()
{
    // Ctrl+L / Ctrl+F — どこからでも検索欄へ戻る。全選択まで行うので、そのまま
    // 打ち直せる。
    auto* focusSearch = new QAction(this);
    focusSearch->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_L), QKeySequence(Qt::CTRL | Qt::Key_F)});
    connect(focusSearch, &QAction::triggered, this, [this] {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
    addAction(focusSearch);

    // Esc — 検索文字列が入っていれば消す。空なら何もしない。
    // **アプリを終了させない** (検索 UI で Esc = 終了は事故のもと)。
    auto* clearSearch = new QAction(this);
    clearSearch->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(clearSearch, &QAction::triggered, this, [this] {
        if (!m_searchEdit->text().isEmpty())
            m_searchEdit->clear();
    });
    addAction(clearSearch);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 保存は終了時の 1 回だけ。検索文字列・履歴・結果行は保存しない。
    m_settings.options = m_controller->options();
    m_settings.windowGeometry = saveGeometry();
    m_settings.windowState = saveState();
    m_settings.headerState = m_tableView->horizontalHeader()->saveState();
    m_settings.save();

    QMainWindow::closeEvent(event);
}

void MainWindow::setTheme(ThemeMode mode)
{
    m_settings.theme = mode;
    applyTheme(*qApp, mode);
    // ネイティブのタイトルバーは Qt の palette では変わらない。
    applyTitleBarTheme(this, resolveDark(mode));
    refreshToolbarIcons();
}

void MainWindow::onSearchStarted()
{
    // 実行中のクエリは中断できないので、**表示だけ**を現在の query に合わせる。
    // 古い行を残すと、検索欄はもう別の条件なのに前の結果が操作できてしまう
    // (Everything の regex 検索は 20 秒以上かかることがある。README の実測)。
    m_model->setRows({});
    // 前回の失敗は「今の検索」の状態ではないので下ろす。
    m_backendError.clear();
    statusBar()->showMessage(QStringLiteral("Searching…"));
    // Regex の構文警告は検索の進行とは無関係なので、あるなら維持する。
    updateMessage();
}

void MainWindow::onResultsReady(const efs::SearchResults& results)
{
    if (results.error.isEmpty()) {
        m_model->setRows(results.rows);
        // 正常に完了した検索でエラー表示を解除する。
        m_backendError.clear();
    } else {
        // 失敗したのに前回の行が残っていると「成功した結果」に見えてしまう。
        m_model->setRows({});
        m_backendError = results.error;
    }

    statusBar()->showMessage(formatStatus(results));
    updateMessage();
}

void MainWindow::onCleared()
{
    m_model->setRows({});
    m_backendError.clear();
    statusBar()->showMessage(QStringLiteral("Ready"));
    updateMessage();
}

void MainWindow::updateRegexValidation()
{
    // best-effort の advisory。Everything の regex エンジンの authority ではない。
    // 判定がどうであれ pattern は書き換えず、検索は既存経路でそのまま渡す。
    const RegexValidation validation = validateRegex(m_searchEdit->text(), m_controller->regex());
    const bool invalid = validation.checked && !validation.valid;

    m_searchEdit->setStyleSheet(
        invalid ? QStringLiteral("QLineEdit { border: 1px solid %1; }").arg(QLatin1String(kWarning))
                : QString());

    if (invalid) {
        m_regexWarning = QStringLiteral("Invalid regular expression at offset %1: %2")
                             .arg(validation.errorOffset)
                             .arg(validation.errorString);
        m_searchEdit->setToolTip(m_regexWarning);
    } else {
        m_regexWarning.clear();
        m_searchEdit->setToolTip(QString());
    }

    updateMessage();
}

void MainWindow::updateMessage()
{
    // backend error を優先する。両方出しても行が増えるだけで判断は変わらない。
    QString text;
    if (!m_backendError.isEmpty())
        text = QStringLiteral("Search failed: %1").arg(m_backendError);
    else
        text = m_regexWarning;

    m_messageLabel->setText(text);
    m_messageLabel->setVisible(!text.isEmpty());
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
    // 右クリックした行を current row として扱う。action は current row 1 件だけを
    // 対象にする (複数選択の一括処理は作らない)。
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
