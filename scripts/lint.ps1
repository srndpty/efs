# clang-tidy を実行する。
#
# ローカル: pwsh scripts/lint.ps1
# 自動修正: pwsh scripts/lint.ps1 -Fix
# CI      : pwsh scripts/lint.ps1 -BuildDir build/ci
#
# ファイル単位で並列に走らせる (既定は論理コア数、-Jobs で変更)。
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
    [switch]$Bootstrap,
    # 指摘を自動修正する。修正後にもう一度検査し、残った指摘があれば失敗する。
    [switch]$Fix,
    # 同時に走らせる clang-tidy の数。既定は論理コア数。
    [int]$Jobs = 0
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

# clang-tidy は 1 プロセスで 1 TU ずつ順に見るだけなので、既定ではコアが余る。
# ファイル単位に分けて並列に回す (TU 間に依存は無い)。出力はファイル順に
# 並べ直してから出す — 並列のまま流すと指摘が混ざって読めなくなる。
#
# 戻り値: 各ファイルの { File, Output, ExitCode }。
function Invoke-TidyPass {
    param(
        [string]$ExportDir  # 指定すると修正案を YAML に書き出す (--fix はしない)
    )

    # ForEach-Object -Parallel は別 runspace なので、使う値は $using: で渡す。
    $tool = $ClangTidy
    $dir = $BuildDir
    $export = $ExportDir

    $files | ForEach-Object -ThrottleLimit $Jobs -Parallel {
        # native コマンドの stderr で止まらないようにする (診断は stderr に出る)。
        $ErrorActionPreference = 'Continue'
        $file = $_
        $tidyArgs = @('-p', $using:dir, '--quiet')
        if ($using:export) {
            $name = [IO.Path]::GetFileName($file) + '-' + [Guid]::NewGuid().ToString('N') + '.yaml'
            $tidyArgs += "--export-fixes=$(Join-Path $using:export $name)"
        }
        $output = & $using:tool @tidyArgs $file 2>&1 | Out-String
        [pscustomobject]@{ File = $file; Output = $output; ExitCode = $LASTEXITCODE }
    }
}

# 結果を出力して、指摘のあったファイル数を返す。
function Write-TidyResults {
    param($Results)

    $failed = 0
    foreach ($result in ($Results | Sort-Object File)) {
        if ($result.ExitCode -ne 0) { $failed++ }
        $text = $result.Output.Trim()
        if ($text) { Write-Host $text }
    }
    return $failed
}

if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
Write-Host "jobs       : $Jobs"

if (-not $Fix) {
    $failed = Write-TidyResults (Invoke-TidyPass)
    if ($failed -ne 0) {
        throw "clang-tidy が $failed 個のファイルで失敗した。"
    }
    Write-Host "clang-tidy: 指摘なし"
    return
}

# --fix を並列で走らせると、複数の TU から同じヘッダを同時に書き換えうる。
# 代わりに修正案を YAML へ出し、clang-apply-replacements にまとめて適用させる
# (重複の解決はこのツールの仕事)。
$applyTool = Join-Path (Split-Path -Parent $ClangTidy) 'clang-apply-replacements.exe'
if (-not (Test-Path $applyTool)) {
    throw "clang-apply-replacements が $ClangTidy の隣に無い。-Fix には必要。"
}

$fixesDir = Join-Path ([IO.Path]::GetTempPath()) ('efs-tidy-fixes-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixesDir | Out-Null
try {
    Write-Host '--- 修正パス ---'
    [void](Write-TidyResults (Invoke-TidyPass -ExportDir $fixesDir))

    $yaml = @(Get-ChildItem -Path $fixesDir -Filter *.yaml -File | Where-Object { $_.Length -gt 0 })
    if ($yaml.Count -eq 0) {
        Write-Host 'clang-tidy: 指摘なし (修正するものは無かった)'
        return
    }

    & $applyTool $fixesDir
    if ($LASTEXITCODE -ne 0) { throw "clang-apply-replacements が失敗した (終了コード $LASTEXITCODE)。" }
    Write-Host "修正を適用した ($($yaml.Count) ファイル分の修正案)。git diff で確認すること。"
} finally {
    Remove-Item -Recurse -Force $fixesDir -ErrorAction SilentlyContinue
}

# 自動修正できない指摘は残る。適用後の状態でもう一度見る。
Write-Host '--- 検査パス (修正後) ---'
$failed = Write-TidyResults (Invoke-TidyPass)
if ($failed -ne 0) {
    throw "自動修正できない指摘が $failed 個のファイルに残っている。"
}
Write-Host "clang-tidy: 指摘なし"
