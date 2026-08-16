# OpenCppCoverage でテストのカバレッジを取得する。
#
# 前提: winget install OpenCppCoverage.OpenCppCoverage
#       (または choco install opencppcoverage)
#
# MSVC には gcov 相当が無く、OpenCppCoverage は PDB を読むため Debug ビルドが必要。
# 使い方: pwsh scripts/coverage.ps1
#
# fail-closed: 前回の出力を消してから実行し、OpenCppCoverage の終了コードと
# 生成物の存在を両方確認する。どこかで失敗したら必ず非ゼロで終わる。
# (古い出力が残っていて成功したように見える、が一番危ない)

$ErrorActionPreference = 'Stop'

$repo     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'build\msvc2022-x64'
$testExe  = Join-Path $buildDir 'Debug\efs_tests.exe'
$outDir   = Join-Path $repo 'build\coverage'
$htmlIdx  = Join-Path $outDir 'html\index.html'
$cobertura = Join-Path $outDir 'cobertura.xml'
$qtBin    = 'C:\Qt\6.8.3\msvc2022_64\bin'

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

if (-not (Test-Path $testExe)) {
    throw "$testExe が無い。先に cmake --build --preset msvc2022-x64-debug を実行すること。"
}

# 前回の出力を消す。残骸を今回の結果と誤認しないため。
if (Test-Path $outDir) {
    Remove-Item -Recurse -Force $outDir
}
New-Item -ItemType Directory -Force $outDir | Out-Null

$env:PATH = "$qtBin;$env:PATH"
# QtTest は標準出力がコンソールでないと結果を出さない (AGENTS.md 参照)。
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'

# third_party とテスト自身は測定対象から外し、src 配下だけを見る。
& $toolPath `
    --sources "$repo\src" `
    --excluded_sources "$repo\third_party" `
    --excluded_sources "$repo\tests" `
    --export_type "html:$outDir\html" `
    --export_type "cobertura:$cobertura" `
    -- $testExe
$toolExit = $LASTEXITCODE

if ($toolExit -ne 0) {
    throw "OpenCppCoverage が終了コード $toolExit で失敗した (テスト自体の失敗を含む)。"
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
