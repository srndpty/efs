# OpenCppCoverage でテストのカバレッジを取得する。
#
# 前提: winget install OpenCppCoverage.OpenCppCoverage
#       (または choco install opencppcoverage)
#
# MSVC には gcov 相当が無く、OpenCppCoverage は PDB を読むため Debug ビルドが必要。
# 使い方: pwsh scripts/coverage.ps1

$ErrorActionPreference = 'Stop'

$repo      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $repo 'build\msvc2022-x64'
$testExe   = Join-Path $buildDir 'Debug\efs_tests.exe'
$outDir    = Join-Path $repo 'build\coverage'
$qtBin     = 'C:\Qt\6.8.3\msvc2022_64\bin'

$tool = Get-Command OpenCppCoverage -ErrorAction SilentlyContinue
if (-not $tool) {
    Write-Error "OpenCppCoverage が見つからない。winget install OpenCppCoverage.OpenCppCoverage を実行すること。"
}

if (-not (Test-Path $testExe)) {
    Write-Error "$testExe が無い。先に cmake --build --preset msvc2022-x64-debug を実行すること。"
}

$env:PATH = "$qtBin;$env:PATH"
New-Item -ItemType Directory -Force $outDir | Out-Null

# third_party とテスト自身は測定対象から外し、src 配下だけを見る。
& $tool.Source `
    --sources "$repo\src" `
    --excluded_sources "$repo\third_party" `
    --excluded_sources "$repo\tests" `
    --export_type "html:$outDir\html" `
    --export_type "cobertura:$outDir\cobertura.xml" `
    -- $testExe

Write-Host "カバレッジ出力: $outDir\html\index.html"
