# Release の配布ディレクトリを再現可能に作る (Phase 3)。
#
# ローカル: pwsh scripts/package.ps1
# CI      : pwsh scripts/package.ps1 -BuildDir build/ci -Config Release
#
# 出力: dist/efs/ (efs.exe + Everything64.dll + windeployqt が入れた Qt 一式)
#
# **fail-closed。** 途中で失敗したら古い package を成功物として残さない。
# 前回の出力を消してから始め、各コマンドの終了コードと、最低限の critical
# artifact の存在を確認する。どれか欠けたら dist を消して非ゼロで終わる。
#
# **Qt のパスをスクリプトへ固定しない** (C:\Qt\6.8.3\... のようなハードコードを
# しない)。ビルドディレクトリの CMakeCache.txt に入っている実際の prefix、環境
# 変数、PATH の順に解決し、見つからなければ「どこを探したか」を出して落ちる。
#
# **windeployqt は Everything64.dll を知らない。** Qt の依存だけを見るので、
# アプリ自身が LoadLibraryW する DLL は明示的にコピーする必要がある。

[CmdletBinding()]
param(
    # 設定済みのビルドディレクトリ。
    [string]$BuildDir,
    # マルチ構成ジェネレータ (Visual Studio) で使う構成名。
    [string]$Config = 'Release',
    # 出力先。既定は <repo>\dist\efs
    [string]$OutDir,
    # Qt の bin ディレクトリ。省略時は下の探索順で解決する。
    [string]$QtBin,
    # windeployqt を直接指定する (最優先)。
    [string]$WindeployQt
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$defaultBuildDir = Join-Path $repo 'build\msvc2022-x64'
if (-not $BuildDir) { $BuildDir = $defaultBuildDir }
if (-not $OutDir)   { $OutDir   = Join-Path $repo 'dist\efs' }

$searched = [System.Collections.Generic.List[string]]::new()

function Find-WindeployQt {
    # 1. 明示指定
    if ($WindeployQt) {
        $searched.Add("-WindeployQt: $WindeployQt")
        if (Test-Path $WindeployQt) { return (Resolve-Path $WindeployQt).Path }
    }
    # 2. -QtBin
    if ($QtBin) {
        $candidate = Join-Path $QtBin 'windeployqt.exe'
        $searched.Add("-QtBin: $candidate")
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    # 3. ビルドディレクトリの CMakeCache.txt。**ここが本命** — 実際にビルドに
    #    使われた Qt を使うので、複数の Qt が入っていても取り違えない。
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path $cache) {
        $prefixes = @()
        foreach ($line in (Get-Content $cache)) {
            # CMAKE_PREFIX_PATH は ; 区切りのこともある。Qt6_DIR は
            # <prefix>/lib/cmake/Qt6 を指すので 3 階層上が prefix。
            if ($line -match '^CMAKE_PREFIX_PATH:[^=]*=(.+)$') {
                $prefixes += ($Matches[1] -split ';' | Where-Object { $_ })
            } elseif ($line -match '^Qt6_DIR:[^=]*=(.+)$') {
                $qt6Dir = $Matches[1]
                if ($qt6Dir) { $prefixes += (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qt6Dir))) }
            }
        }
        foreach ($prefix in ($prefixes | Select-Object -Unique)) {
            $candidate = Join-Path $prefix 'bin\windeployqt.exe'
            $searched.Add("CMakeCache: $candidate")
            if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
        }
    } else {
        $searched.Add("CMakeCache: $cache (無い)")
    }
    # 4. 環境変数
    foreach ($root in @($env:QT_ROOT_DIR, $env:QTDIR)) {
        if (-not $root) { continue }
        $candidate = Join-Path $root 'bin\windeployqt.exe'
        $searched.Add("環境変数: $candidate")
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    # 5. PATH
    $onPath = (Get-Command windeployqt -ErrorAction SilentlyContinue).Source
    $searched.Add("PATH: windeployqt")
    if ($onPath) { return $onPath }

    return $null
}

# --- 1. 前回の出力を消す (clean start) ---------------------------------------
# 残骸を今回の成功物と誤認するのが一番危ない。
if (Test-Path $OutDir) {
    Write-Host "古い package を削除: $OutDir"
    Remove-Item -Recurse -Force $OutDir
}

# 以降のどこかで落ちたら、作りかけの dist を残さない。
trap {
    if (Test-Path $OutDir) {
        Write-Host "失敗したため作りかけの package を削除: $OutDir"
        Remove-Item -Recurse -Force $OutDir -ErrorAction SilentlyContinue
    }
    break
}

# --- 2. Release ビルド ---------------------------------------------------------
if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    if ($BuildDir -eq $defaultBuildDir) {
        Write-Host "ビルドディレクトリが未構成なのでプリセットで構成する: $BuildDir"
        & cmake --preset msvc2022-x64
        if ($LASTEXITCODE -ne 0) { throw "cmake --preset msvc2022-x64 が終了コード $LASTEXITCODE で失敗した。" }
    } else {
        throw "構成済みのビルドディレクトリが無い: $BuildDir`n先に cmake で構成すること。"
    }
}

Write-Host "Release ビルド: $BuildDir ($Config)"
& cmake --build $BuildDir --config $Config --target efs
if ($LASTEXITCODE -ne 0) { throw "Release ビルドが終了コード $LASTEXITCODE で失敗した。" }

# 出力先はジェネレータで変わる (Ninja は build ルート、Visual Studio は
# build/<Config>)。決め打ちにせず探す。
# @() で必ず配列にする。1 件だけだとスカラー (文字列) に落ちて [0] が
# 「先頭の 1 文字」になってしまう。
$exeCandidates = @(@(
    (Join-Path $BuildDir "$Config\efs.exe"),
    (Join-Path $BuildDir 'efs.exe')
) | Where-Object { Test-Path $_ })
if ($exeCandidates.Count -eq 0) {
    throw "ビルドした efs.exe が見つからない。探した場所: $(Join-Path $BuildDir "$Config\efs.exe"), $(Join-Path $BuildDir 'efs.exe')"
}
$exe = $exeCandidates[0]

# --- 3. exe を配布ディレクトリへ ----------------------------------------------
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $exe (Join-Path $OutDir 'efs.exe') -Force

# --- 4. windeployqt --------------------------------------------------------
$tool = Find-WindeployQt
if (-not $tool) {
    throw @"
windeployqt が見つからない。探した場所:
  $($searched -join "`n  ")
-QtBin <Qt の bin> か -WindeployQt <実行ファイル> で指定するか、
ビルドディレクトリを Qt を指す CMAKE_PREFIX_PATH で構成すること。
"@
}
Write-Host "windeployqt : $tool"

# --no-translations / --no-system-d3d-compiler 等の絞り込みはしない。
# 必要ファイルの authority は windeployqt の判断に任せる。
& $tool --release --dir $OutDir (Join-Path $OutDir 'efs.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt が終了コード $LASTEXITCODE で失敗した。" }

# --- 5. Everything64.dll ------------------------------------------------------
# windeployqt は Qt の依存しか見ない。efs は Everything64.dll を実行時に
# LoadLibraryW するので、ここで明示的にコピーしなければ配布版で必ず欠ける。
$everythingDll = Join-Path $repo 'third_party\everything-sdk\dll\Everything64.dll'
if (-not (Test-Path $everythingDll)) { throw "Everything64.dll が無い: $everythingDll" }
Copy-Item $everythingDll (Join-Path $OutDir 'Everything64.dll') -Force

# --- 6/7. critical artifact の存在確認 ----------------------------------------
# 一覧を網羅しようとはしない (必要ファイルの authority は windeployqt)。
# 「これが無ければ確実に起動しない」ものだけを見る。
$critical = @(
    'efs.exe',
    'Everything64.dll',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'platforms\qwindows.dll'
)
$missing = @($critical | Where-Object { -not (Test-Path (Join-Path $OutDir $_)) })
if ($missing.Count -gt 0) {
    throw "配布物に必須ファイルが無い: $($missing -join ', ')"
}

$files = @(Get-ChildItem -Path $OutDir -Recurse -File)
$size = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host "package: $OutDir"
Write-Host ("files  : {0} / {1:N1} MiB" -f $files.Count, ($size / 1MB))
foreach ($name in $critical) {
    Write-Host "  ok   : $name"
}
