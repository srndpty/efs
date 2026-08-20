# dist\efs を配置し、ショートカットを作る (Phase 4)。
#
#   pwsh scripts/install.ps1              # インストール / 上書き更新
#   pwsh scripts/install.ps1 -Uninstall   # 削除
#
# **昇格は不要。** 既定の配置先はユーザー領域 (`%LOCALAPPDATA%\Programs\efs`) で、
# ショートカット 2 つもユーザープロファイル配下なので、どれも管理者権限なしで
# 書ける。個人用ツールであり全ユーザーへ入れる理由が無いので、UAC を毎回
# 通す運用の方が実害が大きい (VS Code 等の per-user インストールと同じ場所)。
# `-Destination` に Program Files を指したときだけ昇格して実行すること。
#
# インストーラ (MSI / MSIX / NSIS / WiX) は作らない — 個人用ツールであり、
# 未署名インストーラの SmartScreen 警告を避けられるこの方式で足りる
# (docs/implementation-plan.md の Phase 4 で確定)。
#
# 設定は %APPDATA%\efs\efs.ini にあり、**インストール先には何も書かない**。
# したがってアンインストールしても設定は残るし、非管理者でも普通に使える。
#
# **fail-closed。** 旧版をいきなり消して上書きしない。staging へ配置して検証し、
# 旧版を backup へ退避してから入れ替える。ショートカット作成まで含めて、途中で
# 失敗したら旧版を復元する。**中途半端なインストールを成功物として残さない。**

[CmdletBinding()]
param(
    # 配置元。既定は scripts/package.ps1 の出力。
    [string]$Source,
    # 配置先。既定はユーザー領域 (昇格が要らない)。
    [string]$Destination = "$env:LOCALAPPDATA\Programs\efs",
    # ログオン時に --tray で自動起動するショートカットを作らない。
    [switch]$NoStartup,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $repo 'dist\efs' }

# 相対パスや `..` を含んだままだと後の比較 (実行中プロセスの照合) がずれる。
function Get-NormalizedPath([string]$path) {
    try { return [System.IO.Path]::GetFullPath($path) } catch { return $path }
}

$Destination = Get-NormalizedPath $Destination
$installedExe = Join-Path $Destination 'efs.exe'

# 入れ替え用の作業ディレクトリ。**配置先と同じ親**に置く (別ボリュームだと
# Move-Item がコピーになり、入れ替えが atomic に近い性質を失う)。
$staging = "$Destination.staging-$PID"
$backup = "$Destination.backup-$PID"
$shortcutBackupDir = Join-Path $env:TEMP "efs-install-shortcuts-$PID"

# ショートカットはどちらもユーザープロファイル配下に置く。個人用ツールなので
# 全ユーザーへ入れる必要が無く、この 2 つは管理者権限なしで作れる
# (昇格が要るのは -Destination にシステム領域を指したときだけ)。
$userPrograms = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$startMenuLink = Join-Path $userPrograms 'efs.lnk'
$startupLink = Join-Path $userPrograms 'Startup\efs.lnk'

# package.ps1 が入れたはずのもの。網羅リストではなく「これが無ければ確実に
# 起動しない」ものだけ (必要ファイルの authority は windeployqt)。
$requiredFiles = @('efs.exe', 'Everything64.dll', 'Qt6Core.dll', 'platforms\qwindows.dll')

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
$Destination へ書き込めない (Program Files 等のシステム領域なら管理者権限が必要)。
既定のユーザー領域 ($env:LOCALAPPDATA\Programs\efs) へ入れるなら -Destination を外す:
  pwsh scripts/install.ps1
そこへ入れたいなら昇格して実行すること:
  Start-Process pwsh -Verb RunAs -ArgumentList '-NoExit','-File','$PSCommandPath','-Destination','$Destination'
"@
}

function Test-InstallContents([string]$root) {
    foreach ($required in $requiredFiles) {
        if (-not (Test-Path (Join-Path $root $required))) {
            throw "$root が不完全: $required が無い。"
        }
    }
}

# **削除するのは配置先を指しているものだけ。** ショートカットの path は配置先に
# 依らず同じ (ユーザープロファイル配下) なので、-Destination だけ違う別の
# インストールを消したときに、生きている方のショートカットを巻き添えにしない。
# 照合は実行中プロセスと同じく `<Destination>\efs.exe` との完全一致。
function Remove-Shortcuts {
    $target = Get-NormalizedPath $installedExe
    $shell = New-Object -ComObject WScript.Shell
    foreach ($link in @($startMenuLink, $startupLink)) {
        if (-not (Test-Path $link -PathType Leaf)) { continue }

        # **fail-closed。** 参照先を確認できたものだけ消す。読めない / 空 /
        # 別 path はいずれも「自分のものだと確認できなかった」として残す。
        # 壊れた .lnk を「参照先が空 = 自分のもの」と扱うと、別のインストールの
        # ショートカットを巻き添えにする経路が復活する。
        $targetPath = $null
        try { $targetPath = $shell.CreateShortcut($link).TargetPath } catch { $targetPath = $null }

        if ([string]::IsNullOrWhiteSpace($targetPath)) {
            Write-Host "ショートカットの参照先を確認できないので残す: $link"
            continue
        }
        if (-not (Get-NormalizedPath $targetPath).Equals($target, [StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "ショートカットは別のインストールを指しているので残す: $link → $targetPath"
            continue
        }

        Write-Host "ショートカットを削除: $link"
        Remove-Item -Force $link
    }
}

# 実行中だと上書きできない。**まず --quit で graceful に終わってもらう** —
# WM_CLOSE (CloseMainWindow) は Phase 4 では「隠す」なので終了しないし、
# 強制終了は設定の保存 (quitApplication) を飛ばしてしまう。
function Stop-RunningEfs {
    # 照合は「配置先の efs.exe そのもの」との完全一致。前方一致にすると
    # `C:\Program Files\efs-old\efs.exe` のような別物まで巻き込む。
    $target = Get-NormalizedPath $installedExe
    $running = @(Get-Process -Name 'efs' -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -and (Get-NormalizedPath $_.Path).Equals($target, [StringComparison]::OrdinalIgnoreCase)
        })
    if ($running.Count -eq 0) { return }

    Write-Host "実行中の efs に終了を要求する ($($running.Count) プロセス)"
    # 自分自身へ --quit を投げる (既存インスタンスが受けて設定を保存して終わる)。
    $quit = Start-Process -FilePath $target -ArgumentList '--quit' -PassThru -Wait
    if ($quit.ExitCode -ne 0) {
        Write-Warning "--quit が終了コード $($quit.ExitCode) を返した。"
    }

    foreach ($process in $running) {
        if ($process.WaitForExit(10000)) { continue }
        Write-Warning "graceful に終了しないため強制終了する (pid $($process.Id))。設定は保存されない。"
        $process.Kill()
        $null = $process.WaitForExit(5000)
    }
}

# 配置先**以外**の場所から efs が常駐していないか。
#
# single instance は「この PC で efs は 1 つ」を前提に組んである (トレイ / グローバル
# ホットキー / `--quit` はどれもそれで初めて意味を持つ)。旧 `Program Files` 版が
# 常駐したまま新しい既定の場所へ入れると、実体とショートカットは新版なのに mutex の
# primary は旧版のまま — 起動しても旧版が前へ出てきて、入れ替えたつもりが効かない。
#
# **kill しない。** 設定の保存 (quitApplication) を飛ばすうえ、ユーザーが意図して
# 使っている可能性がある。診断を出して**何にも触る前に**中止する。
# path を読めないプロセスも「別物ではない」と確認できていないので同じ扱いにする。
function Assert-NoForeignEfs {
    $target = Get-NormalizedPath $installedExe
    $foreign = @(Get-Process -Name 'efs' -ErrorAction SilentlyContinue | Where-Object {
            -not ($_.Path -and (Get-NormalizedPath $_.Path).Equals($target, [StringComparison]::OrdinalIgnoreCase))
        })
    if ($foreign.Count -eq 0) { return }

    $lines = ($foreign | ForEach-Object {
            $path = if ($_.Path) { $_.Path } else { '(path を読めない)' }
            "  pid $($_.Id): $path"
        }) -join "`n"
    $quitHint = ($foreign | Where-Object { $_.Path } | ForEach-Object { "  & '$($_.Path)' --quit" }) -join "`n"
    if (-not $quitHint) { $quitHint = '  (path を読めないので手動で終了させること)' }

    throw @"
配置先とは別の場所の efs が実行中:
$lines
$Destination へ入れる前に終了させること (このまま入れても、多重起動防止の primary は
実行中の方のままで、新しく入れた方は起動しても前へ出てこない):
$quitHint
"@
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
try { Test-InstallContents $Source }
catch { throw "$($_.Exception.Message)`npwsh scripts/package.ps1 をやり直すこと。" }

# 入れ替えのどこまで進んだか。**1 つのフラグにまとめない** — 「旧版を退避した」と
# 「新版を配置した」の間で失敗したときに、退避した旧版を戻さないまま backup ごと
# 消してしまう (= インストールが丸ごと消える) ため。
$hadPrevious = Test-Path $Destination
$previousBackedUp = $false # 旧版を backup へ退避した
$newPlaced = $false        # staging を配置先へ移した
$shortcutsTouched = $false
# rollback 自体が成功したか。**false のまま working dirs を消してはいけない** —
# 消すと復旧に使える最後の正常なコピー (backup / shortcut backup) まで失われる。
$rollbackOk = $true

function Restore-Previous {
    if ($script:previousBackedUp) {
        # 旧版を退避してある。配置先に残っているのは新版か移動途中の残骸なので
        # 捨ててから戻す (**どの境界で失敗しても必ずここを通る**)。
        if (Test-Path $Destination) {
            Remove-Item -Recurse -Force $Destination -ErrorAction SilentlyContinue
        }
        if (Test-Path $backup) {
            Write-Host "旧版を復元: $backup → $Destination"
            try { Move-Item $backup $Destination -ErrorAction Stop }
            catch {
                Write-Warning "旧版を戻せなかった: $($_.Exception.Message)"
                $script:rollbackOk = $false
            }
        } else {
            # 退避したはずの backup が無い。invariant が壊れているので、
            # warning で流さず「復旧できていない」として扱う。
            Write-Warning "退避した旧版が見つからない: $backup"
            $script:rollbackOk = $false
        }
        # 戻した中身が起動できる形かまで見る (移動が中途半端でないこと)。
        if ($script:rollbackOk) {
            try { Test-InstallContents $Destination }
            catch {
                Write-Warning "復元した旧版が不完全: $($_.Exception.Message)"
                $script:rollbackOk = $false
            }
        }
    } elseif (-not $script:hadPrevious -and (Test-Path $Destination)) {
        # 初回インストールの途中で失敗した。partial を成功物として残さない。
        # **削除できたことまで確認する。** ACL / ロックで消せないと partial が
        # 「インストール済み」として残るので、旧版ありの経路と同じく rollback
        # 失敗として扱い、recovery data を消さずに終わる。
        Write-Host "配置途中の $Destination を削除 (初回インストール)"
        try { Remove-Item -Recurse -Force $Destination -ErrorAction Stop }
        catch { Write-Warning "配置途中の $Destination を削除できなかった: $($_.Exception.Message)" }
        if (Test-Path $Destination) {
            Write-Warning "配置途中の $Destination が残っている。"
            $script:rollbackOk = $false
        }
    }
    if ($script:newPlaced) {
        Write-Warning "配置済みの新版を取り消した。"
    }
    # ショートカットも触った分だけ戻す (元が無かったものは消す)。
    # **復元先に何が居ても、まず型を問わず取り除いてから戻す。** ここは efs の
    # ショートカット専用の path なので、.lnk 以外の object (ディレクトリ等) が
    # 居座っているのは異常であり、残す理由が無い。Copy-Item を上書きに頼ると、
    # 相手がディレクトリのときに「その中へコピー」になって .lnk file が復元
    # されない。
    if ($script:shortcutsTouched) {
        foreach ($link in @($startMenuLink, $startupLink)) {
            $saved = Join-Path $shortcutBackupDir (Split-Path -Leaf (Split-Path -Parent $link))
            $saved = Join-Path $saved (Split-Path -Leaf $link)

            if (Test-Path $link) { Remove-Item -Recurse -Force $link -ErrorAction SilentlyContinue }
            if (-not (Test-Path $saved -PathType Leaf)) { continue }

            New-Item -ItemType Directory -Force (Split-Path -Parent $link) | Out-Null
            try { Copy-Item -Force $saved $link -ErrorAction Stop } catch { }
            if (Test-Path $link -PathType Leaf) {
                Write-Host "ショートカットを復元: $link"
            } else {
                # 退避した .lnk があるのに戻せていない。これも復旧未完了として扱う
                # (backup を消さずに残し、手動で戻せるようにする)。
                Write-Warning "ショートカットを復元できなかった: $link"
                $script:rollbackOk = $false
            }
        }
    }
}

function Remove-WorkingDirs {
    foreach ($dir in @($staging, $backup, $shortcutBackupDir)) {
        if (Test-Path $dir) { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
    }
}

# **ここから先は何も触らない前提条件。** 作業ディレクトリの掃除より前に見る。
Assert-NoForeignEfs

# 前回の作業ディレクトリが残っていたら (異常終了した等) 邪魔なので消す。
Remove-WorkingDirs

try {
    Stop-RunningEfs

    # 1. staging へ配置して検証する。ここまでは既存のインストールに触れない。
    Write-Host "staging へコピー: $Source → $staging"
    New-Item -ItemType Directory -Force $staging | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Source '*') $staging
    Test-InstallContents $staging

    # 2. 旧版を backup へ退避する (消してからコピーしない)。**退避した時点で
    #    フラグを立てる** — 次の Move-Item が失敗しても旧版を戻せるように。
    if (Test-Path $Destination) {
        Write-Host "旧版を退避: $Destination → $backup"
        Move-Item $Destination $backup
        $previousBackedUp = $true
    }

    # 3. 新版を配置する。
    Move-Item $staging $Destination
    $newPlaced = $true
    Write-Host "配置: $Destination"

    # 4. 入れ替えた実体をもう一度検証する (移動が中途半端でないこと)。
    Test-InstallContents $Destination

    # 5. ショートカット。ここで失敗しても rollback 対象にするため、既存の
    #    .lnk を先に退避しておく。
    #    **退避するのは .lnk file (Leaf) だけ。** その path にディレクトリ等が
    #    居る場合は「正常な既存ショートカット」とは扱わず、退避もしない
    #    (戻すべき中身が無いため)。rollback ではその object を取り除いて終わる。
    New-Item -ItemType Directory -Force (Split-Path -Parent $startupLink) | Out-Null
    foreach ($link in @($startMenuLink, $startupLink)) {
        if (Test-Path $link -PathType Leaf) {
            $savedDir = Join-Path $shortcutBackupDir (Split-Path -Leaf (Split-Path -Parent $link))
            New-Item -ItemType Directory -Force $savedDir | Out-Null
            Copy-Item -Force $link (Join-Path $savedDir (Split-Path -Leaf $link))
        }
    }
    $shortcutsTouched = $true

    $shell = New-Object -ComObject WScript.Shell

    Write-Host "スタートメニュー: $startMenuLink"
    $link = $shell.CreateShortcut($startMenuLink)
    $link.TargetPath = $installedExe
    $link.WorkingDirectory = $Destination
    $link.Description = 'efs — Everything backed file search'
    $link.Save()
    if (-not (Test-Path $startMenuLink)) { throw "ショートカットを作れなかった: $startMenuLink" }

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
        if (-not (Test-Path $startupLink)) { throw "ショートカットを作れなかった: $startupLink" }
    }
} catch {
    Write-Warning "インストールに失敗した: $($_.Exception.Message)"
    Restore-Previous
    # **rollback の完了を確認できたときだけ作業ディレクトリを消す。**
    # 失敗したまま消すと、復旧に使える最後の正常なコピーごと失われる。
    if ($rollbackOk) {
        Remove-WorkingDirs
    } else {
        # 何が残っているかは失敗した境界で違う (初回インストールなら backup は
        # 無い)。**実在するものだけを挙げる** — 無い path を「ここから戻せる」と
        # 書くと復旧の手がかりとして嘘になる。
        $left = @()
        if (Test-Path $backup) { $left += "  旧版 (退避):           $backup" }
        if (Test-Path $shortcutBackupDir) { $left += "  ショートカット (退避): $shortcutBackupDir" }
        if (Test-Path $staging) { $left += "  staging:               $staging" }
        if (Test-Path $Destination) { $left += "  配置先 (中途半端):     $Destination" }
        $leftText = if ($left.Count -gt 0) { $left -join "`n" } else { '  (残っているものは無い)' }
        # 手順も状況に合わせる。旧版が無い初回インストールで「旧版を戻せ」と
        # 出すのは案内として嘘になる。
        $howTo = if (Test-Path $backup) {
            "手動で復旧する場合は、$backup を $Destination へ移し、退避した`nショートカットを $userPrograms 配下へ戻すこと。"
        } else {
            "旧版は無い (初回インストール)。中途半端な $Destination を手動で削除すること。"
        }
        Write-Warning @"
rollback を完了できなかった。**復旧用のデータは消さずに残してある。**
$leftText
$howTo
"@
    }
    throw
}

# 成功。ここで初めて旧版を捨てる。
Remove-WorkingDirs

$files = @(Get-ChildItem -Path $Destination -Recurse -File)
Write-Host ""
Write-Host "インストール完了: $Destination ($($files.Count) files)"
Write-Host "設定は $env:APPDATA\efs\efs.ini (インストール先には書き込まない)"
Write-Host ""
Write-Host "注意: 検索には Everything 本体が別途常駐している必要がある。"
Write-Host "      Everything のトレイアイコンを消したい場合は"
Write-Host "      %APPDATA%\Everything\Everything.ini の show_tray_icon=0"
