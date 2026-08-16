# efs

Everything を検索エンジンとして使う Windows 向けファイル検索 UI。
アプリの UI 表示は英語。x64 のみ。

現在の状態: **Phase 1 (MVP コア) 完了** — 検索欄に入力すると Everything の結果が
テーブルに出る。種別フィルタ / Regex トグル / テーマ / ソート UI は Phase 2。
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
| `test_*` | QtTest。1 ファイル 1 実行ファイルで `ctest` に登録 (`test_query_builder` / `test_formatting` / `test_result_model` / `test_search_controller` / `test_everything_api` / `test_everything_backend`) |

QtTest は 1 実行ファイルにつき 1 つの `QTEST_MAIN` しか置けないため、テストは
ファイル単位でターゲットを分けている。共通設定は `tests/CMakeLists.txt` の
`efs_add_test()` に寄せてある。

## 開発ツール

| 目的 | 手段 |
|---|---|
| 整形 | `.clang-format` (LLVM ベース、100 桁、4 スペース、関数のみ開き括弧を次行) |
| lint | `.clang-tidy` (bugprone / performance / modernize / readability から実用的なものに限定) |
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

## MVP の確定事項

- exe 名 `efs`、UI は英語。
- `maxResults` = 5000、`matchPath` = false、`matchCase` = false。
- 種別フィルタの拡張子リストはソースにハードコード。
- Everything が未起動でも自動起動はしない。UI に理由を表示する。
- Everything 1.5 は対象外。1.4 / 1.5 の抽象化は行わない。
