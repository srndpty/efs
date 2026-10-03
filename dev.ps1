# 既存の開発手順への入口。ユーザー環境は変更しない。
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = 'help'
)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot

function Invoke-Tool {
    param([string]$Tool, [string[]]$ToolArgs)
    & $Tool @ToolArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Build-Debug {
    Invoke-Tool cmake @('--preset', 'msvc2022-x64')
    Invoke-Tool cmake @('--build', '--preset', 'msvc2022-x64-debug')
}

function Test-Debug {
    Invoke-Tool ctest @('--preset', 'msvc2022-x64-debug')
}

function Start-Gui {
    Build-Debug
    # 実際に構成された Qt を使い、子プロセスの終了後に PATH を戻す。
    $cache = Get-Content (Join-Path $repo 'build/msvc2022-x64/CMakeCache.txt')
    $qtDir = $cache | Select-String '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
    if (-not $qtDir) { throw 'CMakeCache.txt に Qt6_DIR が無い。' }
    $qtBin = [IO.Path]::GetFullPath((Join-Path $qtDir.Matches[0].Groups[1].Value '../../../bin'))
    $previousPath = $env:PATH
    try {
        $env:PATH = "$qtBin;$previousPath"
        $process = Start-Process -FilePath (Join-Path $repo 'build/msvc2022-x64/Debug/efs.exe') -PassThru -Wait
        if ($process.ExitCode -ne 0) { exit $process.ExitCode }
    } finally {
        $env:PATH = $previousPath
    }
}

Push-Location $repo
try {
    switch ($Command.ToLowerInvariant()) {
        'help' {
            Write-Host @'
使い方: .\dev.ps1 <コマンド> または dev <コマンド>
  build    MSVC x64 Debug を構成・ビルド
  run      Debug をビルドして efs を起動（終了まで待機）
  gui      run と同じ GUI 起動
  test     Debug の通常テスト（事前に build が必要）
  lint     Ninja Debug を構成・ビルドし、既存の lint を実行
           Developer PowerShell が必要。初回の準備は scripts/lint.ps1 -Bootstrap
  check    コミット前検証: pre-commit 全件 → Debug ビルド → テスト
  clean    CMake の Debug clean ターゲットを実行（事前に構成が必要）
  package  既存の配布スクリプトで Release 配布物を作成
  install  package 後に既存の配置スクリプトで最新版をインストール
  help     この説明を表示
'@
        }
        'build' { Build-Debug }
        'run' { Start-Gui }
        'gui' { Start-Gui }
        'test' { Test-Debug }
        'lint' {
            Invoke-Tool cmake @('--preset', 'ninja-x64-debug')
            Invoke-Tool cmake @('--build', '--preset', 'ninja-x64-debug')
            Invoke-Tool pwsh @('-NoProfile', '-File', (Join-Path $repo 'scripts/lint.ps1'))
        }
        'check' {
            Invoke-Tool pre-commit @('run', '--all-files')
            Build-Debug
            Test-Debug
        }
        'clean' { Invoke-Tool cmake @('--build', '--preset', 'msvc2022-x64-debug', '--target', 'clean') }
        'package' { Invoke-Tool pwsh @('-NoProfile', '-File', (Join-Path $repo 'scripts/package.ps1')) }
        'install' {
            Invoke-Tool pwsh @('-NoProfile', '-File', (Join-Path $repo 'scripts/package.ps1'))
            Invoke-Tool pwsh @('-NoProfile', '-File', (Join-Path $repo 'scripts/install.ps1'))
        }
        default { throw "不明なコマンド: $Command。dev help を参照すること。" }
    }
} catch {
    Write-Error -Message $_ -ErrorAction Continue
    exit 1
} finally {
    Pop-Location
}
exit 0
