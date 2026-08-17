# clang-tidy を実行する。
#
# ローカル: pwsh scripts/lint.ps1
# CI      : pwsh scripts/lint.ps1 -BuildDir build/ci
#
# CI もこのスクリプトを呼ぶ。ワークフロー側に同じ起動を書くと必ず片方だけ
# 古くなる (coverage.ps1 と同じ理由)。
#
# clang-tidy はビルド済みの compile_commands.json を要求する。Visual Studio
# ジェネレータでは生成されないため、Ninja プリセットでビルドしておくこと:
#   cmake --preset ninja-x64-debug
#   cmake --build --preset ninja-x64-debug
#
# **バージョン差に注意。** VS 2022 同梱は 19.1.5 だが CI ランナー同梱は 22.1 系で、
# 新しい方でだけ有効な check がある (modernize-use-integer-sign-comparison は 20 で、
# modernize-avoid-c-style-cast は 22 で追加。どちらも実際に「ローカルは緑・CI だけ
# 失敗」を起こした)。そのため **既定を CI と同じ 22.1 系にし**、リポジトリ直下の
# .tidy22 (venv) を最優先で探す。無ければ -Bootstrap で入れられる。
# 見つかった版が期待と違えば警告する (止めはしない)。

[CmdletBinding()]
param(
    # compile_commands.json のあるビルドディレクトリ。
    [string]$BuildDir,
    # 使う clang-tidy を明示指定する (別のバージョンを試すとき)。
    [string]$ClangTidy,
    # 期待バージョンの clang-tidy を venv に用意する (要ネットワーク)。
    [switch]$Bootstrap
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\ninja-x64-debug' }

# **既定は CI ランナーと同じ 22.1 系にする。** VS 2022 同梱の 19.1.5 を既定に
# していた頃は、新しい check (modernize-avoid-c-style-cast 等) がローカルを
# 素通りして CI だけが落ちていた。判定は major.minor まで — check セットは
# そこで決まり、パッチ版は PyPI に無いこともある (CI の 22.1.3 は無い)。
$expectedVersion = '22.1'
# -Bootstrap で入れる版。PyPI にある実在の版であること。
$pinnedVersion = '22.1.8'
$venvDir = Join-Path $repo '.tidy22'
$venvClangTidy = Join-Path $venvDir 'Scripts\clang-tidy.exe'

if ($Bootstrap) {
    if (-not (Test-Path $venvClangTidy)) {
        Write-Host "clang-tidy $pinnedVersion を $venvDir に用意する"
        & python -m venv $venvDir
        if ($LASTEXITCODE -ne 0) { throw "venv を作れなかった ($venvDir)。" }
        # NVIDIA のツールが書いた extra-index-url は名前が引けずリトライ警告を
        # 延々出すので、この venv の中だけ黙らせる (結果には影響しない)。
        & (Join-Path $venvDir 'Scripts\pip.exe') config --site set global.extra-index-url "" | Out-Null
        & (Join-Path $venvDir 'Scripts\pip.exe') install "clang-tidy==$pinnedVersion"
        if ($LASTEXITCODE -ne 0) { throw "clang-tidy $pinnedVersion を入れられなかった。" }
    }
    Write-Host "用意済み: $venvClangTidy"
}

# 探索順。**リポジトリの venv を最優先**にして、ローカルでも CI と同じ世代の
# clang-tidy が既定で使われるようにする。CI ランナーには venv が無いので、
# そのまま同梱版 (現在 22.1.3) にフォールバックする。
# VS 同梱の VC\Tools\Llvm\bin は 32bit 版で、Qt のヘッダを解析するとアクセス
# 違反 (0xC0000005) で落ちる。必ず x64 版を使う。
if (-not $ClangTidy) {
    $candidates = @(
        $venvClangTidy,
        "$env:VCINSTALLDIR\Tools\Llvm\x64\bin\clang-tidy.exe",
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe',
        'C:\Program Files\LLVM\bin\clang-tidy.exe'
    )
    $ClangTidy = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $ClangTidy) { $ClangTidy = (Get-Command clang-tidy -ErrorAction SilentlyContinue).Source }
}
if (-not $ClangTidy) {
    throw @"
clang-tidy が見つからない。次のいずれかで用意すること:
  pwsh scripts/lint.ps1 -Bootstrap        # $pinnedVersion を .tidy22 に入れる (要ネットワーク)
  pwsh scripts/lint.ps1 -ClangTidy <path> # 既にあるものを指す
"@
}

$compileDb = Join-Path $BuildDir 'compile_commands.json'
if (-not (Test-Path $compileDb)) {
    throw "compile_commands.json が無い: $compileDb`nNinja プリセットで先にビルドすること (cmake --build --preset ninja-x64-debug)。"
}

# lint 対象は src と tools。third_party と tests は対象外 (.clang-tidy の
# HeaderFilterRegex も src 配下のヘッダに限定している)。
# tools\make_app_icon.cpp は製品コードではないが、生成する .ico は出荷物なので
# 同じ基準で検査する。
$files = @(Get-ChildItem -Path (Join-Path $repo 'src'), (Join-Path $repo 'tools') -Recurse -Include *.cpp -File |
    ForEach-Object { $_.FullName })
if ($files.Count -eq 0) {
    throw "src / tools 配下に *.cpp が無い。"
}

$version = (& $ClangTidy --version | Select-String 'LLVM version').ToString().Trim()
Write-Host "clang-tidy : $ClangTidy"
Write-Host "version    : $version (期待: $expectedVersion 系)"
Write-Host "build dir  : $BuildDir"
Write-Host "files      : $($files.Count)"

# 期待バージョンからずれていたら**止めずに警告する**。古ければ CI だけが落ち、
# 新しければローカルだけが落ちる — どちらも「なぜ手元と CI で違うのか」に
# 時間を溶かすので、実行前に言っておく。
$found = [regex]::Match($version, 'LLVM version (\d+)\.(\d+)')
if (-not $found.Success -or "$($found.Groups[1].Value).$($found.Groups[2].Value)" -ne $expectedVersion) {
    Write-Warning @"
clang-tidy が期待バージョン ($expectedVersion 系) ではない。
check セットが違うため、ここが緑でも CI が落ちることがある (逆もある)。
揃えるには: pwsh scripts/lint.ps1 -Bootstrap
"@
}

& $ClangTidy -p $BuildDir --quiet $files
$tidyExit = $LASTEXITCODE

if ($tidyExit -ne 0) {
    throw "clang-tidy が終了コード $tidyExit で失敗した。"
}
Write-Host "clang-tidy: 指摘なし"
