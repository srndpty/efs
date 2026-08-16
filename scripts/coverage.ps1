# OpenCppCoverage でテストのカバレッジを取得する。
#
# 前提: winget install OpenCppCoverage.OpenCppCoverage
#       (または choco install opencppcoverage)
#
# MSVC には gcov 相当が無く、OpenCppCoverage は PDB を読むため Debug ビルドが必要。
#
# ローカル: pwsh scripts/coverage.ps1
# CI      : pwsh scripts/coverage.ps1 -BuildDir build/ci -QtBin "$env:QT_ROOT_DIR\bin"
#
# CI もこのスクリプトを呼ぶ。ワークフロー側に同じ OpenCppCoverage 起動を書くと
# 必ず片方だけ古くなる (実際に実行ファイルのパスがずれて CI が落ちた)。
#
# fail-closed: 前回の出力を消してから実行し、OpenCppCoverage の終了コードと
# 生成物の存在を両方確認する。どこかで失敗したら必ず非ゼロで終わる。
# (古い出力が残っていて成功したように見える、が一番危ない)

[CmdletBinding()]
param(
    # ビルドディレクトリ。この下から efs_tests.exe を探す。
    [string]$BuildDir,
    # マルチ構成ジェネレータ (Visual Studio) で使う構成名。
    [string]$Config = 'Debug',
    [string]$OutDir,
    # Qt の DLL がある場所。存在しなければ PATH に既にあるものとみなす。
    [string]$QtBin = 'C:\Qt\6.8.3\msvc2022_64\bin'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\msvc2022-x64' }
if (-not $OutDir)   { $OutDir   = Join-Path $repo 'build\coverage' }

$htmlDir   = Join-Path $OutDir 'html'
$htmlIdx   = Join-Path $htmlDir 'index.html'
$cobertura = Join-Path $OutDir 'cobertura.xml'

# PATH に無いことが多い (winget 版は PATH を通さない) ので既定の場所も見る。
$toolPath = (Get-Command OpenCppCoverage -ErrorAction SilentlyContinue).Source
if (-not $toolPath) {
    $fallback = 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe'
    if (Test-Path $fallback) {
        $toolPath = $fallback
    } else {
        throw "OpenCppCoverage が見つからない。winget install OpenCppCoverage.OpenCppCoverage を実行すること。"
    }
}

if (-not (Test-Path $BuildDir)) {
    throw "ビルドディレクトリが無い: $BuildDir"
}

# テストは 1 ファイル 1 実行ファイル (tests/CMakeLists.txt 参照) なので全部集める。
# 出力先はジェネレータで変わる (Ninja は build ルート、Visual Studio は
# build/<Config>)。決め打ちにせず探す。
$candidates = @(Get-ChildItem -Path $BuildDir -Filter 'test_*.exe' -Recurse -File)
if ($candidates.Count -eq 0) {
    throw "$BuildDir 配下に test_*.exe が無い。先にビルドすること。"
}
# マルチ構成ジェネレータでは Debug と Release の両方が見つかりうる。指定された
# 構成のものだけを使う (両方測ると同じテストを二重に数えてしまう)。
$configured = @($candidates | Where-Object { $_.FullName -match "\\$([regex]::Escape($Config))\\" })
$testExes = if ($configured.Count -gt 0) { $configured } else { $candidates }  # 単一構成ジェネレータ

# 前回の出力を消す。残骸を今回の結果と誤認しないため。
if (Test-Path $OutDir) {
    Remove-Item -Recurse -Force $OutDir
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

if (Test-Path $QtBin) { $env:PATH = "$QtBin;$env:PATH" }
# QtTest は標準出力がコンソールでないと結果を出さない (AGENTS.md 参照)。
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'

Write-Host "OpenCppCoverage : $toolPath"
Write-Host "test executables: $($testExes.Count)"

# 実行ファイルごとに測定し、最後にまとめて 1 つのレポートへマージする。
# third_party とテスト自身は測定対象から外し、src 配下だけを見る。
$binDir = Join-Path $OutDir 'binary'
New-Item -ItemType Directory -Force $binDir | Out-Null

$inputs = @()
foreach ($exe in $testExes) {
    $binary = Join-Path $binDir "$($exe.BaseName).cov"
    Write-Host "  measuring $($exe.Name)"
    & $toolPath `
        --sources "$repo\src" `
        --excluded_sources "$repo\third_party" `
        --excluded_sources "$repo\tests" `
        --export_type "binary:$binary" `
        -- $exe.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "$($exe.Name) の測定が終了コード $LASTEXITCODE で失敗した (テスト自体の失敗を含む)。"
    }
    if (-not (Test-Path $binary)) {
        throw "$($exe.Name) の中間カバレッジが生成されていない: $binary"
    }
    $inputs += @('--input_coverage', $binary)
}

& $toolPath `
    --sources "$repo\src" `
    --excluded_sources "$repo\third_party" `
    --excluded_sources "$repo\tests" `
    --export_type "html:$htmlDir" `
    --export_type "cobertura:$cobertura" `
    @inputs
$toolExit = $LASTEXITCODE

if ($toolExit -ne 0) {
    throw "カバレッジのマージが終了コード $toolExit で失敗した。"
}

$missing = @($cobertura, $htmlIdx) | Where-Object { -not (Test-Path $_) }
if ($missing) {
    throw "カバレッジ出力が生成されていない: $($missing -join ', ')"
}

# cobertura の line-rate を全体の指標として表示する。
[xml]$xml = Get-Content $cobertura
$rate = [double]$xml.coverage.'line-rate'
$covered = [int]$xml.coverage.'lines-covered'
$valid = [int]$xml.coverage.'lines-valid'
Write-Host ("overall line coverage: {0:P2} ({1}/{2} lines)" -f $rate, $covered, $valid)
Write-Host "cobertura : $cobertura"
Write-Host "html      : $htmlIdx"
