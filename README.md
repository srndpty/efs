# efs

Everything を検索エンジンとして使う Windows 向けファイル検索 UI。
アプリの UI 表示は英語。x64 のみ。

現在の状態: **Phase 2 (MVP) 完了** — ダークテーマ既定、種別フィルタツールバー、
Regex トグル、ヘッダクリックによる backend ソート、ダブルクリック / Enter で開く、
右クリックメニュー (Open / Show in Explorer / Copy Full Path / Copy Name) まで動く。
設定の永続化・結果行のアイコン・windeployqt は Phase 3。
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

$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"   # windeployqt 導入 (Phase 3) までの暫定
.\build\msvc2022-x64\Debug\efs.exe
```

Visual Studio ジェネレータはマルチ構成のため、configure プリセットは
`msvc2022-x64` の 1 つで、build / test プリセットが Debug / Release に分かれる。
`Everything64.dll` は post-build で各実行ファイルの隣へコピーされる。

clang-tidy は `compile_commands.json` を要求するので、lint だけは Ninja
プリセット (`ninja-x64-debug`) を使う。これは Developer PowerShell が必要。

## ターゲット

| ターゲット | 用途 |
|---|---|
| `efs_core` | 静的ライブラリ。Qt Widgets に依存しないものすべて (core / backend / 検索スレッド / テーブルモデル) |
| `efs` | WIN32 GUI 実行ファイル。`MainWindow` と `main()` だけを持つ |
| `test_*` | QtTest。1 ファイル 1 実行ファイルで `ctest` に登録 (`test_query_builder` / `test_formatting` / `test_path_utils` / `test_result_model` / `test_search_controller` / `test_everything_api` / `test_everything_backend`) |

QtTest は 1 実行ファイルにつき 1 つの `QTEST_MAIN` しか置けないため、テストは
ファイル単位でターゲットを分けている。共通設定は `tests/CMakeLists.txt` の
`efs_add_test()` に寄せてある。

## 開発ツール

| 目的 | 手段 |
|---|---|
| 整形 | `.clang-format` (LLVM ベース、100 桁、4 スペース、関数のみ開き括弧を次行) |
| lint | `.clang-tidy` (bugprone / performance / modernize / readability から実用的なものに限定)。実行は `pwsh scripts/lint.ps1` — CI も同じスクリプトを呼ぶ |
| テスト | QtTest + `ctest` |
| カバレッジ | OpenCppCoverage — `pwsh scripts/coverage.ps1` (要 `winget install OpenCppCoverage.OpenCppCoverage`) |
| pre-commit | `pre-commit install` で有効化。整形と基本的な衛生チェックのみ |
| CI | `.github/workflows/ci.yml` — format / build & test (Debug・Release) / lint / coverage |

CI 上には Everything が存在しないため、IPC を伴うテストは `QSKIP` される。
これは意図した挙動。カバレッジに閾値は設けていない。

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

## MVP の確定事項

- exe 名 `efs`、UI は英語。
- `maxResults` = 5000、`matchPath` = false、`matchCase` = false。
- 種別フィルタの拡張子リストはソースにハードコード。
- Everything が未起動でも自動起動はしない。UI に理由を表示する。
- Everything 1.5 は対象外。1.4 / 1.5 の抽象化は行わない。
