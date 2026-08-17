#include "app/MainWindow.h"
#include "app/Settings.h"
#include "app/SingleInstance.h"
#include "app/Theme.h"
#include "app/ToolbarIcons.h"
#include "backend/everything/EverythingBackend.h"

#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>

#include <memory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("efs"));
    QApplication::setOrganizationName(QStringLiteral("efs"));
    QApplication::setWindowIcon(efs::appIcon());
    // INI にするのは中身を目視・手編集できるようにするため (計画 8)。
    // → %APPDATA%\efs\efs.ini
    // インストール先 (Program Files) には何も書かない。非管理者でも動くこと。
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // `--quit` は「既存インスタンスに graceful に終わってもらう」ためだけの
    // 起動 (install.ps1 が更新前に使う)。**この経路で UI を出してはならない。**
    const bool quitRequested = QApplication::arguments().contains(QStringLiteral("--quit"));

    // 常駐アプリなので多重起動させない。2 個目は既存インスタンスへ要求を投げて
    // 自分は終了する (ショートカットを二度押した、程度のことで増殖させない)。
    efs::SingleInstance instance;
    switch (instance.role()) {
    case efs::InstanceRole::Secondary: {
        const auto request =
            quitRequested ? efs::InstanceRequest::Quit : efs::InstanceRequest::Show;
        QString reason;
        if (!efs::SingleInstance::requestExistingInstance(request, &reason)) {
            // 送れていないのに成功として終わらない (install.ps1 が graceful に
            // 終わったと誤解し、そのまま強制終了へ進んでしまう)。
            qCritical("既存インスタンスへ要求を送れない: %s", qUtf8Printable(reason));
            return 1;
        }
        return 0;
    }
    case efs::InstanceRole::Error: {
        // 多重起動の判定そのものができなかった。**Secondary と同一視しない。**
        // かつ **fail-open にしない** — single instance は Phase 4 の前提
        // (トレイ常駐 + グローバルホットキー + `--quit` はどれも「efs は 1 つ」
        // が成り立って初めて意味を持つ)。判定できないまま起動を許すと、
        // ホットキーの奪い合いや設定の上書き合いが起きる状態になる。
        qCritical("多重起動の判定ができない: %s", qUtf8Printable(instance.errorReason()));
        if (!quitRequested) {
            // コンソールを持たない GUI アプリなので、理由は短く modal で出す
            // (起動できない理由が何も出ないと、ただ消えたようにしか見えない)。
            QMessageBox::critical(
                nullptr, QStringLiteral("efs"),
                QStringLiteral("efs cannot start: %1").arg(instance.errorReason()));
        }
        return 1;
    }
    case efs::InstanceRole::Primary:
        // 終わらせる相手が居ない = すでに目的は達成されている。UI は出さない。
        if (quitRequested)
            return 0;
        break;
    }

    // 閉じるボタンは「隠す」なので、最後のウィンドウが閉じても終了させない。
    // 終了するのは MainWindow::quitApplication() だけ
    // (トレイの Quit と `--quit` IPC が呼ぶ)。
    QApplication::setQuitOnLastWindowClosed(false);

    // 設定が無い / 壊れている / スキーマが違う場合は既定値 (Dark / All /
    // Regex OFF / Name 昇順 / Ctrl+Alt+E) が返る。
    const efs::Settings settings = efs::Settings::load();
    efs::applyTheme(app, settings.theme);

    // backend の選択は 1 箇所だけ。BackendFactory は 2 つ目の実装が実際に
    // 必要になる Phase 5 まで作らない。
    efs::MainWindow window(std::make_unique<efs::EverythingBackend>(), settings);

    QObject::connect(&instance, &efs::SingleInstance::showRequested, &window,
                     &efs::MainWindow::showAndActivate);
    // `--quit` はトレイの Quit と同じ 1 本へ入れる (設定の保存を必ず通す)。
    QObject::connect(&instance, &efs::SingleInstance::quitRequested, &window,
                     &efs::MainWindow::quitApplication);

    // スタートアップ登録 (scripts/install.ps1) は --tray を渡す。ログオン時に
    // ウィンドウを出さず、トレイに常駐するだけにするため。設定項目は増やさない。
    // ただしトレイが使えない環境では隠れたままにしない (呼び出す手段が無くなる)。
    const bool startHidden =
        QApplication::arguments().contains(QStringLiteral("--tray")) && window.hasTrayIcon();
    if (!startHidden)
        window.showAndActivate();

    return QApplication::exec();
}
