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
# **バージョン差に注意。** ローカル (VS 2022 同梱) と CI ランナー (より新しい
# VS 同梱) では clang-tidy のバージョンが違い、新しい方でだけ有効な check が
# ある。実際に modernize-use-integer-sign-comparison (clang-tidy 20 で追加) が
# ローカル 19.1.5 を素通りして CI だけ落ちた。そのため必ずバージョンを表示し、
# -ClangTidy で CI と同じバージョンを指して再現できるようにしてある。

[CmdletBinding()]
param(
    # compile_commands.json のあるビルドディレクトリ。
    [string]$BuildDir,
    # 使う clang-tidy を明示指定する (CI のバージョンを手元で再現するとき)。
    [string]$ClangTidy
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\ninja-x64-debug' }

# 探索順は CI と同じ。VS 同梱の VC\Tools\Llvm\bin は 32bit 版で、Qt のヘッダを
# 解析するとアクセス違反 (0xC0000005) で落ちる。必ず x64 版を使う。
if (-not $ClangTidy) {
    $candidates = @(
        "$env:VCINSTALLDIR\Tools\Llvm\x64\bin\clang-tidy.exe",
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe',
        'C:\Program Files\LLVM\bin\clang-tidy.exe'
    )
    $ClangTidy = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $ClangTidy) { $ClangTidy = (Get-Command clang-tidy -ErrorAction SilentlyContinue).Source }
}
if (-not $ClangTidy) {
    throw "clang-tidy が見つからない。VS の C++ Clang ツールを入れるか -ClangTidy で指定すること。"
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
Write-Host "version    : $version"
Write-Host "build dir  : $BuildDir"
Write-Host "files      : $($files.Count)"

& $ClangTidy -p $BuildDir --quiet $files
$tidyExit = $LASTEXITCODE

if ($tidyExit -ne 0) {
    throw "clang-tidy が終了コード $tidyExit で失敗した。"
}
Write-Host "clang-tidy: 指摘なし"
