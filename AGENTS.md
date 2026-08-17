# AGENTS.md

`efs` — Everything をバックエンドに使う Windows 向けファイル検索 UI。
C++20 / Qt 6.8 / CMake / MSVC 2022 / x64 のみ。

このファイルは人間と AI エージェントの双方に向けた作業規約。

---

## 言語ポリシー

- **人間が読む想定の文章はすべて日本語で書く。** 対象は AI チャットの返答、
  コードコメント、コミットメッセージ、PR 説明、ドキュメント、設計メモ、
  開発者向けの診断出力・ログ・エラー文字列。
- **例外は 2 つだけ。**
  1. **アプリの UI 文言は英語** (確定済みの意思決定)。ボタン・メニュー・
     ステータスバー等、エンドユーザーが見る文字列は英語で書く。
  2. **識別子・API 名・エラーコード名は原語のまま**。`GetNumResults`、
     `EVERYTHING_ERROR_IPC`、`ext:` などを無理に訳さない。日本語の説明を
     添える形にする。
- 型名・変数名・関数名は英語。日本語のローマ字表記は使わない。

---

## 設計方針

### DRY
- **2 箇所までは重複を許し、3 箇所目が現れた時点で共通化を検討する。**
  1 回の重複で抽象化を作ると、たいてい間違った抽象になる。
- ただし「仕様そのもの」が重複している場合は 2 箇所目でも寄せる
  (例: 拡張子リスト、エラーコードの写像)。ずれると壊れるため。

### 責務の分離
守るべき境界は少数。これらは実際に効くので厳格に守る。

| 境界 | 規則 |
|---|---|
| UI ↔ 検索バックエンド | UI コードは `Everything.h` を include しない。`ISearchBackend` 越しにのみ触る。 |
| Everything SDK の封じ込め | `Everything.h` を include してよいのは `src/backend/everything/` 配下と、SDK 自体を直接検証する `tests/` のみ。SDK の include パスは `efs_core` の **PRIVATE** に置き、リンクしただけでは伝播させない。 |
| スレッド | Everything SDK の呼び出しは検索スレッド 1 本に直列化する。SDK はグローバル状態を持つのでスレッドプール化は禁止。 |
| 純粋関数 | クエリ組み立て・書式整形は Qt 以外に依存しない自由関数にし、単体テストの主戦場にする。 |
| `efs_core` の依存 | `Qt6::Core` + `Qt6::Gui` まで。**`Qt6::Widgets` と Win32 を足さない。** Widgets は `MainWindow` / `IconDelegate` 等、Win32 は `Theme.cpp` / `FileActions.cpp` / `ShellIcon.cpp` に閉じ込める。 |
| 設定 | `QSettings` を触ってよいのは `app/Settings.*` だけ。MainWindow の各所から直接呼ばない。 |

### オーバーエンジニアリングを避ける
これは個人用ツールであり、最優先は**日常利用できる MVP を早く完成させること**。

- **やらないこと**: DI フレームワーク、イベントバス、プラグイン機構、
  抽象ファクトリの階層、`CancelToken` のような追加抽象、設定ダイアログ、i18n 機構。
- 抽象化は「将来 backend を 1 個差し替えられる」一点のみに絞る。それ以外の
  一般化は、必要になった時点で入れる。
- インタフェースにメンバを増やすのは、2 つ目の実装が実際に要求したときだけ。
- 最終アーキテクチャのファイルを先回りして作らない。フェーズの範囲を守る。

### フォールバック・互換性は最小
**開発中なので、防御的コードは最小でよい。**

- Everything 1.4.1 だけを対象にする。1.5 系の互換レイヤは作らない。
- 1.4 用の境界を「将来 1.5 でも使えるように」と一般化しない。
- 想定外の入力に対する多段のフォールバックを書かない。前提が崩れたら、
  黙って別経路に逃げるより**明確に失敗して理由を出す**方が良い。
- 実測で必要と分かってから対処する。推測でフォールバックを実装しない。
- 例外は投げない。バックエンドはエラーを戻り値 (`SearchResults::error`) で返す契約。

---

## ディレクトリ構成

```
src/core/                 Qt Core のみに依存。Widgets / Win32 / Everything 非依存
src/backend/everything/   Everything SDK の唯一の利用箇所
src/app/                  UI と検索の駆動。Widgets に依存するのは MainWindow と main() だけ
tests/                    QtTest。ctest に登録
docs/                     implementation-plan.md (計画の authority)
scripts/                  補助スクリプト
tools/                    ビルド時に走らせるだけの道具 (出荷物ではない)
third_party/              ベンダリングした Everything SDK。整形・lint の対象外
```

---

## 開発コマンド

```powershell
# 初回のみ
pre-commit install

# ビルド
cmake --preset msvc2022-x64
cmake --build --preset msvc2022-x64-debug        # または -release

# テスト
ctest --preset msvc2022-x64-debug

# 整形 (全ファイル)
pre-commit run --all-files

# lint (Developer PowerShell が必要。compile_commands.json を使う)
cmake --preset ninja-x64-debug
cmake --build --preset ninja-x64-debug
pwsh scripts/lint.ps1 -Bootstrap   # 初回のみ: CI と同じ 22.1 系を .tidy22 に用意
pwsh scripts/lint.ps1

# カバレッジ (要 OpenCppCoverage)
pwsh scripts/coverage.ps1

# 配布ディレクトリ (dist\efs\) を作る
pwsh scripts/package.ps1
```

実行時は Qt の DLL に PATH を通す:
`$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"`

### ハマりどころ (確認済み)

- **clang-tidy は x64 版を使う。** VS 同梱の `VC\Tools\Llvm\bin` は 32bit 版で、
  Qt のヘッダを解析するとアクセス違反 (0xC0000005) で落ちる。
  `VC\Tools\Llvm\x64\bin` を使うこと (`scripts/lint.ps1` がそうしている)。
- **ローカルと CI で clang-tidy のバージョンが違う。** ローカルは VS 2022 同梱の
  19.1.5、CI ランナーはより新しい VS 同梱版 (2026-08 時点で **VS 18 の 22.1.3**)。
  新しい方でだけ有効な check があり、**ローカルが緑でも CI が落ちることがある**。
  実例 1: `modernize-use-integer-sign-comparison` は clang-tidy 20 で追加されたため
  19.1.5 では検出されず、CI だけが失敗した。
  実例 2: `modernize-avoid-c-style-cast` (22 で有効) は **`quint32(0)` のような
  関数形式のキャストも C-style cast として弾く**。`QDataStream` へ数値を流すときは
  `static_cast<T>(...)` か**型付きの定数**を使うこと (`tools/make_app_icon.cpp`
  がそうしている)。同時に `performance-unnecessary-copy-initialization` も
  19.1.5 より広く効く (`const QString x = list.at(i);` → 参照にする)。
  `WarningsAsErrors: '*'` なので新しい check は即エラーになる。
  **そこで `lint.ps1` の既定を CI と同じ 22.1 系にしてある。** リポジトリ直下の
  `.tidy22` (venv。`.gitignore` の `.tidy*/` で無視される) を最優先で探し、
  無ければ `-Bootstrap` で用意できる。CI ランナーには venv が無いので同梱版へ
  フォールバックする。見つかった版が 22.1 系でなければ**警告を出してから実行する**
  (止めはしない — 手元に古い版しか無くても lint 自体はできる方がよい)。

  ```powershell
  pwsh scripts/lint.ps1 -Bootstrap          # .tidy22 に clang-tidy 22.1.8 を入れる
  pwsh scripts/lint.ps1                     # 以後は既定でそれが使われる
  pwsh scripts/lint.ps1 -ClangTidy <path>   # 別の版を試すとき
  ```

  期待バージョン (`$expectedVersion`) と `-Bootstrap` が入れる版
  (`$pinnedVersion`) は `lint.ps1` の冒頭にある。**CI ランナーが上がったら
  ここを上げる。**
  **CI と同じパッチ版が PyPI に無いことがある** (CI の 22.1.3 は無く、22.1.7 /
  22.1.8 が最寄り)。check セットはマイナー版で決まるので `22.1.x` を合わせれば
  再現できる。利用可能な版は
  `.tidy22\Scripts\pip index versions clang-tidy` で見る。
  実測: 22.1.8 で `tools/make_app_icon.cpp` の CI failure を再現・修正確認した。

  なお pip が `pypi.ngc.nvidia.com` へのリトライ警告を延々出すことがある
  (NVIDIA のツールが `extra-index-url` を書いている。この環境では名前が引けない)。
  **インストール自体は pypi.org へフォールバックして成功する**ので無視してよい。
  黙らせるなら venv の中だけで上書きする:
  `.tidy22\Scripts\pip config --site set global.extra-index-url ""`
- **ビルドの並列化はプリセットに入っている。** MSBuild はターゲット内も
  ターゲット間も既定で直列に走るため、`/MP` (top-level の `add_compile_options`。
  **Visual Studio ジェネレータのときだけ**) と build プリセットの `"jobs": 0`
  (= `--parallel`) の**両方**が要る。片方だけだと大して速くならない
  (実測: 95 s → 38 s → 9 s。README の表)。素の `cmake --build <dir>` を叩くとき
  だけ `--parallel` を自分で付けること。
- **QtTest はリダイレクトされると標準出力に何も書かない。** Windows では
  コンソールが無いと判断すると `OutputDebugString` へ送るため、ctest から
  実行すると結果が見えない。`tests/CMakeLists.txt` で
  `QT_ASSUME_STDERR_HAS_CONSOLE=1` を設定して回避している。

---

## テスト方針

壊れやすく、かつ GUI 無しで検証できる部分に集中する。GUI の自動テストは
投資対効果が低いので書かない。

- クエリ組み立て、書式整形、テーブルモデル、stale 破棄 → 単体テストを書く。
- Everything に依存するテストは、冒頭で利用可否を見て**利用できなければ
  `QSKIP`**。CI には Everything が無いため、失敗させてはならない。
- カバレッジに閾値は設けない。Everything 依存部が CI で skip される以上、
  数値の絶対量に意味が無い。

---

## コミット

- メッセージは日本語。1 行目は要約 (50 字程度)、必要なら空行を空けて本文。
- **なぜそうしたか**を書く。何をしたかは diff を見れば分かる。
- 1 コミット 1 論点。整形だけの変更と実装の変更を混ぜない。
- コミット前に `pre-commit run --all-files` とビルド・テストを通す。

---

## フェーズ

計画の authority は [docs/implementation-plan.md](./docs/implementation-plan.md)。
現在 **Phase 4 完了**。各フェーズの範囲外に手を出さない。

| Phase | 内容 |
|---|---|
| 0 | Qt 導入、Everything SDK の動的ロード、`ext:` + `regex:` の実機検証 (完了) |
| 1 | 検索が動く MVP コア (type-as-you-search、ワーカースレッド、結果テーブル) (完了) |
| 2 | 種別フィルタ、Regex トグル、ダークテーマ、ソート、右クリックメニュー = MVP 完成 (完了) |
| 3 | 設定永続化、テーマ切替、エラー表示、Regex 構文警告、結果アイコン、`windeployqt` 配布 (完了) |
| 4 | トレイ常駐、グローバルホットキー、多重起動防止、`Program Files` への配置 (完了) |
| 5 | 将来 backend の受け皿 (着手条件を満たしていない) |
| 6 | 実利用で不満が出たときだけ着手する候補の置き場 (未確定) |

Phase 4 と 5 は当初と逆順にした。順番の authority は「不満が実在するか」であり、
当初の並びではない。Phase 6 に並んでいるのは **「やる」ではなく「不満として
実在したらやる」** 候補なので、先回りして実装しない。

---

## 確定済みの意思決定

判断を蒸し返さないための一覧。

- exe 名は `efs`。UI は英語。
- `maxResults` = 5000、`matchPath` = false、`matchCase` = false。
- 種別フィルタの拡張子リストはソースにハードコードする (INI 化しない)。
- Everything が未起動でも**自動起動しない**。理由を提示するだけ。
- Everything 1.5 は対象外。
- ソートはバックエンド側 (`Everything_SetSort`) で行う。`QTableView` の
  `setSortingEnabled` は false のままにする (打ち切られた範囲内だけで
  並べ替えると、全体の正しい上位 N 件と食い違うため)。
- **`ext:` と `regex:` は併用できる** (Phase 0 で実機検証済み)。
  `Everything_SetRegex` は常に FALSE のままにし、正規表現は `regex:` 項として
  クエリ文字列に埋め込む。計画に書かれていた「拡張子を正規表現に畳み込む」
  フォールバックは**不要であり、実装してはならない**。
- **正規表現は `regex:"<入力>"` と必ず引用する** (Phase 2 で実機検証済み)。
  Everything の search-term parser は空白と TAB を regex エンジンより先に項の
  区切りとして解釈するため、引用しないと 1 つのパターンが複数の項へ割れる。
  空白を含まないパターンでも結果は変わらないので無条件に囲む。パターン内部の
  エスケープは補わない。
- **Regex ON ではユーザーのパターンを一字も変えない。** 引用の内側では前後の
  空白も TAB も意味を持つ。trim してよいのは Regex OFF のときだけ。空白だけの
  パターンは有効な検索条件 (`regex:" "` = 名前に空白を含む)。
- **種別フィルタは hard constraint。** 種別項があり Regex OFF のときは、ユーザー
  式全体を Everything のグルーピング `<...>` で囲む。演算子の優先順位は Everything
  の設定で変えられるため、`ext:... a|b` のままだと種別が OR の片側から外れうる。
  `( )` はグルーピングではない (括弧を含む名前を探しに行く)。
- **検索するかどうかの判定は `hasSearchConstraint()`** (`core/SearchTypes.h`)。
  UI 側から `buildQueryString()` を呼んで空判定する設計にはしない (UI が
  Everything 固有のクエリ組み立てに依存してしまうため)。**この述語と
  `buildQueryString()` は空白について同じ契約であること** — 片方だけ変えると
  「条件はあるのにクエリが空」になる。
- **検索状態の authority は `SearchController`**。UI は `SearchQuery` を直接
  編集しない。テキストだけがデバウンス対象で、種別 / Regex / ソートの変更は
  明示操作なので即時再検索する (同値の再設定では発行しない)。
- **フルパスは `core/PathUtils.h` の `fullPath()` だけが組み立てる。**
  Everything はドライブ直下を `path="C:"`、ドライブ自体を `path="" name="C:"` で
  返すので、素朴な連結ではドライブ相対パスになる。
- **シェルのコマンド文字列を組み立てない。** ファイルを開く / Explorer で選択は
  `QDesktopServices::openUrl` と `SHOpenFolderAndSelectItems`(PIDL) を使い、
  Windows 固有処理は `app/FileActions.*` / `app/Theme.cpp` / `app/ShellIcon.cpp`
  に閉じ込める。

### Phase 3 で追加した不変条件

- **設定の復元でクエリを fan-out させない。** 起動時は
  `SearchController::restoreOptions()` で kind / regex / sortKey / sortOrder を
  まとめて入れ、**最後に 1 回だけ** dispatch する。`setKind()` → `setRegex()` →
  `setSort()` と順に呼ぶ実装へ戻すと、復元だけで最大 3 本のクエリが backend へ
  飛ぶ。検索欄は起動時に空なので、発行されるのは高々 1 本
  (復元された kind が `All` なら 0 本)。回帰テストは `test_search_controller.cpp` の
  `restoreDoesNotFanOutIntoMultipleQueries` ほか。
- **表示中の結果と現在の query を食い違わせない。** `SearchController` は
  クエリ発行時に `searchStarted` を**同期で**発火し、`MainWindow` はそこで結果を
  空にして `Searching…` を出す。Everything の IPC クエリは中断できず 20 秒以上
  かかることがあるため、これが無いと「検索欄は別の条件なのに古い結果を開ける」
  状態になる。**cancel / timeout / fallback は追加しない** — 直すのは表示だけ。
  また `MainWindow` は controller の signal を繋いだ**後で** `restoreOptions()` を
  呼ぶこと。逆にすると復元時の初回クエリだけ `Searching…` を取りこぼす。
- **検索文字列を永続化しない。** search history と意味が混ざる。Phase 3 の
  スコープ外であり、`test_settings.cpp` の `searchTextIsNotPersisted` が
  INI のキー一覧ごと固定している。
- **アイコンの lookup を result paint / `data()` から実ファイルへ同期で行わない。**
  `DecorationRole` のたびに `QFileInfo` / `QFileIconProvider` を実パスへ呼ぶと
  5,000 行のスクロールでディスク I/O に張り付く。lookup の単位は「ファイル」では
  なく**種別** (`dir:` / `ext:<小文字>`) で、専用スレッド 1 本、同じキーの要求は
  重複させない。`SHGetFileInfoW` には必ず `SHGFI_USEFILEATTRIBUTES` を付け、
  実ファイルへは触らない。`HICON` は `QImage` へコピー後に必ず `DestroyIcon`。
- **アイコン完了通知に行番号を持たせない。** `IconCache::imagesReady` は
  viewport の塗り直しだけを促す。行を指す通知にすると、モデル reset 後や
  高速な検索切替中に古い行を触って壊れる。またここで `IconDelegate` の
  `QPixmap` cache を全 clear しないこと — 未解決キーの placeholder は
  cache に入れていないので塗り直すだけで本物に差し替わる。clear すると
  アイコンが 1 つ届くたびに解決済み全件を再変換する。
- **COM の初期化と解放を対にする。** `ShellIcon.cpp` の `CoInitializeEx` が
  成功した (`S_OK` / `S_FALSE`) ときだけ、そのスレッドの終了時に
  `CoUninitialize` を 1 回呼ぶ (`thread_local` な RAII)。`RPC_E_CHANGED_MODE`
  等の失敗で呼ぶと、他所が張った初期化を剥がしてしまう。
- **`windeployqt` だけでは `Everything64.dll` は入らない。** Qt の依存しか見ない
  ので、`scripts/package.ps1` の明示コピーを消さないこと。消すと配布版だけが
  「検索が全部失敗する」状態になる。
- **Regex の validator を backend の authority にしない。** `validateRegex()` は
  UI の見た目のための best-effort であり、invalid と判定してもユーザーの
  パターンを書き換えず、検索は既存経路でそのまま Everything へ渡す。
  **「0 件だから invalid」と判定してはならない** (Everything は構文エラーでも
  0 件を返すが、valid でも 0 件はありうる)。
- **テーマ切替で `QApplication::setStyle()` を呼び直さない** (既に Fusion なら)。
  全 widget が再 polish され、ステータスバーの表示中メッセージが消える。
  また stylesheet は追記せず毎回まるごと差し替える (累積させない)。
- **ツールバーアイコンの色を固定値で持たない。** palette の `ButtonText` から
  取り、テーマ変更時に描き直す (`MainWindow::refreshToolbarIcons`)。固定色だと
  Light テーマで見えなくなる。

### Phase 4 で追加した不変条件

- **閉じるボタンは終了ではない。** トレイが使えるなら `closeEvent` は
  `saveSettings()` + `hide()` だけを行う。**意図的な終了処理の authority は
  `MainWindow::quitApplication()` の 1 本** (`saveSettings()` →
  `QApplication::quit()`) で、caller はトレイメニューの `Quit` と
  `efs.exe --quit` の IPC の 2 つ。終了経路を増やすときも必ずここへ合流させる。
  したがって**設定を保存するコード経路は `closeEvent` と `quitApplication()` の
  2 本**あり、片方だけにすると、その経路で終わったときに設定が飛ぶ。
- **「呼び出す手段が無いのに生きているプロセス」を作らない。**
  `setQuitOnLastWindowClosed(false)` の副作用なので、トレイが使えない環境では
  `closeEvent` から明示的に `QApplication::quit()` する。同じ理由で `--tray` は
  `MainWindow::hasTrayIcon()` が true のときだけ隠して起動する。
- **復帰経路は `MainWindow::showAndActivate()` の 1 本に集約する。**
  ホットキー / トレイのクリック / 2 個目の起動 がすべてここへ入る。
  最小化されている場合は `showNormal()` ではなく最小化ビットだけを落とす
  (最大化していた状態を潰さないため)。
- **グローバルホットキーは HWND を持たせず `RegisterHotKey(nullptr, …)` で
  登録する。** `WM_HOTKEY` はスレッドのメッセージキューへ届くので、ウィンドウが
  隠れていても・作り直されても native event filter で拾える。`MOD_NOREPEAT` を
  必ず付ける (押しっぱなしで前面化を連打しない)。
- **ホットキーの登録に失敗しても起動を止めない。** 他アプリとの衝突は普通に
  起こる。非モーダルに 1 行出すだけで、アプリは通常どおり使えること。
- **ホットキーは INI に文字列で持つ** (`hotkey/show` = `Ctrl+Alt+E`。int の生値に
  しない)。表記の解釈は `app/HotkeySpec.*` の純粋関数、Win32 の `MOD_*` /
  仮想キーへの写像は `app/GlobalHotkey.cpp` だけ。**修飾キー無しは拒否**
  (OS 全体でそのキーを奪う)。空文字は「意図的に無効」として尊重し、解釈できない
  綴りは既定へ戻す。変更する UI は作らない (設定ダイアログを作らない方針は維持)。
- **多重起動防止のために Qt Network を入れない。** 名前付き mutex (`Local\` =
  セッション単位) と `RegisterWindowMessageW` のブロードキャストで足りる。
- **native event filter は `windows_generic_MSG` と `windows_dispatcher_MSG` の
  両方を受ける。** どちらで渡されるかは Qt の内部実装次第 (実測では
  `WM_HOTKEY` は generic)。片方に決め打つと Qt の版が変わった途端に黙って
  効かなくなる。メッセージ種別と ID の照合は必ず行う。
- **`CreateMutexW` の NULL を Secondary 扱いしない。** 「既に起動している」と
  「判定そのものができなかった」は別の事象。後者 (`InstanceRole::Error`) は
  **fail-open にせず、短い modal で理由を出して exit 1 (起動しない)**。
  single instance は常駐 / ホットキー / `--quit` の前提そのもので、判定できない
  まま起動を許すと efs が複数常駐しうる。要求を送れなかった Secondary も
  exit 1 — 送れていないのに成功として終わると `install.ps1` が graceful に
  終わったと誤解して強制終了へ進む。
- **外から終わらせる手段は `efs.exe --quit` だけ。** 閉じる = 隠す なので
  `WM_CLOSE` (`CloseMainWindow`) では終了しない。tray Quit と `--quit` は
  `MainWindow::quitApplication()` の 1 本に合流させ、必ず `saveSettings()` を
  通す。`install.ps1` はまず `--quit`、待ってから最後の手段としてだけ Kill。
- **ホットキーの綴りで空の項を読み飛ばさない** (`Ctrl++Alt+E` 等は invalid)。
  `Qt::SkipEmptyParts` で拾うと、打ち間違えた INI が正しい綴りと同じに解釈される。
  **同じ修飾キーの重複** (`Ctrl+Ctrl+E` / `Ctrl+Control+E` / `Meta+Win+K`) も
  同じ理由で invalid。1 つに畳まない。
- **install は staging + backup で入れ替える。** 旧版を消してからコピーしない。
  ショートカット作成の失敗も含め、途中で失敗したら旧版を復元して非ゼロで
  終わること (partial install を成功物として残さない)。実行中プロセスの照合は
  `<Destination>\efs.exe` との**完全一致**で行う (前方一致は別物を巻き込む)。
- **ショートカットの復元は型まで戻す。** 扱うのは `.lnk` ファイル (Leaf) だけで、
  その path にディレクトリ等が居る場合は既存ショートカットとみなさない
  (退避しない / rollback では取り除く)。復元は取り除いてからコピーし、最後に
  Leaf であることを確認する。`Copy-Item -Force` の上書きに頼ると、相手が
  ディレクトリのときに中へコピーされて `.lnk` が戻らない。
- **rollback の進捗フラグを 1 つにまとめない。** 「旧版を退避した」と「新版を
  配置した」を別に持つ (`previousBackedUp` / `newPlaced`)。1 つにすると、
  退避と配置の**間**で失敗したときに旧版を戻さないまま backup ごと消す経路が
  でき、インストールが丸ごと消える。
- **アイコンの図形は `paintAppIcon()` の 1 箇所だけ。** Explorer 用の
  `efs.ico` はリポジトリへコミットせず、同じ関数を呼ぶ `tools/make_app_icon.cpp`
  がビルド時に生成して `.rc` で埋め込む。**`.ico` を手で差し替えない** —
  実行時の表示と Explorer の表示がずれる。
- **インストール先には何も書かない。** 設定は `%APPDATA%\efs\efs.ini` のまま。
  これを壊すと非管理者で動かなくなる (設定を exe の隣へ置く「ポータブル版」は
  作らない)。インストーラとコード署名は恒久的に作らない。

### P4 review で追加した不変条件

- **`Everything64.dll` の探索先は exe と同階層の絶対パス 1 つだけ。**
  PATH / CWD へフォールバックしない (`LoadLibraryExW` +
  `LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`)。ビルドと
  `package.ps1` の両方が隣へ置く契約なので、無いのは配置の失敗。フォールバック
  を戻すと「開発機では PATH 上の別 DLL を拾って動く」不確定性が復活する。
  回帰テストは `test_everything_api.cpp` の `loadUsesOnlyTheDllBesideTheExe`。
- **接続先の Everything の版を runtime で確認する (fail-closed)。** 版が取れるのは
  IPC が通ってからなので、**最初に成功したクエリの直後に 1 度だけ**
  `major.minor == 1.4` を見て、違えば以後の検索をすべて失敗させる。版の問い合わせも
  IPC なので、**結果行を読み終えた後**に行う (`GetResult*` の間に挟まない)。
  build number は gate にせずログだけ。
- **`--tray` で隠して起動したときは初回クエリを出さない。**
  `restoreOptions(options, InitialDispatch::Deferred)` で値だけ戻し、最初の
  `showAndActivate()` で 1 本だけ発行する。ログオンのたびに見えない状態で
  数百万件の filter-only を走らせない / Everything 本体より先に起動して
  `Everything is not running.` のまま残るのを避ける。
- **選択モードと action の対象範囲を食い違わせない。** 行の action は
  `currentIndex()` の 1 行だけなので `QTableView` は `SingleSelection`。
  一括処理を実際に作るときだけ `ExtendedSelection` へ戻す。
- **設定は「書けたこと」を確認して初めて成功。** `Settings::save()` は
  `sync()` → `QSettings::status()` を見て `bool` を返す。失敗はメッセージ行
  (最優先) + トレイ通知 + `qWarning`。閉じる = 隠す なので、黙って捨てると
  設定が失われ続けることに気づけない。`QSettings` を触るのは `app/Settings.cpp`
  だけという境界は維持する (パスは `settingsFilePath()` 経由で取る)。
- **rollback の完了を確認できたときだけ作業ディレクトリを消す** (`install.ps1`)。
  旧版を戻せた / 戻した中身が必須ファイルを満たす / 退避した `.lnk` を戻せた、の
  すべてを確認する。失敗したら staging / backup / ショートカットの退避を残し、
  手動復旧の path を出して非ゼロで終わる。
- **compile hygiene は `efs_enable_warnings()` の 1 箇所。** 本体・generator・
  tests のすべてに適用する。ツール類だけ警告が緩い状態を作らない。
