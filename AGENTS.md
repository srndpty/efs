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
| Everything SDK の封じ込め | `Everything.h` を include してよいのは `src/backend/everything/` 配下と、SDK 自体を直接検証する `tests/`・`src/spike/` のみ。SDK の include パスは `efs_core` の **PRIVATE** に置き、リンクしただけでは伝播させない。 |
| スレッド | Everything SDK の呼び出しは検索スレッド 1 本に直列化する。SDK はグローバル状態を持つのでスレッドプール化は禁止。 |
| 純粋関数 | クエリ組み立て・書式整形は Qt 以外に依存しない自由関数にし、単体テストの主戦場にする。 |

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
src/app/                  Qt Widgets の UI
src/spike/                Phase 0 限定の調査用。Phase 1 で削除する
tests/                    QtTest。ctest に登録
docs/                     implementation-plan.md (計画の authority)
scripts/                  補助スクリプト
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
$tidy = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
& $tidy -p build/ninja-x64-debug --quiet (Get-ChildItem -Recurse src -Include *.cpp).FullName

# カバレッジ (要 OpenCppCoverage)
pwsh scripts/coverage.ps1
```

実行時は Qt の DLL に PATH を通す:
`$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"`

### ハマりどころ (確認済み)

- **clang-tidy は x64 版を使う。** VS 同梱の `VC\Tools\Llvm\bin` は 32bit 版で、
  Qt のヘッダを解析するとアクセス違反 (0xC0000005) で落ちる。
  `VC\Tools\Llvm\x64\bin` を使うこと。
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
現在 **Phase 0 (walking skeleton) 完了**。各フェーズの範囲外に手を出さない。

| Phase | 内容 |
|---|---|
| 0 | Qt 導入、Everything SDK の動的ロード、`ext:` + `regex:` の実機検証 (完了) |
| 1 | 検索が動く MVP コア (type-as-you-search、ワーカースレッド、結果テーブル) |
| 2 | 種別フィルタ、Regex トグル、ダークテーマ、ソート、右クリックメニュー = MVP 完成 |
| 3 | 設定永続化、エラー表示、アイコン、`windeployqt` |
| 4 | 将来 backend の受け皿 (着手は任意) |

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
