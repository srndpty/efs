# efs

Everything を検索エンジンとして使う Windows 向けファイル検索 UI。
アプリの UI 表示は英語。x64 のみ。

現在の状態: **Phase 4 完了** — MVP (種別フィルタ、Regex トグル、backend ソート、
右クリックメニュー) に加えて、設定の永続化、テーマ切替 (System / Dark / Light)、
backend error の非モーダル表示、Regex の構文警告、結果行のファイル種別アイコン、
`windeployqt` による配布ディレクトリ生成、さらにタスクトレイ常駐 /
グローバルホットキー / 多重起動防止 / `Program Files` への配置スクリプトまで動く。
計画の authority は [docs/implementation-plan.md](./docs/implementation-plan.md)。
この README には実測で確定した事実だけを記録する。
開発上の規約は [AGENTS.md](./AGENTS.md) を参照。

## 使用したツールチェーン (実際のバージョン)

| 項目 | バージョン |
|---|---|
| Qt | **6.8.3** (LTS)、`win64_msvc2022_64`、`aqtinstall` 3.3.0 で `C:\Qt\6.8.3\msvc2022_64` へ導入 |
| Visual Studio | Community 2022 17.14.36518.9 (MSVC 19.44.35217.0、ツールセット 14.44.35207) |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.0.3 |
| Everything | 1.4.1.1022 (`C:\Program Files\Everything\Everything.exe`) |
| Everything SDK | voidtools の `Everything-SDK.zip`。ヘッダと `dll\Everything64.dll` を `third_party/everything-sdk/` へベンダリング |
| clang-format / clang-tidy | 19.1.5 (VS 2022 同梱) |
| pre-commit | 4.2.0 |

Qt の導入手順:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

## ビルドと実行

```powershell
cmake --preset msvc2022-x64
cmake --build --preset msvc2022-x64-debug      # または msvc2022-x64-release
ctest --preset msvc2022-x64-debug

$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"   # 開発ビルドを直接動かす場合
.\build\msvc2022-x64\Debug\efs.exe
```

配布ディレクトリを作れば PATH を通さずに起動できる (下の「配布」を参照)。
どちらの場合も **Everything 本体 (`Everything.exe`) が別途インストールされ、
起動している必要がある。** efs は検索エンジンを持たず、Everything の IPC を
呼ぶだけなので、Everything が動いていないと検索はすべて失敗する
(その場合はステータスバーと検索欄の下に理由が出る)。efs は Everything を
自動起動しない。

Visual Studio ジェネレータはマルチ構成のため、configure プリセットは
`msvc2022-x64` の 1 つで、build / test プリセットが Debug / Release に分かれる。
`Everything64.dll` は post-build で各実行ファイルの隣へコピーされる。

clang-tidy は `compile_commands.json` を要求するので、lint だけは Ninja
プリセット (`ninja-x64-debug`) を使う。これは Developer PowerShell が必要。

### ビルドの並列化

MSBuild は既定で **ターゲット内も、ターゲット間も直列**に走る。放っておくと
32 論理コアの機械で CPU 使用率が 3〜7% しか出ず、フルリビルドに 95 秒かかっていた。
効かせる軸は 2 つあり、**両方入れて初めて速くなる**。

| 軸 | 手段 | 効果 (フルリビルド) |
|---|---|---|
| ターゲット内のファイル | `/MP` (top-level の `add_compile_options`) | 95 s → 38 s |
| ターゲット間 | build プリセットの `"jobs": 0` (= `--parallel`、MSBuild の `/m`) | 38 s → **9 s** |

`jobs: 0` は「ビルドツールの既定 = コア数」を意味する。台数固定の数値を書くと
別の機械や CI で過不足が出るので数値は置かない。プリセット経由で叩く限り
追加のフラグは要らない (素の `cmake --build <dir>` を使うときだけ `--parallel` を
自分で付ける)。

**`/MP` は Visual Studio ジェネレータのときだけ付ける。** Ninja は元から
ファイル単位で並列に走るので二重になるうえ、`/MP` が `compile_commands.json` に
入ると clang-tidy が余計な引数を見ることになる。

## ターゲット

| ターゲット | 用途 |
|---|---|
| `efs_core` | 静的ライブラリ。Qt Widgets に依存しないものすべて (core / backend / 検索スレッド / テーブルモデル) |
| `efs` | WIN32 GUI 実行ファイル。`MainWindow` と `main()` だけを持つ |
| `efs_icongen` | ビルド時に `efs.ico` を生成するだけのツール (下の「アプリアイコン」)。出荷物ではない |
| `test_*` | QtTest。1 ファイル 1 実行ファイルで `ctest` に登録 (`test_query_builder` / `test_formatting` / `test_path_utils` / `test_result_model` / `test_search_controller` / `test_settings` / `test_regex_validation` / `test_icon_cache` / `test_hotkey_spec` / `test_everything_api` / `test_everything_backend`) |

QtTest は 1 実行ファイルにつき 1 つの `QTEST_MAIN` しか置けないため、テストは
ファイル単位でターゲットを分けている。共通設定は `tests/CMakeLists.txt` の
`efs_add_test()` に寄せてある。

## 開発ツール

| 目的 | 手段 |
|---|---|
| 整形 | `.clang-format` (LLVM ベース、100 桁、4 スペース、関数のみ開き括弧を次行) |
| lint | `.clang-tidy` (bugprone / performance / modernize / readability から実用的なものに限定)。実行は `pwsh scripts/lint.ps1` — CI も同じスクリプトを呼ぶ。**既定の clang-tidy は CI と同じ 22.1 系** (`-Bootstrap` で `.tidy22` に用意。版が違えば警告が出る) |
| テスト | QtTest + `ctest` |
| カバレッジ | OpenCppCoverage — `pwsh scripts/coverage.ps1` (要 `winget install OpenCppCoverage.OpenCppCoverage`) |
| pre-commit | `pre-commit install` で有効化。整形と基本的な衛生チェックのみ |
| 配布 | `pwsh scripts/package.ps1` — Release ビルド + `windeployqt` + `Everything64.dll` |
| インストール | `pwsh scripts/install.ps1` — `dist\efs` を `Program Files` へ配置 + ショートカット (管理者権限が要る) |
| CI | `.github/workflows/ci.yml` — format / build & test (Debug・Release) / lint / coverage / package |

CI 上には Everything が存在しないため、IPC を伴うテストは `QSKIP` される。
これは意図した挙動。カバレッジに閾値は設けていない。package ジョブが見るのも
Qt の deployment と必須ファイルの存在だけで、Everything の IPC は要求しない。

## 動作 (Phase 3)

### 設定の永続化

`%APPDATA%\efs\efs.ini` (INI 形式なので目視・手編集できる)。保存は終了時の 1 回だけ。

| 保存するもの | 既定値 |
|---|---|
| テーマ (`system` / `dark` / `light`) | `dark` |
| 種別フィルタ (`all` / `image` / `video` / `audio` / `document` / `directory`) | `all` |
| Regex ON/OFF | `false` |
| ソートキー (`name` / `path` / `size` / `dateModified`) | `name` |
| ソート順 (`asc` / `desc`) | `asc` |
| ウィンドウのジオメトリ・状態・列ヘッダ状態 (列幅 / 列順) | なし (既定サイズ・既定列幅) |

**保存しないもの: 検索文字列、検索履歴、最近使った query、結果行、Everything の状態。**
検索文字列の永続化は search history と意味が混ざるため Phase 3 では行わない。

列挙型は int の生値ではなく安定した文字列で書く (列挙子の順序を入れ替えても
既存の INI が別の意味にならないため)。`settingsVersion` が現行 (`1`) と違う、
値が未知、型が合わない、いずれの場合も**その項目は既定値へ戻る**。ジオメトリが
壊れていれば Qt の `restoreGeometry` が false を返すだけで、既定のサイズが残る。
画面外へ出たウィンドウを補正する独自計算は持たない。

起動時の復元は `SearchController::restoreOptions()` で 4 つの値をまとめて入れ、
**最後に 1 回だけ検索を発行する**。`setKind` / `setRegex` / `setSort` を順に
呼ぶと復元だけで最大 3 本のクエリが飛ぶため。検索欄は起動時に空なので、
実際に飛ぶのは「復元された種別が `all` 以外」のときの filter-only クエリ 1 本だけ
(`all` なら 0 本 = `Ready` 表示)。

### テーマ

ツールバー右端の `Theme` から System / Dark / Light。既定は Dark。
System は起動時と明示選択時に OS の配色 (`QStyleHints::colorScheme()`) を見る
— 実行中の OS 側の変更に追従するところまでは Phase 3 ではやらない。
Windows のタイトルバーも `DwmSetWindowAttribute` で追従する。

stylesheet は切り替えのたびに丸ごと差し替えるので累積しない。
なお `QApplication::setStyle()` は **既に Fusion なら呼び直さない** —
切り替えのたびに呼ぶと全 widget が再 polish され、ステータスバーの表示中
メッセージが消える (実測)。

### エラー表示

「正常に 0 件」と「検索できなかった」を取り違えないようにしてある。

| 状態 | ステータスバー |
|---|---|
| 検索中 | `Searching…` (結果テーブルは空) |
| 正常 | `265 results / 254 ms` — 一致 0 件でも `0 results / 4 ms` |
| 失敗 | `Search failed: Everything is not running.` |

失敗時は検索欄の下にも同じ文言を赤い 1 行で出す。**modal は出さない**
(検索のたびに popup を連打しないため)。同じエラーが続いても表示は 1 つのまま。
正常に完了した次の検索で解除される。失敗したときは結果テーブルを空にする
— 前回の行が残っていると「成功した結果」に見えてしまうため。
DLL のパス等の内部診断は `qWarning` へ流し、UI には出さない。

### 検索中の表示 (in-flight search)

`SearchController` は実際に backend へクエリを発行した時点で `searchStarted` を
**同期に**発火する。UI はこれを受けて結果テーブルを空にし、前回の backend error を
下ろし、ステータスバーを `Searching…` にする (Regex の構文警告は検索の進行とは
無関係なので維持する)。絞り込み条件が無いときはクエリを出さないので、
`searchStarted` ではなく既存の `cleared` 経路で `Ready` に戻る。

理由: Everything の IPC クエリは中断できず、条件によっては 20 秒以上かかる
(下の実測)。これが無いと**検索欄はもう別の条件なのに前の結果が表示され、しかも
ダブルクリックで開けてしまう**。cancel / timeout / fallback は追加せず、
「表示中の結果と現在の query の食い違い」だけを fail-closed にしてある。

この signal は接続順に依存する。`MainWindow` は controller の signal を繋いで
ステータスバーを初期化した**後で** `restoreOptions()` を呼ぶ — 逆にすると
復元時の初回 filter-only クエリだけ `Searching…` を取りこぼす。

### Regex の構文警告

Regex ON のときだけ、`QRegularExpression` で構文を **best-effort に** 見る。
不正なら検索欄の枠を赤くし、ツールチップと赤い 1 行に
`Invalid regular expression at offset 0: quantifier does not follow a repeatable item`
のように出す。Regex OFF に戻すと表示は消える。

**これは advisory であって backend の authority ではない。** 不正と判定しても
ユーザーのパターンは一字も書き換えず、検索は既存経路でそのまま Everything へ渡す。
空白だけのパターンは有効な検索条件として扱う (P2 の whitespace 契約)。
互換性の実測は下の「Phase 3 の検証結果」を参照。

### 結果行のアイコン

Name 列に Windows のファイル種別アイコンを出す。

- **paint / `data()` から実ファイルへ同期 I/O を出さない。** 5,000 行で
  `QFileInfo` / `QFileIconProvider` を実パスに対して呼ぶとディスク I/O で固まる。
- lookup の単位は「ファイル」ではなく**種別**。キーは
  ディレクトリ = `dir:`、拡張子 = `ext:<小文字>` (`archive.tar.gz` → `ext:gz`、
  拡張子なし = `ext:`)。同じ `.txt` が 500 行あっても shell lookup は 1 回。
- lookup はアイコン専用のワーカースレッド 1 本。同じキーの要求は重複させない。
  失敗した lookup も記録するので、未知の拡張子を毎回引き直さない。
- 実ファイルには触れず、`SHGetFileInfoW` + `SHGFI_USEFILEATTRIBUTES` で
  「拡張子 + ファイル属性」だけから引く。`HICON` は `QImage::fromHICON` で
  コピーしたのち必ず `DestroyIcon` する。
- 完了通知 (`IconCache::imagesReady`) は**行番号を持たない**。受け手は viewport を
  塗り直すだけなので、通知が届いた時点でモデルが reset 済みでも行が消えていても
  不整合が起きない。cache を捨てる必要も無い — 未解決キーの placeholder は
  delegate 側の `QPixmap` cache に**入れていない**ので、次の paint で自然に本物へ
  差し替わる。別のキーのアイコンが 1 つ届くたびに解決済みの `QImage`→`QPixmap`
  変換をやり直すことはしない。
- `SHGetFileInfoW` は shell の COM を使うので、worker スレッドでは
  `thread_local` な RAII (`ComScope`) で 1 回だけ `CoInitializeEx` し、
  **成功したときだけ**スレッド終了時に `CoUninitialize` する
  (`RPC_E_CHANGED_MODE` 等の失敗で呼ぶと他所の初期化を剥がしてしまう)。
- この方針の代償として `.exe` などの「ファイル固有アイコン」は再現されず、
  汎用の種別アイコンになる。Phase 3 では高速・安定であることを優先した。
  アイコンは shell の小アイコン (16px) なので、高 DPI では拡大される。

### キーボード

`Ctrl+L` / `Ctrl+F` で検索欄へフォーカス + 全選択。`Esc` は検索文字列が
入っていれば消す (空なら何もしない)。**`Esc` でアプリは終了しない。**
アプリ外から呼び出すグローバルホットキーは Phase 4 で追加した (下記)。

## 動作 (Phase 4)

### タスクトレイ常駐

閉じるボタンは**終了ではなく非表示**。意図的な終了処理は
`MainWindow::quitApplication()` (`saveSettings()` → `QApplication::quit()`) の
1 本だけで、呼ぶのは**トレイメニューの `Quit` と `efs.exe --quit` の IPC の 2 つ**。
`closeEvent` は `saveSettings()` + `hide()` で、終了はしない。
したがって**設定を保存するコード経路は `closeEvent` と `quitApplication()` の 2 本**。
トレイアイコンのクリック / ダブルクリック、メニューの `Show efs`、
グローバルホットキー、2 個目の起動 — 復帰経路はすべて
`MainWindow::showAndActivate()` の 1 本に入る。最小化されていた場合は
`showNormal()` ではなく最小化ビットだけを落とす (最大化していた状態を潰さない)。
復帰したら検索欄にフォーカスして全選択するので、そのまま打ち始められる。

閉じる = 終了でなくなったため、**設定を保存するコード経路は 2 本**ある —
隠す前の `closeEvent` と、意図的な終了の `quitApplication()`
(caller はトレイの `Quit` と `--quit` IPC)。片方だけにすると、その経路で
終わったときに設定が飛ぶ。

トレイが使えない環境 (`QSystemTrayIcon::isSystemTrayAvailable()` が false) では
従来どおり閉じる = 終了。`QApplication::setQuitOnLastWindowClosed(false)` に
しているので、この経路では `closeEvent` から明示的に `quit()` する — でないと
「ウィンドウもトレイも無いのにプロセスだけ残る」状態になり、自力で戻せない。
同じ理由で `--tray` はトレイが使えるときだけ隠して起動する。

### グローバルホットキー

既定 `Ctrl+Alt+E`。INI の `hotkey/show` に `Ctrl+Alt+E` のような**文字列**で
持つ (int の生値にしない)。変更する UI は作っていない — INI を直接編集する。

- 解釈は `app/HotkeySpec.*` の純粋関数。Win32 の `MOD_*` / 仮想キーへの写像は
  `app/GlobalHotkey.cpp` の 1 箇所だけ。この分離で表記のテストが GUI 無しで書ける。
- 受け付けるキーは `A-Z` / `0-9` / `F1-F24` / `Space`。修飾キーの綴りは
  大文字小文字を問わず `Ctrl` (`Control`) / `Alt` / `Shift` / `Meta` (`Win`)。
  出力は常に `Ctrl+Alt+Shift+Meta+<key>` の順に正規化する。
- **空の項は読み飛ばさず invalid にする** (`Ctrl++Alt+E` / `+Ctrl+Alt+E` /
  `Ctrl+Alt++E` / `Ctrl+ +E`)。読み飛ばすと打ち間違えた INI が正しい綴りと
  同じに解釈され、「壊れているのに黙って動く」ことになる。
  **同じ修飾キーの重複も同じ理由で invalid** (`Ctrl+Ctrl+E` / `Ctrl+Control+E` /
  `Meta+Win+K`)。1 つに畳まない。
- **修飾キー無しは拒否する** (OS 全体でそのキーを 1 つ奪ってしまう)。
  空文字は「意図的に無効」として尊重し、解釈できない綴りは既定へ戻す
  (壊れた INI で黙って無効になると「なぜか効かない」になるため)。
- 登録は `RegisterHotKey(nullptr, …)`。**HWND を持たせない** — `WM_HOTKEY` は
  スレッドのメッセージキューへ届くので、ウィンドウが隠れていても・作り直されても
  Qt の native event filter で拾える。`MOD_NOREPEAT` を付けて、押しっぱなしで
  前面化を連打しないようにする。
- native event filter は **`windows_generic_MSG` と `windows_dispatcher_MSG` の
  両方**を受ける。実測 (Qt 6.8.3 / Windows 11) では `WM_HOTKEY` は
  **`windows_generic_MSG`** で届いたが、どちらで渡すかは Qt の内部実装次第なので
  片方に決め打たない (決め打つと Qt の版が変わった途端にホットキーが黙って
  効かなくなる)。`WM_HOTKEY` + ホットキー ID の照合はそのまま維持する。
- **登録に失敗しても起動を止めない。** 他アプリとの衝突は普通に起こるので、
  検索欄の下に `Global hotkey Ctrl+Alt+E is unavailable (already in use by
  another application).` と 1 行出すだけ (modal は出さない)。backend error と
  Regex 警告の方が「今の操作」に近いので、表示の優先度はこれが最も低い。

### アプリアイコン

ウィンドウ / タスクトレイ / タスクバーの表示は実行時に `QPainter` で描く
(`app/ToolbarIcons.cpp` の `paintAppIcon()`)。一方 **Explorer が出すアイコンは
PE のリソース**なので、実行時に描く方式では出せず、ビルド時に `.ico` の実体が要る。

図形の定義を 2 箇所に持たないよう、`.ico` はリポジトリへコミットせず
**同じ `paintAppIcon()` を呼ぶ生成ツール (`tools/make_app_icon.cpp`) が
ビルド時に作る**。CMake が `src/app/efs.rc.in` を configure し、生成した
`efs.ico` を `1 ICON` として exe へ埋め込む。

- 収録サイズは 16 / 32 / 48 / 256。16〜48 は DIB、256 は PNG で持つ
  (256 を DIB にすると数百 KB になる)。
- ICO の書き出しは手書き。**Qt の ICO ハンドラは 1 画像しか書けず**、複数サイズを
  1 ファイルへ収められない。
- ツールは `QPixmap` を作らないので `QGuiApplication` を必要としない
  (`QImage` + `QPainter` だけ)。
- 実測: 生成された `efs.ico` は 4 フレーム (16/32/48/256) を持ち、シェルが
  exe から解決するアイコン (`SHGetFileInfoW`) が実際にこの図形になることを確認した。
  なお .NET の `System.Drawing.Icon` は 256px の PNG フレームを読まない
  (GDI+ の制限) ので、確認には WPF の `IconBitmapDecoder` を使った。

### 多重起動と既存インスタンスへの要求

名前付き mutex (`Local\` 名前空間 = ログオンセッション単位) で 2 個目を検出し、
`RegisterWindowMessageW` で得たメッセージ ID を `HWND_BROADCAST` へ投げてから
自分は終了する。既存インスタンスは native event filter でそれを受ける
(常駐中はウィンドウが隠れているのでブロードキャストが要る)。
**この 1 経路のために Qt Network (`QLocalServer`) は入れない。**

要求は 1 つのメッセージの `wParam` で区別する (メッセージを 2 つ登録しても
失敗点が増えるだけ)。知らない要求コードは消費するだけで、Show や Quit へ
勝手に倒さない。

| 起動 | 既存インスタンスが居る | 居ない |
|---|---|---|
| `efs.exe` | 前面に出して自分は exit 0 | 通常起動 |
| `efs.exe --quit` | 設定を保存して終了させ、自分は exit 0 | 何もせず exit 0 (**UI は出さない**) |

`--quit` は `install.ps1` が更新前に使う graceful exit。**閉じる = 隠すになった
以上、`WM_CLOSE` (`CloseMainWindow`) では終了しない**ので、外から行儀よく
終わらせる手段がこれしかない。tray の Quit と同じ `MainWindow::quitApplication()`
(= `saveSettings()` → `quit()`) に合流させてあり、終了経路は 1 本だけ。

`CreateMutexW` の結果は 3 通りを区別する。

| 結果 | 立場 | 動作 |
|---|---|---|
| 非 NULL + `!ERROR_ALREADY_EXISTS` | Primary | 通常起動。`--quit` は exit 0 |
| 非 NULL + `ERROR_ALREADY_EXISTS` | Secondary | 要求を投げて exit 0。**投げられなければ exit 1** |
| NULL | Error | 理由を modal で 1 つ出して **exit 1 (起動しない)** |

**`NULL` を Secondary 扱いして exit 0 しない。** 「既に起動している」と「判定
そのものができなかった」は別の事象で、後者を成功終了させると、起動したはずの
アプリが理由も無く消えたように見える。`RegisterWindowMessageW` の失敗も同じ
infrastructure error として扱う (mutex は握ったままにする — 手放すと今度は
多重起動まで許してしまう)。送れていないのに成功として終わらないのは
`install.ps1` のためでもある (graceful に終わったと誤解して強制終了へ進む)。

**Error では fail-open しない (起動を続けない)。** single instance は Phase 4 の
前提そのもので、トレイ常駐・グローバルホットキー・`--quit` はどれも「efs は
1 つ」が成り立って初めて意味を持つ。判定できないまま起動を許すと、ホットキーの
奪い合いや設定の上書き合いが起きる。コンソールを持たない GUI アプリなので、
理由は短い modal (`efs cannot start: …`) で出してから終わる。

### インストール

```powershell
pwsh scripts/package.ps1                      # → dist\efs\
pwsh scripts/install.ps1                      # 管理者 PowerShell で実行
pwsh scripts/install.ps1 -NoStartup           # ログオン時の自動起動なし
pwsh scripts/install.ps1 -Uninstall
```

`dist\efs` を `%ProgramFiles%\efs` へコピーし、スタートメニューと (既定では)
スタートアップにショートカットを作る。スタートアップのショートカットだけは
`--tray` を渡し、ログオン時にウィンドウを出さずトレイに常駐させる
(「起動時に隠す」ための設定項目は増やさない)。

- **インストーラ (MSI / MSIX / NSIS / WiX) は作らない。コード署名もしない。**
  個人用ツールであり、未署名インストーラの SmartScreen 警告を避けられる
  この方式で足りる。
- **fail-closed。旧版を消してから上書きしない。** 手順は
  ① staging (`<Destination>.staging-<pid>`) へコピーして必須ファイルを検証
  → ② 旧版を `<Destination>.backup-<pid>` へ退避 → ③ staging を配置先へ move
  → ④ 配置後にもう一度検証 → ⑤ ショートカット作成 → ⑥ 成功して初めて backup を
  捨てる。②以降のどこで失敗しても**旧版を復元**し、非ゼロで終わる。
  **ショートカット作成の失敗も rollback 対象**で、既存の `.lnk` は先に退避して
  おき、元が無かったものは消して戻す。staging と backup は配置先と同じ親に置く
  (別ボリュームだと move がコピーになり、入れ替えの性質が変わる)。
- **ショートカットとして扱うのは `.lnk` ファイル (Leaf) だけ。** その path に
  ディレクトリ等が居座っている場合は「正常な既存ショートカット」とみなさず
  退避もしない (戻すべき中身が無い)。復元は**型を問わず取り除いてからコピー**し、
  最後に Leaf であることを確かめる — `Copy-Item` の上書きに頼ると、相手が
  ディレクトリのときに「その中へコピー」になって `.lnk` が復元されない。
- **進捗のフラグを 1 つにまとめない。** 「旧版を退避した」(`previousBackedUp`) と
  「新版を配置した」(`newPlaced`) は別に持つ。1 つにすると、②と③の**間**で
  失敗したときに「旧版を戻さないまま backup ごと消す」= インストールが丸ごと
  消える経路ができる (fault injection で実測して塞いだ)。初回インストールで
  ③以降に失敗した場合は、旧版が無いので配置途中の中身を削除して終わる。
- 事前確認は「管理者かどうか」ではなく**実際に書けるか**。`-Destination` に
  ユーザー書き込み可能な場所を指せば、昇格なしでスクリプト自体を検証できる。
  ショートカット 2 つはユーザープロファイル配下なので、昇格が要るのは
  `Program Files` へのコピーだけ。
- **設定は `%APPDATA%\efs\efs.ini` のまま。インストール先には何も書かない。**
  だから非管理者でも普通に使えるし、アンインストールしても設定は残る。
  設定を exe の隣に置く「ポータブル版」は作らない。
- 常駐しているのが普通なので、上書きの前に **`efs.exe --quit` で graceful に
  終わってもらう** (10 秒待って駄目なときだけ強制終了し、その旨を warning に
  出す)。`CloseMainWindow()` は Phase 4 では「隠す」でしかなく終了しないので、
  そこへ頼ると**通常経路が毎回強制終了**になり、設定の保存を飛ばしてしまう。
- 実行中プロセスの照合は **`<Destination>\efs.exe` との完全一致** (正規化した
  フルパスで比較)。前方一致にすると `C:\Program Files\efs-old\efs.exe` のような
  別物まで巻き込んで止めてしまう。

Everything 本体は引き続き別途常駐が必要。Everything のトレイアイコンを消したい
場合は Everything 側の `show_tray_icon=0` にする (efs のコードは関与しない)。

### Phase 4 の実機確認

Debug ビルドで確認した (GUI の自動テストは書かない方針なので、ここは手動)。

| 確認 | 結果 |
|---|---|
| 2 個目の起動 | 即座に終了コード 0 で終わり、プロセスは 1 つのまま |
| 2 個目の起動 → 既存インスタンス | 隠れていたウィンドウが前面に戻る |
| `--tray` 起動直後に 2 個目を 5 連射 | 5 回とも exit 0・プロセスは 1 つ・ウィンドウが出る (**activation を取りこぼさない**)。隠す→起動を 5 往復しても同じ |
| 閉じるボタン | プロセスは生き続け、`%APPDATA%\efs\efs.ini` が更新される (`hotkey/show=Ctrl+Alt+E` を含む) |
| `Ctrl+Alt+E` (ウィンドウを隠した状態) | ウィンドウが戻る。`WM_HOTKEY` の eventType は `windows_generic_MSG` |
| `efs.exe --tray` | ウィンドウを出さずに常駐し、`Ctrl+Alt+E` で出せる |
| tray メニューの Quit | 正常終了する (ユーザー確認) |
| exe のアイコン | シェルが exe から引くアイコンが efs のもの (青い丸 + 虫めがね) になる。`.ico` は 16/32/48/256 の 4 フレーム |
| `efs.exe --quit` (常駐中) | exit 0、既存インスタンスが INI を更新してから終了 |
| `efs.exe --quit` (未起動) | exit 0。**UI は出さず**プロセスも残らない |
| 実行中の efs を更新 (`install.ps1`) | `--quit` で graceful に終わり、強制終了の warning は出ない。入れ替え後に作業ディレクトリは残らない |
| ショートカット作成を失敗させる (`efs.lnk` の場所をディレクトリにする) | 旧版が復元され (退避した中身がそのまま戻る)、非ゼロで終わる。退避した `.lnk` も戻る |
| 同上で、別の配置先への初回インストール中に Startup 側を失敗させる | 退避した `.lnk` が **Leaf として**復元され、リンク先も元のまま。ディレクトリは除去、配置途中の新しい配置先は削除、exit 1 |
| fault injection: 旧版を退避した直後・新版を配置する前に失敗 | 旧版が復元される (marker 込み)、exit 1、作業ディレクトリ残らず |
| fault injection: 新版を配置した直後に失敗 (旧版あり) | 新版を捨てて旧版を復元、exit 1 |
| fault injection: 新版を配置した直後に失敗 (初回 = 旧版なし) | 配置途中の中身を削除して exit 1 (**partial を残さない**) |
| 不完全な `-Source` | 配置先に触れる前に失敗。旧版は無傷 |
| 配置先と前方一致する別ディレクトリ (`...-other\efs.exe`) から起動中 | `install.ps1` はそれを止めない (完全一致で照合) |
| `install.ps1 -Uninstall` | 配置先とショートカットが消え、INI は残る |

tray メニューの Quit は通知領域のクリックが要るため自動化しておらず、
ユーザーが手動で確認した。実体は `--quit` と同じ
`MainWindow::quitApplication()` の 1 本。

## 配布

```powershell
pwsh scripts/package.ps1                       # → dist\efs\
pwsh scripts/package.ps1 -BuildDir build/ci -Config Release   # CI
```

手順は Release ビルド → `efs.exe` を配置 → `windeployqt --release` →
`Everything64.dll` を明示コピー → 必須ファイルの存在確認。

- **fail-closed。** 前回の出力を消してから始め、各コマンドの終了コードと
  `efs.exe` / `Everything64.dll` / `Qt6Core.dll` / `Qt6Gui.dll` /
  `Qt6Widgets.dll` / `platforms\qwindows.dll` の存在を確認する。途中で失敗したら
  作りかけの `dist` を消して非ゼロで終わる (古い package を成功物として残さない)。
- **必要ファイルの authority は `windeployqt`。** 上の一覧は「これが無ければ
  確実に起動しない」ものだけで、網羅リストではない。
- **`windeployqt` は `Everything64.dll` を知らない。** Qt の依存しか見ないので、
  アプリが実行時に `LoadLibraryW` する DLL は明示コピーが要る。
- Qt のパスはスクリプトに固定しない。`-WindeployQt` → `-QtBin` →
  ビルドディレクトリの `CMakeCache.txt` の `CMAKE_PREFIX_PATH` / `Qt6_DIR` →
  `QT_ROOT_DIR` / `QTDIR` → `PATH` の順に解決し、見つからなければ**探した場所を
  全部並べて**失敗する。

起動は `dist\efs\efs.exe` をそのまま実行するだけ。Qt を `PATH` に通す必要はない
(実測: Qt の入っていない `PATH` で起動を確認済み)。**Everything 本体は別途
起動している必要がある。**

## Phase 0 の検証結果

### `ext:` と `regex:` の併用 — PASS

計画 6.2 が挙げていたリスクは「`Everything_SetRegex(TRUE)` は検索文字列**全体**を
正規表現として解釈するため、`ext:` 前置詞と併用できないのではないか」というもの。
Everything 1.4.1.1022 に対する実測で、その懸念の両面が確認された。

- **インライン修飾子 `regex:` は `ext:` と併用できる。**
  `ext:jpg regex:^a00` は 122 件を返し、その全行が `.jpg` で終わり**かつ**
  `^a00` に一致した。陰性対照 `ext:jpg regex:^ZZQXNOMATCH` は 0 件。
  2 つの項は AND 結合されている。
- **グローバルフラグは使えない。** `Everything_SetRegex(TRUE)` で
  `ext:jpg ^IMG_\d+` を投げると 0 件。懸念どおり `ext:` 前置詞がパターンの
  一部として飲み込まれている。

**決定: `EverythingQueryBuilder` は `Everything_SetRegex(FALSE)` を維持したまま、
`"<拡張子前置詞> regex:<パターン>"` を組み立てる。** 計画に代替案として
書かれていた「拡張子を正規表現に畳み込むフォールバック」は不要であり、
実装してはならない。

### その他の観測

- IPC は起動中のクライアントに届いている: `Everything_GetMajorVersion` 等が
  `1.4.1.1022` を返す。
- 打ち切りの扱いは計画の前提どおり。この機で `ext:jpg` は
  `GetNumResults=5000` (`SetMax` の上限) に対し `GetTotResults=1288529` を返す。
  `truncated = totalMatches > rows.size()` で判定できる。
- `SetMatchCase(FALSE)` で大文字小文字を無視する: `regex:^IMG_\d+` が
  `img_0193` に一致した。
- DLL 欠如時の診断が機能する: `Everything64.dll` の無いディレクトリから
  実行すると、探索した場所と win32 エラー 126 を報告する (起動失敗にはならない)。

### 未検証

- `ERROR_IPC` の経路 (Everything 未起動時の「Everything is not running.」表示) は
  実装済みだが未実行。検証には起動中の `Everything.exe` を落とす必要があるため
  見送った。

## Phase 1 の実測

- 通常の検索 (`everything`、172 件) は 110〜120 ms 前後。
- 全ドライブ規模の検索 (`e`、3,989,130 件) は 469 ms で、
  ステータスバーに `3,989,130 results (showing first 5,000) / 469 ms` と出る。
  打ち切りは仕様どおり 5,000 行。
- 高速入力中も UI は固まらない (デバウンス 120 ms + 検索スレッド)。
  ただし **重いクエリの実行中にウィンドウを閉じると、そのクエリが終わるまで
  終了が待たされる**。Everything の IPC クエリは中断できないため意図した挙動
  (計画 4)。実測では 1 秒未満。

## Phase 2 の検証結果

### 空白を含む Regex — `regex:"<パターン>"` と引用する (実測で確定)

Phase 1 から持ち越していた「Everything の search-term parser が空白を AND 区切り
として扱うため、`^IMG \d+` のようなパターンが 2 項に割れるのではないか」という
懸念は、**そのとおりだった**。

検証は `tmp/` の使い捨て実行ファイルで行った (恒久ターゲットとしては残していない)。
「たまたま 0 件」を PASS にしないよう、正解集合が人手で確定できるプローブファイルを
`tmp/spike_data/` に置いて実測した:

```
efsspike alpha beta.txt   efsspike alpha.txt   efsspike alpha.dat   efsspikealpha.txt
```

`^efsspike alpha` の正解は前 3 者 (3 件)。純粋 Regex としての正解集合は診断目的で
`Everything_SetRegex(TRUE)` を使って確認した (production は `FALSE` のまま)。

| # | 検索文字列 (`Everything_SetRegex(FALSE)`) | 件数 | 判定 |
|---|---|---|---|
| A1 | `regex:^efsspike` (空白なし・対照) | 4 | 期待どおり |
| B1 | `^efsspike alpha` (`SetRegex(TRUE)`・正解の基準) | **3** | — |
| C1 | `regex:^efsspike alpha` (引用なし = Phase 1 の実装) | **4** | **NG。`regex:^efsspike` AND `alpha` の 2 項に割れている** |
| C2 | `regex:"^efsspike alpha"` | **3** | **OK。正解と完全一致** |
| C3 | `"regex:^efsspike alpha"` (項全体を引用) | 0 | NG。修飾子ごと文字列扱いになる |
| C4 | `regex:^efsspike\salpha` | 3 | 一致するが、ユーザーに `\s` を打たせる必要がある |
| C5 | `regex:^efsspike[ ]alpha` | **0** | NG。`[` と `]` の間で項が割れる |
| C6 | `regex:^efsspike\x20alpha` | 3 | 一致するが C4 と同じ理由で不採用 |
| C8 | `regex:^efsspike\ alpha` | 0 | NG |
| D1 / D2 | 空白 2 個 (`^efsspike alpha beta`) 引用なし / あり | 1 / 1 | この例では差が出ない |
| E1–E5 | 陰性対照 (一致しないパターン) | すべて 0 | 期待どおり |
| F1 | `ext:txt regex:^efsspike alpha` | 3 | NG (割れる) |
| F2 | `ext:txt regex:"^efsspike alpha"` | **2** | **OK。`ext:` との AND が保たれる** |
| G1 / G2 | `\.` を含むパターン 引用なし / あり | 0 / 2 | 引用ありのみ正しい |
| H1 | パターン内に生の TAB | 4 | **TAB も項の区切りとして解釈される** |

引用の適用範囲も実測した:

- `regex:"^efsspike"` — 空白が無くても引用してよい (引用なしと同じ結果)。
  したがって**条件分岐せず無条件に囲む**。
- `folder: regex:"^efsspikedir alpha"` — `folder:` との併用も可。
- 引用の内側で `|` の選択、`{n}` の量指定子、`\.` のエスケープ、`;`、先頭の空白、
  TAB がすべてリテラルとして生きる (`regex:"^efsspike<TAB>*alpha"` は
  `efsspikealpha.txt` に一致した = 引用が TAB を守っている)。
- **不正な正規表現 (`regex:"^efsspike("` 等) はエラーにならず 0 件を返す。**
  `Everything_GetLastError()` は `EVERYTHING_OK` のまま。したがって
  「正規表現が壊れている」ことを backend から検出する手段は無い。

**確定仕様: `buildQueryString()` は Regex ON のとき `regex:"<入力>"` を出す。**
パターン内部のエスケープは補わない。パターン自体が `"` を含む場合は項が壊れるが、
NTFS のファイル名に `"` は入れられないので実用上意味が無く、実測でも crash はしない。
TAB もファイル名に入れられない (プローブ作成時に win32 が拒否した) ため、
「TAB 入り Regex」は引用の内側に入ることだけを保証すれば十分。

**Regex ON ではユーザーのパターンを一字も変えない (前後の空白を含む)。** 引用の
内側では先頭/末尾の空白も TAB も意味を持つ (上の I9 / I11)。したがって:

| | Regex OFF | Regex ON |
|---|---|---|
| 前後の空白 | trim する (Everything の項区切りでしかない) | **保持する** |
| 「テキスト条件なし」の判定 | trim して空 | **空文字のときだけ** |
| 空白だけの入力 | 検索しない | **有効な条件** (`regex:" "` = 名前に空白を含む) |

`buildQueryString()` と `core/SearchTypes.h` の `hasSearchConstraint()` は同じ契約で
なければならない (片方だけ変えると「条件はあるのにクエリが空」になる)。両者を
同じ入力で突き合わせる `regexWhitespaceContractMatchesHasSearchConstraint` で固定した。

### FileKind + OR 式の演算子優先順位 — 種別は hard constraint にする

Everything は演算子の優先順位を設定で変更できるため、`ext:... a|b` のまま渡すと
種別項が OR の片側からしか掛からない懸念があった。プローブ
(`efsor alpha.jpg` / `efsor alphabeta.jpg` / `efsor gamma.png` / `efsor beta.txt` /
`efsor delta.mp3`) で実測した結果:

- **現在の既定設定では `|` が空白 (AND) より強く結合しており、懸念した破綻は
  起きていない。** `ext:<image> alpha|beta` は `efsor beta.txt` を返さない。
- **`( )` はグルーピングではない。** `ext:<image> (alpha|beta)` は括弧を含む名前を
  探しに行き、プローブに 1 件も当たらなかった。Everything 1.4 のグルーピングは `< >`。
- `< >` で囲んでも結果は変わらない。単語 / AND / OR / 否定 `!` / 引用 `"..."` /
  ワイルドカード `*` `?` / inline `regex:` / 既に `<>` を含む入力 / 不均衡な `<` `>` /
  他の修飾子 (`path:`) の **13 ケースすべてで、raw と `<>` 版の totalMatches と
  ヒット内容が完全一致**した (`folder:` 側の 2 ケースも同じ)。

**決定: 種別項があり Regex OFF のときは、ユーザー式全体を `<...>` で囲む。**
現在の設定では no-op だが、優先順位設定に依存せず「ツールバーで選んだ種別は必ず
効く」ことを保証できる。実測で無害と確認できた範囲だけの最小の変更で、
パーサやフォールバックは作らない。Regex 項は引用済みの 1 項なので囲まない。
`FileKind::All` のときは種別項が無いので囲まない。

回帰テストは `tests/test_query_builder.cpp` の `p2RegressionRegexWithSpaceIsQuoted` と
`tests/test_everything_backend.cpp` の `regexWithSpaceIsOneTerm` (実機・陽性 + 陰性対照)。
後者は引用を外すと実際に FAIL することを確認済み。

### `ResultRow::path` の形 (フルパス組み立ての根拠)

| 対象 | `path` | `name` |
|---|---|---|
| 通常のファイル | `C:\dev\soft\efs\tmp\spike_data` | `efsspike alpha.txt` |
| **ドライブ直下のファイル** | `C:` (末尾に `\` が付かない) | `pagefile.sys` |
| **ドライブそのもの** | (空文字) | `C:` |

裸の `C:` はドライブ相対パスという別の意味になるため、`core/PathUtils.h` の
`fullPath()` がこの 3 形を吸収する。

### fast sort の実測

Everything の設定は変更していない。同一クエリを 7 回ずつ実行 (ウォームアップ 1 回は除外):

| クエリ | ソート | totalMatches | 返却行 | min | median | max |
|---|---|---|---|---|---|---|
| `e` | Name Asc | 3,991,512 | 5,000 | 235 | 396 | 433 ms |
| `e` | Path Asc | 3,991,512 | 5,000 | 239 | 252 | 412 ms |
| `e` | Size Desc | 3,991,512 | 5,000 | 268 | 389 | 451 ms |
| `e` | Date Modified Desc | 3,991,512 | 5,000 | 216 | 283 | 344 ms |
| `a` | Name Asc | 3,128,648 | 5,000 | 284 | 362 | 454 ms |
| `a` | Path Asc | 3,128,648 | 5,000 | 251 | 352 | 443 ms |
| `a` | Size Desc | 3,128,648 | 5,000 | 268 | 361 | 429 ms |
| `a` | Date Modified Desc | 3,128,649 | 5,000 | 228 | 277 | 366 ms |

**サイズ / 日時ソートが名前ソートより遅いという事象は観測されなかった。**
計画 6.3 が懸念していた "fast sort 無効時の劣化" はこの環境では起きていない。
フォールバックや閾値は追加していない。

filter-only クエリ (テキスト空 + 種別) も測った:

| 種別 | totalMatches | 初回 | warm min / median / max |
|---|---|---|---|
| Image | 2,410,502 | 265 | 215 / 374 / 475 ms |
| Video | 101,812 | 229 | 183 / 332 / 400 ms |
| Audio | 12,302 | 825 | 207 / 281 / 399 ms |
| Document | 154,001 | 1,115 | 654 / 1,137 / 1,256 ms |
| Directory | 748,871 | 384 | 159 / 181 / 376 ms |

拡張子が 14 個ある Document が最も重く、1 秒を超えることがある。UI はブロックしない
(検索スレッド + stale 破棄) が、ツールバーを押してから結果が出るまでに 1 秒前後
かかる場合がある。**Phase 2 では対処しない** (実測値の提示にとどめる)。

## Phase 3 の検証結果

### Regex 互換性 probe — `QRegularExpression` を advisory に使ってよい (実測で確定)

Everything 1.4 は不正な正規表現を error として返さず **0 件**を返す (P2 で確定)。
そのため「0 件 = invalid」とは判定できず、UI の構文警告はローカルの
`QRegularExpression` に頼るしかない。両者のエンジンが同一である保証は無いので、
実装前に陽性対照ファイル (`efsp3probe_abc.txt` / `efsp3probe_a00.txt` /
`efsp3probe alpha.txt` / `efsp3probe_a.b.txt` / `efsp3probe_(paren).txt` /
`efsp3probe_日本語.txt`) を置いて 28 パターンを突き合わせた。

Everything 側は `regex:"<パターン>"` (= 製品と同じ経路) で実行し、
`totalMatches` を見た。

| パターン | Qt | Everything |
|---|---|---|
| `efsp3probe` | valid | 6 |
| `^efsp3probe_abc\.txt$` | valid | 1 |
| `efsp3probe_abc\|efsp3probe_a00` | valid | 2 |
| `efsp3probe_(abc\|a00)` | valid | 2 |
| `efsp3probe_[a-c]+\.txt` | valid | 1 |
| `efsp3probe_a\d+` | valid | 1 |
| `efsp3probe_a{1,3}0` | valid | 1 |
| `efsp3probe_a\.b` | valid | 1 |
| `efsp3probe alpha` (空白) | valid | 1 |
| `efsp3probe\salpha` | valid | 1 |
| `efsp3probe_\(paren\)` | valid | 1 |
| `efsp3probe_日本語` (Unicode) | valid | 1 |
| `efsp3probe_(?=abc)` (先読み) | valid | 1 |
| `efsp3probe_abc(?!zzz)` (否定先読み) | valid | 1 |
| `(?<=efsp3probe_)abc` (後読み) | valid | 1 |
| `efsp3probe_(?:abc\|a00)` | valid | 2 |
| `(?i)EFSP3PROBE_ABC` | valid | 1 |
| `efsp3probe_a*bc` / `efsp3probe_a.b` | valid | 1 |
| **`efsp3probe_zzzznomatch`** (陰性対照) | **valid** | **0** |
| `efsp3probe_(` | INVALID (offset 12) | 0 |
| `efsp3probe_[` | INVALID (offset 12) | 0 |
| `efsp3probe_\` | INVALID (offset 12) | 0 |
| `efsp3probe_abc)` | INVALID (offset 14) | 0 |
| `efsp3probe_a{3,1}` | INVALID (offset 16) | 0 |
| `*efsp3probe` | INVALID (offset 0) | 0 |
| `efsp3probe_a{1,` | valid (リテラル解釈) | 0 |
| `efsp3probe_+` | valid | 5 |

読み取れたこと:

- **日常的な構文で非互換は見つからなかった。** 先読み / 後読み / `(?i)` /
  `\s` / Unicode まで、Everything 1.4 は Qt と同じように解釈している。
- **「Qt が invalid と言うのに Everything では動く」ケースは 1 つも無かった。**
  誤って赤くする方向の事故が起きないので、advisory として使ってよい。
- 陰性対照が示すとおり **0 件は invalid の証拠にならない**。`efsp3probe_a{1,`
  は Qt でも valid (PCRE は `{1,` をリテラルとみなす) で、0 件なのは単に
  一致するファイルが無いから。判定の根拠に件数を使ってはならない。

この probe は目的を達成したので、調査用の一時ターゲットは削除した。
固定した内容は `tests/test_regex_validation.cpp` の corpus に移してある。

### Regex 検索の実測 — Everything 側は線形走査で数十秒かかることがある

`Image` フィルタ (2,410,588 件) + Regex ON で `a12` を検索したところ
**3,653 results / 24,572 ms**。`ext:` だけの filter-only が 311ms で返るのに対し、
`regex:` が付くと桁が変わる。Everything の regex はインデックスを使えず
全走査になるため。

この間も **UI はブロックしない** (`Responding=True` を確認済み) が、
Everything の IPC クエリは中断できない契約なので、実行中のクエリが終わるまで
新しい検索は始まらない。

**当初はこの間、直前に成功した結果がそのまま表示・操作できてしまっていた**
(検索欄はもう別の条件なのに、古い行をダブルクリックで開けた)。P3 review で
`searchStarted` を入れ、クエリ発行の時点で結果を空にして `Searching…` を出すよう
にした。実測 (配布版・Qt を含まない `PATH`):

| 時点 | 表示 |
|---|---|
| `a12` 入力直後 (1.2 秒後) | 行は空、`Searching…`、`Responding=True` |
| 完了後 | `3,653 results / 7,668 ms`、全行が `a12` を含む (= 最新 query の結果だけ) |

cancel / timeout / fallback は**追加していない**。Everything 側の走査時間そのものは
変わらない (実測 7.7〜24.5 秒。ばらつきはキャッシュ状態による)。

### 配布ディレクトリの実測

`windeployqt --release` の出力は **51 ファイル / 54.3 MiB**。内訳の主なもの:

```
efs.exe  Everything64.dll
Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6Network.dll  Qt6Svg.dll
platforms\qwindows.dll   styles\qmodernwindowsstyle.dll
imageformats\{qgif,qico,qjpeg,qsvg}.dll   iconengines\qsvgicon.dll
tls\{qcertonlybackend,qschannelbackend}.dll
networkinformation\qnetworklistmanager.dll   generic\qtuiotouchplugin.dll
D3Dcompiler_47.dll  opengl32sw.dll  translations\qt_*.qm (31 個)
```

`Qt6Network` / `Qt6Svg` / `tls` は efs が直接使っていないが、`windeployqt` が
必要と判断したものはそのまま入れる (**必要ファイルの authority は
`windeployqt`**。`--no-*` で絞り込む最適化はしていない)。

検証したこと:

- Qt を含まない `PATH`
  (`C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem` のみ) で
  `dist\efs\efs.exe` が起動し、実検索・テーマ切替・終了まで動く。
- package のコピーから `Everything64.dll` だけを外した状態でも
  **プロセスは即死しない** (暗黙リンクではなく `LoadLibraryW` の動的ロードなので)。
  検索すると `Search failed: Everything SDK is not available.` が
  ステータスバーと検索欄の下に出る。検証後は clean package を再生成した。
- `windeployqt` が見つからない / ビルドが失敗した場合は非ゼロで終わり、
  作りかけの `dist` は残らない (実測で確認)。

### 設定の永続化の実測

Light テーマ + `Image` フィルタ + Regex ON にし、ウィンドウを (180,120) /
880x520 へ動かしてから終了 → 再起動で、テーマ・タイトルバーの明暗・
ウィンドウ位置とサイズ・列幅・種別・Regex がすべて復元された。
**検索欄は空**で、`Image` の filter-only クエリが 1 本だけ飛んだ
(`2,410,588 results (showing first 5,000) / 311 ms`)。

保存された INI:

```ini
[General]
settingsVersion=1
[appearance]
theme=light
[search]
kind=image
regex=true
sortKey=name
sortOrder=asc
[window]
geometry=@ByteArray(...)
state=@ByteArray(...)
headerState=@ByteArray(...)
```

## MVP の確定事項

- exe 名 `efs`、UI は英語。
- `maxResults` = 5000、`matchPath` = false、`matchCase` = false。
- 種別フィルタの拡張子リストはソースにハードコード。
- Everything が未起動でも自動起動はしない。UI に理由を表示する。
- Everything 1.5 は対象外。1.4 / 1.5 の抽象化は行わない。
