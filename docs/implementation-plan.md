# efs — Everything 代替 高速ファイル検索 UI (Windows / C++20 / Qt 6)

## Context

Everything File Search を日常利用しているが、(1) 標準のダークモードがない、(2) ファイル種別フィルタをメニューから選ぶ操作が煩雑、(3) Regex の ON/OFF 切り替えが遅い、という3点が不満。
そこで **検索エンジンは Everything をそのまま使い、UI だけを自分好みに置き換える** デスクトップアプリ `efs` を新規開発する。

初版では検索エンジンを再実装しない。ただし将来 MFT/USN Journal ベースの自前 backend へ差し替えられるよう、UI と Everything SDK の間に `ISearchBackend` を挟む。

**最優先は「日常利用できる MVP を早く完成させること」。** アーキテクチャの抽象化は "将来 backend を1個足せる" 一点に絞り、それ以外の一般化は行わない。

### 確認済みの環境 (2026-08-16 時点)

| 項目 | 状態 |
|---|---|
| 作業ディレクトリ | `c:\dev\soft\efs` — 空、git 未初期化 |
| Everything | **1.4.1.1022** インストール済み・起動中 (`C:\Program Files\Everything`) |
| Visual Studio | 2022 あり |
| CMake | あり (`Python310\Scripts\cmake.exe`) |
| Qt | **未導入** → aqtinstall で取得する |
| vcpkg / ninja(PATH上) | なし (ninja は VS 同梱のものを使用) |

### 確定済みの意思決定 (ユーザー回答)

- Qt 導入: **aqtinstall**
- 大量ヒット時: **上限打ち切り + 総件数表示** (仮想スクロールはやらない)
- ソート: **バックエンド側 (`Everything_SetSort`) で実行**、`SearchQuery` にソート指定を含める
- MVP スコープ追加: **結果の右クリックメニュー**のみ (グローバルホットキー/トレイ常駐・検索履歴・D&D は MVP 外)

---

## 1. Requirements

### 機能要件 (MVP)

| ID | 内容 |
|---|---|
| F1 | 検索ボックスへの入力に応じてインクリメンタル検索 (type-as-you-search)。デバウンス約120ms。 |
| F2 | Regex トグルボタン + ショートカット `Ctrl+R`。状態は視覚的に明示。 |
| F3 | ツールバーで種別フィルタを1クリック切替: All / Image / Video / Audio / Document / Directory (排他)。`Alt+1`〜`Alt+6`。 |
| F4 | 結果テーブル: Name / Path / Size / Date Modified。 |
| F5 | 列ヘッダクリックでソート (backend 側で再検索)。 |
| F6 | ステータスバー: `12,345 件 (上限 5,000 件表示) / 18 ms` および エラー状態。 |
| F7 | ダークテーマを **既定** とし、Light/Dark/System を設定で切替。 |
| F8 | 右クリックメニュー: 開く / フォルダを開いて選択 / フルパスをコピー / 名前をコピー。ダブルクリック・Enter = 開く。 |
| F9 | ウィンドウ位置・サイズ・列幅・最後のフィルタ・Regex 状態・テーマを次回起動時に復元。 |

### 非機能要件

| ID | 内容 |
|---|---|
| N1 | **UI スレッドを一切ブロックしない。** 検索 IPC はすべてワーカースレッド。 |
| N2 | 入力から結果表示まで体感即時 (Everything の応答が数ms〜数十msなのでデバウンス+描画が支配的)。 |
| N3 | 古い検索結果が新しい結果を上書きしない (stale 破棄)。 |
| N4 | Everything が未起動/未インストールでもクラッシュせず、ステータスバーで理由を提示。 |
| N5 | x64 のみ。Windows 10 / 11。 |
| N6 | UI コードが `Everything.h` を include しない (backend 差し替え可能性の実質的な担保)。 |

### MVP 非スコープ (明示的にやらない)

自前インデックス / 全文(内容)検索 / プレビューペイン / 複数タブ / 検索履歴 / D&D / グローバルホットキー / トレイ常駐 / インストーラ / プラグイン機構 / 多言語化 / Everything 1.5 alpha 対応。

---

## 2. Architecture

```
┌─────────────────────── UI thread ───────────────────────┐
│  MainWindow                                              │
│   ├─ QLineEdit (search)  ├─ QToolBar (kind, regex)       │
│   ├─ QTableView ── ResultTableModel (QAbstractTableModel)│
│   └─ QStatusBar                                          │
│                     ↓ requestSearch()                    │
│  SearchController   ── debounce QTimer, request id 採番   │
└──────────────┬───────────────────────────▲───────────────┘
     queued    │ runSearch(SearchQuery)    │ resultsReady(SearchResults)  queued
┌──────────────▼───────────────────────────┴───────────────┐
│                  QThread "search"                        │
│  SearchWorker ── owns std::unique_ptr<ISearchBackend>    │
└──────────────────────────┬───────────────────────────────┘
                           │ 同期呼び出し
              ┌────────────▼─────────────┐
              │  ISearchBackend (抽象)   │
              ├──────────────┬───────────┤
              │EverythingBack│NativeNtfs │ ← 将来
              │end           │Backend    │
              └──────┬───────┴───────────┘
                     │ 動的ロード (LoadLibrary)
              Everything64.dll ──IPC──▶ Everything.exe
```

### 設計の要点

**(A) `ISearchBackend` は同期 API にする。** 非同期化・キャンセル・デバウンスは `SearchController`/`SearchWorker` の1箇所だけが持つ。backend 実装者は「クエリを受けて結果を返す関数」を書くだけでよく、将来 `NativeNtfsBackend` を足すコストが最小になる。逆に backend ごとにコールバック/シグナルを持たせると、実装が2箇所に散り overengineering になる。

**(B) Everything への依存は `src/backend/everything/` の3ファイルに封じ込める。** `Everything.h` を include するのは `EverythingApi.cpp` のみ。DLL は `LoadLibraryW` + `GetProcAddress` で **動的ロード**する（暗黙リンクにすると DLL 欠如でプロセスが起動すらできない。動的なら「Everything SDK が見つかりません」と UI に出せる）。

**(C) 種別フィルタは backend 非依存の構造体として表現する。** `FileKind` enum + 拡張子リストを core 側に持ち、Everything 用クエリ文字列への変換は `EverythingQueryBuilder` が担当する。Everything 組み込みマクロ (`audio:` 等) は使わない — あれはユーザーの `Filters.csv` 依存で、将来の native backend に移植できないため。

**(D) DI フレームワーク・イベントバス・プラグインローダは導入しない。** backend の選択は `createBackend(BackendKind)` という関数1つ。

---

## 3. Module / File structure

```
c:\dev\soft\efs\
├─ CMakeLists.txt                  # top-level, C++20, Qt6::Widgets
├─ CMakePresets.json               # msvc2022-x64-debug / -release
├─ README.md
├─ .gitignore
├─ third_party\everything-sdk\
│   ├─ Everything.h                # voidtools SDK 同梱ヘッダ (参照用)
│   └─ dll\Everything64.dll        # 実行時に exe と同階層へコピー
├─ src\
│  ├─ core\                        # Qt Core のみ。Widgets/Win32/Everything 非依存
│  │   ├─ SearchTypes.h            # FileKind, SortKey, SortOrder, SearchQuery, ResultRow, SearchResults
│  │   ├─ ISearchBackend.h
│  │   ├─ FileKinds.h / .cpp       # FileKind → 拡張子リスト、表示名
│  │   └─ Formatting.h / .cpp      # サイズ/日時の表示整形 (単体テスト対象)
│  ├─ backend\everything\
│  │   ├─ EverythingApi.h / .cpp   # DLL 動的ロード + 関数ポインタ束 (唯一の Everything.h 利用箇所)
│  │   ├─ EverythingQueryBuilder.h / .cpp   # SearchQuery → クエリ文字列 (純粋関数・最重要テスト対象)
│  │   └─ EverythingBackend.h / .cpp        # ISearchBackend 実装
│  ├─ backend\BackendFactory.h / .cpp       # enum → unique_ptr<ISearchBackend>
│  └─ app\
│      ├─ main.cpp
│      ├─ MainWindow.h / .cpp      # .ui は使わずコードで構築 (差分が読める / ビルド単純)
│      ├─ SearchController.h / .cpp
│      ├─ SearchWorker.h / .cpp
│      ├─ ResultTableModel.h / .cpp
│      ├─ Theme.h / .cpp           # QPalette ベースの Dark/Light
│      └─ Settings.h / .cpp        # QSettings ラッパ (plain struct + load/save)
└─ tests\
   ├─ CMakeLists.txt
   ├─ test_query_builder.cpp
   ├─ test_formatting.cpp
   ├─ test_result_model.cpp
   └─ test_everything_backend.cpp  # Everything 起動時のみ実行 (未起動なら QSKIP)
```

CMake ターゲット構成 (最小):
- `efs_core` (STATIC) ← core + backend
- `efs` (WIN32 EXECUTABLE) ← app、`efs_core` にリンク
- `efs_tests` ← `efs_core` にリンク、`ctest` 登録

これ以上ターゲットを割らない (core と backend を分けても現時点で得がない)。

---

## 4. Threading model

### スレッド構成 — 2本のみ

| スレッド | 役割 |
|---|---|
| **UI (main)** | Qt Widgets、`ResultTableModel`、`SearchController`、デバウンス `QTimer` |
| **search (QThread 1本)** | `SearchWorker` + backend インスタンスを所有。**Everything SDK の呼び出しはすべてこのスレッドのみ**。 |

Everything SDK は検索パラメータをグローバル/TLS 状態として保持する API 形状 (`SetSearch` → `SetRegex` → `Query` → `GetResult*`) のため、**呼び出しを1スレッドに直列化するのが唯一安全かつ最も単純**。スレッドプール化は禁止 (状態が壊れる)。

### 型の受け渡し

`SearchQuery` / `SearchResults` は `Q_DECLARE_METATYPE` + `qRegisterMetaType` し、`Qt::QueuedConnection` の引数として値渡し。`SearchResults` は `QVector<ResultRow>` を持つため、`emit` 時のコピーは暗黙共有で安価。UI 側では `std::move` でモデルへ移す。

### デバウンスと stale 破棄 (N1/N3)

1. `QLineEdit::textChanged` / フィルタ変更 / Regex 変更 / ソート変更 → `SearchController::scheduleSearch()`。
2. 単発 `QTimer` を 120ms で再起動 (Enter キーは即時発火、デバウンスをスキップ)。
3. 発火時に `id = ++m_nextId` を採番、`m_latestRequestId.store(id)` (`std::atomic<quint64>`)、`emit runSearch(query)`。
4. **worker 側**: `onRunSearch` の冒頭で `if (q.id != m_latestRequestId.load()) return;` — キューに溜まった古いリクエストを実行前に捨てる。
5. `backend->search(q)` (ブロッキング) → `emit resultsReady(results)`。
6. **UI 側**: `results.id != m_latestRequestId` なら破棄。

Everything の IPC クエリは中断できないため「実行中のクエリをキャンセルする」機構は作らない。1クエリは通常数ms〜数十msで完了し、結果を捨てるだけで十分。この判断により `CancelToken` 等の追加抽象が不要になる。

### 終了処理

`MainWindow` の close 時に `thread->quit(); thread->wait();`。`SearchWorker` は `deleteLater` でスレッド上で破棄し、その中で `Everything_CleanUp()` と `FreeLibrary` を呼ぶ。

---

## 5. Search request / result data model

`src/core/SearchTypes.h` — backend 非依存。

```cpp
enum class FileKind { All, Image, Video, Audio, Document, Directory };
enum class SortKey  { Name, Path, Size, DateModified };
enum class SortOrder{ Asc, Desc };

struct SearchQuery {
    quint64   id        = 0;          // 採番。stale 判定に使用
    QString   text;                   // ユーザー入力そのまま
    FileKind  kind      = FileKind::All;
    bool      regex     = false;
    bool      matchCase = false;
    bool      matchPath = false;      // 既定 false = ファイル名のみ照合
    SortKey   sortKey   = SortKey::Name;
    SortOrder sortOrder = SortOrder::Asc;
    int       maxResults = 5000;      // 設定で変更可
};

struct ResultRow {
    QString   name;
    QString   path;                   // 親ディレクトリのみ (フルパスは path + '\' + name)
    qint64    size = -1;              // ディレクトリは -1 → 表示は空欄
    QDateTime modified;               // 無効な場合あり → 表示は "-"
    bool      isDir = false;
};

struct SearchResults {
    quint64            id = 0;
    QVector<ResultRow> rows;
    quint64            totalMatches = 0;   // 打ち切り前の全ヒット数
    bool               truncated = false;  // totalMatches > rows.size()
    qint64             elapsedMs = 0;
    QString            error;              // 空でなければ失敗。UI 表示用の日本語メッセージ
};
```

補足:
- `size`/`modified` は `Everything_SetRequestFlags` で一括取得するので追加 I/O は発生しない (`QFileInfo` を各行に対して呼ぶのは厳禁 — ディスクアクセスで固まる)。
- `error` は例外ではなく値で返す。backend は例外を投げない契約。

---

## 6. Everything SDK integration boundary

### 6.1 `EverythingApi` — DLL 動的ロード層

- `Everything64.dll` を `LoadLibraryW` で読み込む。探索順: (1) exe と同階層、(2) `PATH`。
- 使用する関数のみ `GetProcAddress` で解決し構造体に保持:
  `Everything_SetSearchW / SetRegex / SetMatchCase / SetMatchWholeWord / SetMatchPath / SetSort / SetRequestFlags / SetMax / SetOffset / QueryW / GetNumResults / GetTotResults / GetResultFileNameW / GetResultPathW / GetResultSize / GetResultDateModified / IsFolderResult / GetLastError / CleanUp`
- 1つでも解決できなければ `available=false` + 理由文字列。

### 6.2 `EverythingQueryBuilder` — 純粋関数 (テストの主戦場)

`SearchQuery` → Everything 検索文字列に変換する。**Qt 以外に依存しない自由関数**にし、単体テストしやすくする。

```
buildQueryString(q) =
    [kindPrefix(q.kind)] + [" "] + [regex ? "regex:" + q.text : q.text]
```

| FileKind | 前置詞 |
|---|---|
| All | (なし) |
| Image | `ext:jpg;jpeg;png;gif;bmp;webp;tif;tiff;heic;svg;ico;cr2;nef` |
| Video | `ext:mp4;mkv;avi;mov;wmv;flv;webm;m4v;mpg;mpeg;ts` |
| Audio | `ext:mp3;flac;wav;aac;m4a;ogg;opus;wma` |
| Document | `ext:pdf;doc;docx;xls;xlsx;ppt;pptx;txt;md;rtf;odt;ods;csv;epub` |
| Directory | `folder:` |

**Regex の扱い（Phase 0 で実機確定・再検討不要）**: Everything 1.4.1.1022 に対する実測で以下が確定した。

- **インライン修飾子 `regex:` は `ext:` と併用できる。** 2 つの項は AND 結合される (`ext:jpg regex:^a00` は全行が `.jpg` かつ `^a00` に一致。陰性対照は 0 件)。
- **グローバルフラグ `Everything_SetRegex(TRUE)` は使えない。** 検索文字列**全体**を正規表現として解釈するため `ext:` 前置詞がパターンに飲み込まれ、`ext:jpg ^IMG_\d+` は 0 件になる。

**確定方針: `Everything_SetRegex` は常に FALSE のままにし、正規表現は `regex:` 項としてクエリ文字列へ埋め込む** (`ext:jpg;png regex:^IMG_\d+`)。

代替案として書かれていた「Regex ON のときは種別フィルタを拡張子の正規表現 `.*\.(jpg|png)$` へ畳み込むフォールバック」は **不要であり、実装してはならない**。この結論は `tests/test_query_builder.cpp` の `p0RegressionExtPlusRegex` と `tests/test_everything_backend.cpp` の `extAndRegexAreCombinedWithAnd` (陽性 + 陰性対照) で回帰テストとして固定してある。

**Regex 項の引用（Phase 2 冒頭で実機確定・再検討不要）**: Phase 1 から持ち越していた「空白を含むパターンが 2 項に割れるのではないか」という懸念は、Everything 1.4.1.1022 に対する実測で**そのとおりだった**ことが確認された。

- 引用しない `regex:^efsspike alpha` は `regex:^efsspike` と `alpha` の 2 項に割れる (正解 3 件に対して 4 件)。**空白だけでなく TAB も項の区切りとして解釈される。**
- 引用した `regex:"^efsspike alpha"` は 1 項として渡り、`Everything_SetRegex(TRUE)` で得た正解集合と完全に一致した。`ext:` / `folder:` との AND も保たれる。
- `[ ]` や `\ ` で空白を表す案は、`[` と `]`・`\` と ` ` の間で先に項が割れるため 0 件になり**使えない**。`\s` / `\x20` は動くが、ユーザーに打たせる記法を強制するので採らない。
- 空白を含まないパターンを引用しても結果は変わらないため、**条件分岐せず無条件に囲む**。

**確定方針: Regex ON のテキスト項は `regex:"<入力>"` とする。** パターン内部のエスケープは補わない (パターン自体が `"` を含む場合は壊れるが、NTFS のファイル名に `"` は入らないので実用上意味が無い)。詳細な観測値は README の「Phase 2 の検証結果」に記録した。回帰テストは `p2RegressionRegexWithSpaceIsQuoted` (単体) と `regexWithSpaceIsOneTerm` (実機・陽性 + 陰性対照)。

**空白の扱いは Regex の ON/OFF で異なる (P2 review で確定)**: 引用の内側では前後の空白も TAB も意味を持つため、**Regex ON ではユーザーのパターンを一字も変えない**。Regex OFF の前後空白は Everything の項区切りでしかないので trim する。「テキスト条件なし」の判定も同じ契約に揃える — Regex ON では空文字だけが条件なしで、**空白だけのパターン (`" "` = 名前に空白を含む) は有効な検索条件**。`buildQueryString()` と `hasSearchConstraint()` がずれると「条件はあるのにクエリが空」になるため、両者を同じ入力で突き合わせるテスト (`regexWhitespaceContractMatchesHasSearchConstraint`) で固定してある。

**種別フィルタは hard constraint とする (P2 review で確定)**: Everything は演算子の優先順位を設定で変更できるため、`ext:... a|b` のままだと種別項が OR の片側からしか掛からない可能性がある。実測では**現在の既定設定で `|` は空白 (AND) より強く結合しており破綻していない**が、設定に依存させないため、種別項があり Regex OFF のときはユーザー式全体を Everything のグルーピング `<...>` で囲む。`( )` はグルーピングではない (括弧を含む名前を探しに行く) ことも実測で確認した。`<>` を被せても結果が変わらないことを AND / OR / 否定 / 引用 / ワイルドカード / inline regex / 入れ子 `<>` / 不均衡な `<` `>` / 他の修飾子の 13 ケースで確認済み (README)。パーサやフォールバックは作らない。

**不正な正規表現は検出できない。** Everything は構文エラーを返さず、単に 0 件を返す (`GetLastError()` は `EVERYTHING_OK`)。したがって「Regex が壊れている」ことを backend から知る手段は無く、`SearchResults::error` にも載らない。**Phase 3 で確定**: 検索欄を赤くする error UX は UI 側の判断として `QRegularExpression` で行う (`app/RegexValidation.*`)。これは advisory であり backend の authority ではない。**「0 件だから invalid」と判定してはならない** — 陰性対照で 0 件を返す valid なパターンを確認済み。

### 6.3 `EverythingBackend::search()` — 1クエリの手順

```
1. api.SetSearchW(buildQueryString(q))
2. api.SetRegex(FALSE)                     // 6.2 の方針
3. api.SetMatchCase(q.matchCase) / SetMatchPath(q.matchPath) / SetMatchWholeWord(FALSE)
4. api.SetSort(toEverythingSort(q.sortKey, q.sortOrder))
5. api.SetRequestFlags(FILE_NAME | PATH | SIZE | DATE_MODIFIED | ATTRIBUTES)
6. api.SetOffset(0); api.SetMax(q.maxResults)
7. api.QueryW(TRUE)                        // ブロッキング
8. 失敗 → GetLastError() を日本語メッセージへ写像し error にセットして返す
     IPC        → 「Everything が起動していません」
     MEMORY     → 「メモリ不足」
     その他     → 「検索に失敗しました (code=N)」
9. n = GetNumResults(); rows.reserve(n)
   for i<n: GetResultFileNameW / GetResultPathW / GetResultSize / GetResultDateModified / IsFolderResult
   FILETIME → QDateTime 変換はヘルパ関数に切り出す
10. totalMatches = GetTotResults(); truncated = totalMatches > n
```

注意点:
- 各 `GetResult*` は次の `Query` まで有効なポインタを返すため、**その場で `QString::fromWCharArray` でコピー**する。
- サイズ/日時ソートは Everything 側の "fast sort" 設定が無いと低速になる場合がある。実測で遅ければステータスバーに ms を出しているので気づける。
- 例外は投げない。DLL 未ロード時は `isAvailable()` が false を返し、`search()` は error 付き空結果を返す。

### 6.4 `ISearchBackend`

```cpp
class ISearchBackend {
public:
    virtual ~ISearchBackend() = default;
    virtual QString name() const = 0;                          // "Everything"
    virtual bool    isAvailable(QString* reason) const = 0;
    virtual SearchResults search(const SearchQuery& q) = 0;     // 同期。search スレッドからのみ呼ばれる
};
```

これ以上のメンバ（capability フラグ、非同期版、進捗通知など）は将来 `NativeNtfsBackend` を書く段になって必要になったら足す。今は入れない。

---

## 7. Qt model / view design

### `ResultTableModel : QAbstractTableModel`

- 内部は `QVector<ResultRow>` 1本。`setResults(SearchResults&&)` で `beginResetModel`/`endResetModel`。差分更新 (`dataChanged`) は実装しない — 5,000行の全リセットは1ms未満で、Everything の結果は毎回まったく別物になるため差分に意味がない。
- 4列: Name / Path / Size / Date Modified。
- `data()`:
  - `DisplayRole` — Size は `QLocale::formattedDataSize` (ディレクトリは空文字)、Date は `QLocale::toString(dt, QLocale::ShortFormat)`、無効日時は `"-"`。
  - `TextAlignmentRole` — Size は右寄せ。
  - `ToolTipRole` — フルパス。
  - `DecorationRole` — Name 列のみ。**拡張子をキーにした `QHash<QString, QIcon>` キャッシュ**で `QFileIconProvider` を使う。個々のファイルパスに対して呼ぶとディスク I/O で固まるので厳禁。ディレクトリは共通アイコン。(Phase 3)
  - 独自 `FullPathRole` — コンテキストメニュー/オープン処理用。
- `headerData()` で列名。

### `QTableView` 設定

```
setSelectionBehavior(SelectRows); setSelectionMode(ExtendedSelection);
setShowGrid(false); setAlternatingRowColors(true);
setEditTriggers(NoEditTriggers);
setContextMenuPolicy(CustomContextMenu);
verticalHeader()->hide();
verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);   // 行高固定 = 大量行でも高速
horizontalHeader()->setSectionsClickable(true);
setSortingEnabled(false);   // ★重要
```

**`setSortingEnabled(false)` は意図的**。ソートは backend 側で行うため、`QTableView` に勝手に並べ替えさせてはならない (打ち切られた 5,000 行内だけの並べ替えになり、全体の正しい上位N件と食い違う)。代わりに `horizontalHeader()->sortIndicatorChanged` を自前で受けて `SearchController` へソート変更を通知し、再検索する。ソートインジケータは `setSortIndicator()` で手動表示。

`QSortFilterProxyModel` は使わない (フィルタは backend、ソートも backend)。

### `MainWindow` レイアウト

```
QVBoxLayout
 ├ QLineEdit          placeholder「検索…」, clearButtonEnabled, フォーカス初期値
 ├ QToolBar           [All][Image][Video][Audio][Document][Directory]  (QActionGroup 排他, checkable)
 │                    ── separator ── [.*] Regex (checkable, Ctrl+R)
 ├ QTableView
 └ QStatusBar         左: 件数/打ち切り/経過ms  右: backend 名
```

ショートカット: `Ctrl+R` Regex、`Alt+1..6` 種別、`Esc` 検索欄クリア→再度 Esc で検索欄へフォーカス、`Ctrl+L`/`Ctrl+F` 検索欄フォーカス、`↓` で検索欄からテーブルへ、`Enter` 開く、`Ctrl+C` フルパスコピー。

コンテキストメニュー (F8): 開く / フォルダを開いて選択 (`explorer /select,"<path>"`) / フルパスをコピー / 名前をコピー。「開く」は `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`。

### テーマ (F7)

`Theme.cpp` に `applyTheme(QApplication&, ThemeMode)`。`QApplication::setStyle("Fusion")` + `QPalette` を明示構築 (Dark: base `#1e1e1e` / alternate `#252526` / text `#e0e0e0` / highlight `#0a84ff`)。細部の詰めのみ小さな QSS を1つ (`Theme.cpp` 内の文字列リテラル、外部ファイルにはしない)。`System` は `QStyleHints::colorScheme()` を参照し、`colorSchemeChanged` で追従。

---

## 8. Settings design

`QSettings(QSettings::IniFormat, QSettings::UserScope, "efs", "efs")` → `%APPDATA%\efs\efs.ini`。INI 形式にするのは中身を目視・手編集できるようにするため。

```cpp
struct Settings {
    // 外観
    ThemeMode theme = ThemeMode::Dark;
    QByteArray windowGeometry, windowState, headerState;   // Qt の saveGeometry/saveState をそのまま
    // 検索既定値 (前回終了時の状態を復元)
    FileKind  lastKind  = FileKind::All;
    bool      regex     = false;
    bool      matchCase = false;
    bool      matchPath = false;
    SortKey   sortKey   = SortKey::Name;
    SortOrder sortOrder = SortOrder::Asc;
    // 挙動 (UI からは変更しない。INI 直編集で調整する隠し設定)
    int  maxResults    = 5000;
    int  debounceMs    = 120;
    void load(); void save() const;
};
```

- 設定ダイアログは MVP では作らない。UI で触れるのはテーマ切替 (ツールバー右端のトグル) のみ。それ以外は INI を直接編集。自分専用ツールなので設定 UI に工数を割かない。
- `windowGeometry`/`headerState` は `QMainWindow::saveGeometry()` / `QHeaderView::saveState()` の QByteArray をそのまま保存 (自前でジオメトリを分解しない)。
- 保存タイミングは `closeEvent` の1回のみ。

---

## 9. Testing strategy

テストは「壊れやすく、かつ GUI 無しで検証できる部分」に集中する。GUI の自動テストは投資対効果が低いので書かない。

| 層 | 手段 | 内容 |
|---|---|---|
| `EverythingQueryBuilder` | QtTest 単体 (**最重要**) | 各 `FileKind` の前置詞、Regex ON/OFF、空文字入力、`Directory` + テキスト併用、特殊文字を含む入力。ここが仕様の中心。 |
| `Formatting` | QtTest 単体 | サイズ整形 (0 / 1023 / 1MiB / 巨大値 / ディレクトリの -1)、無効 `QDateTime` → `"-"`、ステータスバー文字列 (通常 / 打ち切り / エラー)。 |
| `fileTimeToDateTime` | QtTest 単体 | FILETIME→QDateTime 変換 (境界値・エポック)。`core/` は Win32 非依存にするため、この関数だけは `backend/everything/` に置く。 |
| `ResultTableModel` | QtTest + `QAbstractItemModelTester` | 行数/列数、各ロール、空結果、リセット時のシグナル整合性。 |
| `EverythingBackend` | 統合テスト (条件付き) | 冒頭で `isAvailable()` を見て false なら `QSKIP`。`*.exe` 検索が1件以上返る、`Directory` フィルタで全行 `isDir==true`、`maxResults=10` で `rows.size()<=10 && totalMatches>=10`。 |
| スレッド/stale 破棄 | QtTest + フェイク backend | `ISearchBackend` を実装した `SleepingFakeBackend` (指定ms待って固定結果を返す) を注入し、リクエストを連射 → 最後の id の結果だけが UI に届くことを検証。**backend を抽象化した副次的な利益がここに出る。** |
| UI | 手動チェックリスト (README に記載) | 高速タイプ中に UI が固まらない / Everything を落とした状態での起動と復帰 / テーマ切替 / 列幅とウィンドウ位置の復元 / 打ち切り表示 / 各ショートカット。 |

`ctest` に登録し、`ctest --output-on-failure` 一発で回る状態にする。CI は当面組まない (Everything 依存テストが回らないため、ローカル実行で足りる)。

---

## 10. Implementation phases

### Phase 0 — Walking skeleton (最優先 / 半日)
1. `pip install aqtinstall` → `aqt install-qt windows desktop 6.8.x win64_msvc2022_64 -O C:\Qt`
2. voidtools から Everything SDK を取得し `third_party/everything-sdk/` へ配置。
3. `CMakeLists.txt` + `CMakePresets.json` (Qt パスをプリセットで指定)、空の `QMainWindow` がビルド・起動する。
4. `EverythingApi` で DLL 動的ロード → **ハードコードしたクエリ** (`ext:jpg regex:^IMG_\d+` を含む) を1回実行し `qDebug()` に出力。
   - **ここで 6.2 の「`ext:` + `regex:` 併用」を実機確認する。**
   - post-build で `Everything64.dll` を出力ディレクトリへコピー。

**Phase 0 完了の定義: 「Qt のウィンドウが出る」かつ「Everything から結果が取れる」ことが別々に確認できている。** 技術リスクはこの2点に集中しているので、UI を作り込む前に潰す。

**Phase 0 結果 (完了・受け入れ済み)**: 両方とも確認できた。`ext:` + `regex:` 併用は動作し、6.2 の確定方針どおりに実装してよい。調査用の `src/spike/everything_spike.cpp` は目的を達成したため Phase 1 で削除した。そこで確認した仕様は 6.2 に記載の回帰テストへ移してある。

### Phase 1 — 検索が動く (MVP コア)
`SearchTypes.h` / `ISearchBackend` / `FileKinds` / `EverythingQueryBuilder` / `EverythingBackend` / `SearchWorker` + `QThread` / `SearchController` (デバウンス + id) / `ResultTableModel` / 検索ボックス + テーブル + ステータスバーの `MainWindow`。
→ **type-as-you-search で結果が出て、UI が固まらない。** ここで一度実際に使ってみる。

### Phase 2 — 不満点の解消 (MVP UX / このアプリを作る理由そのもの)
種別フィルタツールバー (F3) / Regex トグル (F2) / ダークテーマ既定 (F7) / ヘッダソート = backend 再検索 (F5) / 打ち切り件数表示 (F6) / 右クリックメニュー + ダブルクリック/Enter で開く (F8)。

**Phase 1 から持ち越した注意事項 → いずれも Phase 2 で解決済み**

1. **Regex ON で空白を含むパターン。** 冒頭の実機検証で確定した。懸念どおり
   空白 (と TAB) で項が割れるため、**`regex:"<入力>"` と引用する**。詳細は 6.2 と
   README の「Phase 2 の検証結果」。

2. **空テキスト + `FileKind != All`。** 「テキストが空」判定は撤廃した。ただし
   ここに書かれていた「`buildQueryString()` が空か」で判定する案は**採らなかった**。
   それだと UI 側の状態機械が Everything 固有のクエリ組み立てに依存してしまう。
   代わりに backend 非依存の述語 `hasSearchConstraint(SearchQuery)`
   (`core/SearchTypes.h`。「テキストが実質空 **かつ** `FileKind::All`」なら false)
   を置き、`SearchController` はこれだけを見る。

**Phase 2 結果 (完了・受け入れ済み)**

- 実装: 種別フィルタツールバー (`QActionGroup` 排他 + `Alt+1..6`) / Regex トグル
  (`Ctrl+R`) / Dark 固定テーマ (`app/Theme.*`。`QPalette` + 小さな QSS。タイトルバーは
  `DwmSetWindowAttribute` を 1 関数へ封じ込め) / ヘッダクリック → backend 再検索の
  ソート / ダブルクリック・Enter で開く / 右クリックメニュー
  (Open / Show in Explorer / Copy Full Path / Copy Name)。
- `SearchController` が検索状態の authority になった。`setKind` / `setRegex` /
  `setSort` は明示操作なのでデバウンスせず即時再検索し、同値の再設定では発行しない。
  経路が増えても stale 破棄は既存の世代 id 1 本で成立している (新しい cancellation
  機構は作っていない)。
- フルパスの組み立ては `core/PathUtils.h` の `fullPath()` 1 箇所に寄せた。
  Everything はドライブ直下を `path="C:"`、ドライブ自体を `path="" name="C:"` で
  返すため、素朴な連結ではドライブ相対パスになってしまう (実測。README 参照)。
- ファイル操作は `app/FileActions.*` に閉じ込め、`QDesktopServices::openUrl` と
  `SHOpenFolderAndSelectItems`(PIDL) を使う。**シェルのコマンド文字列は組み立てない。**
- ツールバーのアイコンは画像アセット / qrc / Qt Svg を持ち込まず `QPainter` で描いて
  いる (`app/ToolbarIcons.*`)。結果行ごとのファイルアイコンは Phase 3 のまま。
- fast sort (下の質問 10) は実測した。サイズ / 日時ソートの劣化は観測されず、
  フォールバックや閾値は追加していない。数値は README。

**Phase 2 完了 = MVP 完成。ここから Everything の代わりに日常使いを開始する。**

### Phase 3 — 定着 (使いながら)
設定の永続化 (F9) / エラー状態表示 (N4: Everything 未起動時の案内) / アイコン (拡張子キャッシュ) / キーボードナビ調整 / `windeployqt` でポータブルフォルダ生成。

**Phase 3 結果 (完了)**

- 実装: `app/Settings.*` (QSettings を触る唯一の場所) / `app/Theme.*` を
  `ThemeMode{System,Dark,Light}` へ拡張 (既定は Dark のまま) /
  `app/RegexValidation.*` (advisory) / `app/IconCache.*` + `app/ShellIcon.*` +
  `app/IconDelegate.*` (アイコン) / `scripts/package.ps1` / CI に package ジョブ。
- **設定復元でクエリを fan-out させない。** `SearchController::restoreOptions()`
  で 4 つの値をまとめて入れ、最後に 1 回だけ `dispatch()` する。`setKind` /
  `setRegex` / `setSort` を順に呼ぶ実装だと復元だけで最大 3 本飛ぶ。
  検索欄は起動時に空なので、実際に飛ぶのは「復元された kind が All 以外」の
  ときの filter-only 1 本だけ (All なら 0 本)。回帰テストは
  `restoredDefaultOptionsIssueNoQuery` / `restoredFileKindIssuesAtMostOneQuery` /
  `restoreDoesNotFanOutIntoMultipleQueries`。
- 検索文字列は保存しない (search history と意味が混ざるため)。
  `searchTextIsNotPersisted` で INI のキー一覧ごと固定した。
  enum は int の生値ではなく安定した文字列で書き、未知値・別スキーマは既定値へ戻す。
- **不正な正規表現の扱い**: 6.2 に書いたとおり Everything は 0 件を返すだけなので、
  UI 側で `QRegularExpression` を **best-effort advisory** として使う。
  実装前に陽性対照ファイルで 28 パターンの互換性 probe を行い、
  **「Qt が invalid と言うのに Everything では動く」ケースが 1 つも無い**ことを
  確認した (先読み / 後読み / `(?i)` / `\s` / Unicode まで一致。README 参照)。
  判定がどうであれユーザーのパターンは書き換えず、検索は既存経路で渡す。
- **アイコンは種別単位。** キーは `dir:` / `ext:<小文字>`。lookup は専用スレッド
  1 本で、同じキーの要求は重複させない。実ファイルには触れず
  `SHGetFileInfoW` + `SHGFI_USEFILEATTRIBUTES` を使う。完了通知は行番号を持たない
  ので、モデル reset と競合しない。`ResultTableModel` は plain data の
  `IconKeyRole` (ただの QString) を返すだけで QtGui 依存へ広がっていない。
  `efs_core` には `Qt6::Gui` (QImage) だけを足し、**Widgets と Win32 は入れていない**。
- deviation (計画との差分):
  1. 計画 7 は `DecorationRole` を `ResultTableModel` に持たせる想定だったが、
     モデルを QtGui 依存にしないため `QStyledItemDelegate::initStyleOption` へ移した。
  2. `QApplication::setStyle()` は既に Fusion なら呼び直さない。テーマ切替の
     たびに呼ぶとステータスバーの表示中メッセージが消えたため (実測)。
  3. ツールバーアイコンの色を固定値から palette の `ButtonText` に変えた。
     固定色のままだと Light テーマで見えなくなる (実測)。
  4. 計画 8 にあった `matchCase` / `matchPath` / `maxResults` / `debounceMs` の
     永続化は入れていない。UI から変更する経路が無く、保存しても意味が無いため。
- 実測: `Image` + Regex ON の検索は Everything 側が全走査になり **7.7〜24.5 秒**
  かかる。UI はブロックしないが、IPC クエリは中断できない契約なので、その間
  新しい検索は始まらない。**Everything 側の所要時間には手を入れない**
  (cancel / timeout / fallback は追加しない)。

**P3 review で追加した修正**

1. **in-flight search の表示 authority。** 上記の 24.5 秒の間、検索欄は新しい
   query なのに直前の結果が表示・操作できてしまっていた。`SearchController` に
   `searchStarted` (クエリ発行時に同期発火) を足し、`MainWindow` はそこで
   結果を空にし、backend error を下ろし、`Searching…` を出す (Regex の構文警告は
   維持)。条件が無いときは従来どおり `cleared`。
   `MainWindow` の ctor は **signal 接続とステータス初期化を `restoreOptions()`
   より前**へ移した — 逆順だと復元時の初回 filter-only クエリだけ `Searching…` を
   取りこぼす。`restoreOptions()` が fan-out しない契約は維持
   (`searchStarted` の回数でも固定した)。
2. **COM lifecycle。** `ShellIcon.cpp` の `thread_local` 初期化を RAII 化し、
   `CoInitializeEx` が成功した (`S_OK` / `S_FALSE`) ときだけスレッド終了時に
   `CoUninitialize` を 1 回呼ぶ。失敗時 (`RPC_E_CHANGED_MODE` 等) は呼ばない。
   汎用 COM フレームワークは作っていない。
3. **IconDelegate の cache churn。** placeholder は `m_pixmaps` に入れていない
   ため `imagesReady` で全 clear する必要が無かった。`invalidate()` を削除し、
   `viewport()->update()` だけにした。別キーのアイコン到着で解決済みキーを
   `QImage`→`QPixmap` 再変換しない。

### Phase 4 — 常駐と配置 (Phase 3 完了後に追加で決めたスコープ)

Phase 3 完了時点で日常利用を開始し、**実際に使ってみて欲しくなったものだけ**を
取り出したフェーズ。当初 Phase 4 に置いていた「将来 backend の受け皿」は
実利用の要求ではないので後ろ (Phase 5) へ回した — 順番の authority は
「不満が実在するか」であって、当初の並びではない。

やること:

1. **タスクトレイ常駐** — `QSystemTrayIcon`。閉じるボタンは終了ではなく非表示。
   終了はトレイメニューの Quit だけ。
2. **グローバルホットキー** — `RegisterHotKey`。既定 `Ctrl+Alt+E`。
   常駐の実質的な前提 (ホットキーが無いと常駐する意味が薄い)。
3. **多重起動防止** — 常駐アプリなので必須。2 個目の起動は既存インスタンスを
   前面に出して自分は終了する。
4. **`Program Files` への配置** — 管理者用のコピースクリプト
   (`scripts/install.ps1`)。**インストーラ (MSI/MSIX/NSIS/WiX) は作らない**、
   コード署名もしない (個人用ツールであり、未署名の SmartScreen 警告を避けられる
   方式を選んだ)。

やらないこと (Phase 4 でも scope 外):
インストーラ / コード署名 / 自動更新 / 検索履歴 / お気に入り / D&D /
Everything の自動起動 / Everything 1.5 対応。

確定事項:

- **設定の保存先は `%APPDATA%\efs\efs.ini` のまま。** `Program Files` 配下は
  非管理者から書けないが、efs はインストール先に書きに行かないので変更不要。
  この性質を壊さないこと (設定を exe の隣へ置く「ポータブル版」は作らない)。
- **Everything は引き続き別途常駐が必要。** efs は検索エンジンを持たない。
  Everything 側の `show_tray_icon=0` にすればトレイに出るのは efs だけになる
  (これは Everything の設定であり、efs のコードは関与しない)。
- ホットキーは INI に `Ctrl+Alt+E` のような**文字列**で保存する
  (int の生値にしない)。解釈は `app/HotkeySpec.*` の純粋関数に寄せ、
  Win32 の `MOD_*` / VK への写像だけを `app/GlobalHotkey.cpp` に置く。
- **登録に失敗しても起動を止めない。** 他アプリと衝突していることは普通に
  起こる。非モーダルに理由を出し、アプリは通常どおり使えること。
- スタートアップ登録はショートカットに `--tray` を渡す方式にし、
  「起動時に隠す」ための設定項目は増やさない。

**Phase 4 結果 (完了)**

- 実装: `app/HotkeySpec.*` (INI 文字列 ⇄ 修飾キー+キーの純粋関数) /
  `app/GlobalHotkey.*` (`RegisterHotKey` + native event filter) /
  `app/SingleInstance.*` (名前付き mutex + `RegisterWindowMessageW`) /
  `MainWindow` のトレイ常駐と `showAndActivate()` / `ToolbarIcons::appIcon()` /
  `scripts/install.ps1`。
- **ホットキーは HWND を持たせず `RegisterHotKey(nullptr, …)` で登録する。**
  `WM_HOTKEY` はスレッドのメッセージキューへ届くので、ウィンドウが隠れていても
  Qt の native event filter で拾える。ウィンドウの生成/破棄に左右されない。
  `MOD_NOREPEAT` を必ず付ける (押しっぱなしで前面化を連打しない)。
- **`showAndActivate()` が唯一の復帰経路。** ホットキー / トレイのクリック /
  2 個目の起動 のすべてがここへ入る。最小化されている場合は `showNormal()` では
  なく最小化ビットだけを落とす (最大化状態を潰さないため)。
- **多重起動防止に Qt Network は使わない。** 名前付き mutex (`Local\` 名前空間 =
  セッション単位) と `RegisterWindowMessageW` のブロードキャストで足りる。
  `QLocalServer` のためだけに Qt のモジュールを増やさない。
- **設定の保存経路が 2 本になった。** 閉じる = 終了ではなくなったので、
  `closeEvent` (隠す前) と トレイの Quit の両方から `saveSettings()` を呼ぶ。
  片方だけにすると、その経路で終わったときだけ設定が飛ぶ。
**P4 review で追加した修正**

1. **native event の種別を決め打たない。** `windows_generic_MSG` だけを見て
   いたが、Qt が system-wide message を `windows_dispatcher_MSG` で渡す経路も
   あるため両方受ける。実測 (Qt 6.8.3) では `WM_HOTKEY` は
   `windows_generic_MSG` で届いた。ID の照合は維持。
2. **`efs.exe --quit`。** 閉じる = 隠す にした結果、`CloseMainWindow` では
   終了しなくなり、`install.ps1` の「行儀よく閉じる → 駄目なら Kill」が
   **通常経路で必ず Kill** になっていた (= 設定が保存されない)。既存の
   SingleInstance IPC を wParam 1 つ分だけ拡張して `--quit` を足し、tray の
   Quit と同じ `MainWindow::quitApplication()` へ合流させた。
3. **install.ps1 を実際に fail-closed にした。** 「旧版を消してからコピー」を
   staging + backup + 失敗時 rollback へ変更。ショートカット作成の失敗も
   rollback 対象 (既存 `.lnk` は先に退避)。実行中プロセスの照合は前方一致から
   `<Destination>\efs.exe` との完全一致へ。
4. **`CreateMutexW` の 3 状態を区別。** NULL を Secondary 扱いして exit 0 して
   いたのをやめ、`InstanceRole::{Primary,Secondary,Error}` にした。
   `RegisterWindowMessageW` の失敗も同じ infrastructure error として扱う。
5. **ホットキーの綴りで空の項を読み飛ばさない** (`Ctrl++Alt+E` 等を invalid に)。
   `Qt::SkipEmptyParts` をやめ、table-driven の回帰テストを追加した。

- deviation (計画との差分):
  1. `QApplication::setQuitOnLastWindowClosed(false)` を入れた副作用として、
     **トレイが使えない環境では閉じてもプロセスが残る**。`closeEvent` の
     非トレイ経路で明示的に `QApplication::quit()` を呼んで塞いだ。同じ理由で
     `--tray` はトレイが使えるときだけ隠して起動する (`MainWindow::hasTrayIcon()`)。
     トレイ不在は実機では起きていないが、「呼び出す手段が無いのに生きている
     プロセス」は自力で復帰できないので防御した。
  2. ホットキーを変更する UI は作っていない。INI (`hotkey/show`) の直編集のみ。
     設定ダイアログを作らない方針は Phase 3 から変えていない。
  3. `install.ps1` は「管理者かどうか」ではなく**実際に書けるか**で事前確認する。
     `-Destination` にユーザー書き込み可能な場所を指せば昇格なしでスクリプト自体を
     検証できる。ショートカット 2 つ (スタートメニュー / スタートアップ) は
     ユーザープロファイル配下なので昇格が要るのは Program Files へのコピーだけ。

### Phase 5 — 将来 backend の受け皿 (着手は任意)
`BackendFactory` に `NativeNtfsBackend` の空実装を追加 (`isAvailable()` は false を返す) / backend 共通の適合テストスイート (両実装が同じテストを通ることを保証) / INI の隠し設定で backend 選択。
**MFT/USN の実装そのものはここでは行わない。** Everything を日常利用して「Everything 自体に不満が残る」と分かった時点で初めて着手する。現時点でその不満は出ていないので、**このフェーズは着手条件を満たしていない。**

### Phase 6 — 実利用で不満が出たときだけ着手する候補 (未確定)

日常利用しながら溜める置き場。**ここに書いてあることは「やる」ではなく
「不満として実在したらやる」。** 先回りして実装しない (Phase 4 が実際に
そうやって選ばれた)。着手するときは 1 フェーズ 1 テーマに切り出す。

| 候補 | 着手条件 |
|---|---|
| 検索履歴 (直近のクエリを ↑↓ で戻す) | 同じクエリを打ち直す回数が実際に鬱陶しくなったら。永続化するか (= 検索文字列を INI に書くか) はそのときに決める — Phase 3 の「検索文字列を永続化しない」を覆す判断になる |
| `matchPath` / `matchCase` のトグル | パス照合が欲しい場面が実際に出たら。**設定ダイアログは作らず**ツールバーのトグルとして足す |
| 種別フィルタの拡張子リストの編集 | ハードコードした一覧で困ったら。INI 化はそのとき初めて検討する |
| 結果のプレビュー / 複数タブ / D&D | 現状スコープ外。要求が出るまで検討もしない |
| Everything 1.5 対応 | 1.5 が stable になり、かつ 1.4 で困ってから |

恒久的にやらないもの (候補にも入れない): インストーラ / コード署名 / 自動更新 /
i18n 機構 / DI フレームワーク / プラグイン機構。

---

## Verification

各フェーズの検証手順:

```powershell
# ビルド
cmake --preset msvc2022-x64-debug
cmake --build --preset msvc2022-x64-debug

# 単体テスト
ctest --preset msvc2022-x64-debug --output-on-failure

# 実行
.\build\msvc2022-x64-debug\efs.exe
```

MVP (Phase 2 終了時) の受け入れ確認:
1. 起動 → 検索欄にフォーカスがある。ダークテーマ。
2. `a` から `abcdef` まで高速タイプ → 一度も固まらず、最終的に `abcdef` の結果が表示される (古い結果が残らない)。
3. `Ctrl+R` → Regex ON の見た目に変わり、`^IMG_\d+` が期待通りヒット。
4. `Alt+2` (Image) → 結果が画像拡張子のみ。Regex ON のまま種別フィルタも効く。
5. Size 列ヘッダをクリック → 降順で最大サイズのファイルが先頭 (打ち切り 5,000 件内ではなく **全体の最大**であること)。
6. ステータスバーに `N 件 (上限 5,000 件表示) / xx ms`。
7. 行をダブルクリックで開く。右クリック → 「フォルダを開いて選択」で Explorer が該当ファイルを選択状態で開く。
8. Everything.exe を終了 → 検索するとステータスバーに「Everything が起動していません」。再起動すると復帰する (アプリの再起動不要)。
9. ウィンドウを移動/リサイズ、列幅変更 → 終了 → 再起動で復元。

---

## 実装開始前に決めておきたいこと (質問)

1. **アプリ名 / exe 名**は `efs` でよいか。ウィンドウタイトルの表示名は？
2. **UI の言語**: 日本語 / 英語 どちらにするか (本プランは暫定的にステータスバー等を日本語で記述)。i18n 機構は入れず、どちらか一方に固定する想定。
3. **git リポジトリを初期化するか** (現在 `c:\dev\soft\efs` は非 git)。初期化するなら `.gitignore` と初回コミットを Phase 0 に含める。
4. **Everything 未起動時の挙動**: 案内メッセージのみ / `Everything.exe` の自動起動を試みる、どちらにするか (自動起動は実装は容易だが、意図しないプロセス起動になる)。
5. **`maxResults` の既定値 5,000** で妥当か。もっと多く (20,000) 表示したいか。
6. **既定の照合設定**: `matchPath=false` (ファイル名のみ照合)、`matchCase=false` でよいか。Everything の現在の使い方と揃えたい。
7. **種別フィルタの拡張子リスト**をソースにハードコードするか、INI で編集可能にするか (MVP はハードコードを推奨。編集は Phase 3 以降)。
8. **Everything 1.5 系へ将来移行する予定はあるか** (1.5 は別 SDK。予定があるなら `EverythingApi` を 1.4/1.5 両対応の形で切っておく余地を残す)。
9. ~~**`ext:` と `regex:` の併用**が Everything 1.4.1 で動作しない場合、Regex ON 時は「種別フィルタを無効化 (グレーアウト)」と「拡張子を正規表現に畳み込む」のどちらを採るか。~~ → **解決済み。併用は動作するので、この問い自体が不要になった (6.2 参照)。**
10. ~~Everything の設定で **サイズ/日時の fast sort が有効になっているか** — 無効だとソート時のクエリが目に見えて遅くなる可能性がある。Phase 2 で実測して判断でよいか。~~ → **解決済み。** Phase 2 で実測した (Everything の設定は変更していない)。約 400 万件ヒットのクエリで Name / Path / Size / Date Modified のいずれも median 250〜400ms 程度に収まり、**サイズ / 日時ソートだけが遅いという事象は無かった**。フォールバックは不要 (README の実測表を参照)。なお filter-only の Document (拡張子 14 個) は 1 秒を超えることがあり、これが現状もっとも重いクエリ。
