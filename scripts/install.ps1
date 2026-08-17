# dist\efs を Program Files へ配置し、ショートカットを作る (Phase 4)。
#
#   pwsh scripts/install.ps1              # インストール / 上書き更新
#   pwsh scripts/install.ps1 -Uninstall   # 削除
#
# **管理者権限が必要** (Program Files へ書くため)。
#
# インストーラ (MSI / MSIX / NSIS / WiX) は作らない — 個人用ツールであり、
# 未署名インストーラの SmartScreen 警告を避けられるこの方式で足りる
# (docs/implementation-plan.md の Phase 4 で確定)。
#
# 設定は %APPDATA%\efs\efs.ini にあり、**インストール先には何も書かない**。
# したがってアンインストールしても設定は残るし、非管理者でも普通に使える。
#
# fail-closed: 事前条件を全部確認してから触り始め、途中で失敗したら
# 中途半端な状態を残さない。

[CmdletBinding()]
param(
    # 配置元。既定は scripts/package.ps1 の出力。
    [string]$Source,
    # 配置先。
    [string]$Destination = "$env:ProgramFiles\efs",
    # ログオン時に --tray で自動起動するショートカットを作らない。
    [switch]$NoStartup,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $repo 'dist\efs' }

# ショートカットはどちらもユーザープロファイル配下に置く。個人用ツールなので
# 全ユーザーへ入れる必要が無く、この 2 つは管理者権限なしで作れる
# (昇格が要るのは Program Files へのコピーだけ)。
$userPrograms = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$startMenuLink = Join-Path $userPrograms 'efs.lnk'
$startupLink = Join-Path $userPrograms 'Startup\efs.lnk'

# --- 事前条件 -----------------------------------------------------------------
# 「管理者かどうか」ではなく「実際に書けるか」を見る。こうしておくと
# -Destination にユーザー書き込み可能な場所を指したときに昇格を要求せずに済み、
# スクリプト自体をそのまま検証できる。
function Test-Writable([string]$path) {
    $parent = Split-Path -Parent $path
    if (-not $parent) { return $false }
    if (-not (Test-Path $parent)) {
        try { New-Item -ItemType Directory -Force $parent -ErrorAction Stop | Out-Null }
        catch { return $false }
    }
    $probe = Join-Path $parent ".efs-write-probe-$PID"
    try {
        New-Item -ItemType File $probe -ErrorAction Stop | Out-Null
        Remove-Item -Force $probe -ErrorAction SilentlyContinue
        return $true
    } catch {
        return $false
    }
}

if (-not (Test-Writable $Destination)) {
    throw @"
$Destination へ書き込めない (Program Files なら管理者権限が必要)。
管理者の PowerShell で実行すること:
  Start-Process pwsh -Verb RunAs -ArgumentList '-NoExit','-File','$PSCommandPath'
"@
}

function Remove-Shortcuts {
    foreach ($link in @($startMenuLink, $startupLink)) {
        if (Test-Path $link) {
            Write-Host "ショートカットを削除: $link"
            Remove-Item -Force $link
        }
    }
}

# 実行中だと上書きできない。常駐しているのが普通なので、黙って止める前に告げる。
function Stop-RunningEfs {
    $running = @(Get-Process -Name 'efs' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($Destination, [StringComparison]::OrdinalIgnoreCase) })
    if ($running.Count -eq 0) { return }

    Write-Host "実行中の efs を終了する ($($running.Count) プロセス)"
    foreach ($process in $running) {
        # まずは行儀よく閉じる (設定の保存を走らせるため)。
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            Write-Warning "応答しないため強制終了する (pid $($process.Id))"
            $process.Kill()
            $null = $process.WaitForExit(5000)
        }
    }
}

# --- アンインストール ---------------------------------------------------------
if ($Uninstall) {
    Stop-RunningEfs
    Remove-Shortcuts
    if (Test-Path $Destination) {
        Write-Host "削除: $Destination"
        Remove-Item -Recurse -Force $Destination
    }
    Write-Host ""
    Write-Host "アンインストール完了。"
    Write-Host "設定 ($env:APPDATA\efs\efs.ini) は残してある。不要なら手動で削除すること。"
    return
}

# --- インストール -------------------------------------------------------------
if (-not (Test-Path $Source)) {
    throw "配置元が無い: $Source`n先に配布ディレクトリを作ること: pwsh scripts/package.ps1"
}
$sourceExe = Join-Path $Source 'efs.exe'
if (-not (Test-Path $sourceExe)) {
    throw "$Source に efs.exe が無い。package.ps1 が失敗している可能性がある。"
}
# package.ps1 が入れたはずのものを最低限だけ確認する (詳細は package.ps1 側の責務)。
foreach ($required in @('Everything64.dll', 'Qt6Core.dll', 'platforms\qwindows.dll')) {
    if (-not (Test-Path (Join-Path $Source $required))) {
        throw "配置元が不完全: $required が無い。pwsh scripts/package.ps1 をやり直すこと。"
    }
}

Stop-RunningEfs

# 古い版のファイルが残らないよう、置き換えは全消し → コピー。
if (Test-Path $Destination) {
    Write-Host "既存のインストールを削除: $Destination"
    Remove-Item -Recurse -Force $Destination
}
Write-Host "コピー: $Source → $Destination"
New-Item -ItemType Directory -Force $Destination | Out-Null
Copy-Item -Recurse -Force (Join-Path $Source '*') $Destination

$installedExe = Join-Path $Destination 'efs.exe'
if (-not (Test-Path $installedExe)) {
    Remove-Item -Recurse -Force $Destination -ErrorAction SilentlyContinue
    throw "コピーに失敗した ($installedExe が無い)。"
}

# --- ショートカット -----------------------------------------------------------
$shell = New-Object -ComObject WScript.Shell
New-Item -ItemType Directory -Force (Split-Path -Parent $startupLink) | Out-Null

Write-Host "スタートメニュー: $startMenuLink"
$link = $shell.CreateShortcut($startMenuLink)
$link.TargetPath = $installedExe
$link.WorkingDirectory = $Destination
$link.Description = 'efs — Everything backed file search'
$link.Save()

if ($NoStartup) {
    if (Test-Path $startupLink) { Remove-Item -Force $startupLink }
    Write-Host "スタートアップ登録: なし (-NoStartup)"
} else {
    # --tray でウィンドウを出さずトレイに常駐する。ログオンのたびに
    # ウィンドウが開かないようにするためで、設定項目は増やさない。
    Write-Host "スタートアップ: $startupLink (--tray)"
    $startup = $shell.CreateShortcut($startupLink)
    $startup.TargetPath = $installedExe
    $startup.Arguments = '--tray'
    $startup.WorkingDirectory = $Destination
    $startup.Description = 'efs (tray)'
    $startup.Save()
}

$files = @(Get-ChildItem -Path $Destination -Recurse -File)
Write-Host ""
Write-Host "インストール完了: $Destination ($($files.Count) files)"
Write-Host "設定は $env:APPDATA\efs\efs.ini (インストール先には書き込まない)"
Write-Host ""
Write-Host "注意: 検索には Everything 本体が別途常駐している必要がある。"
Write-Host "      Everything のトレイアイコンを消したい場合は"
Write-Host "      %APPDATA%\Everything\Everything.ini の show_tray_icon=0"
