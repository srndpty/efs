#include "app/MainWindow.h"
#include "app/Settings.h"
#include "app/SingleInstance.h"
#include "app/Theme.h"
#include "app/ToolbarIcons.h"
#include "backend/everything/EverythingBackend.h"

#include <QApplication>
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

    // 常駐アプリなので多重起動させない。2 個目は既存インスタンスを前面に出して
    // 自分は終了する (ショートカットを二度押した、程度のことで増殖させない)。
    efs::SingleInstance instance;
    if (!instance.isPrimary()) {
        efs::SingleInstance::requestExistingInstanceToShow();
        return 0;
    }

    // 閉じるボタンは「隠す」なので、最後のウィンドウが閉じても終了させない。
    // 終了はトレイメニューの Quit だけ。
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

    // スタートアップ登録 (scripts/install.ps1) は --tray を渡す。ログオン時に
    // ウィンドウを出さず、トレイに常駐するだけにするため。設定項目は増やさない。
    // ただしトレイが使えない環境では隠れたままにしない (呼び出す手段が無くなる)。
    const bool startHidden =
        QApplication::arguments().contains(QStringLiteral("--tray")) && window.hasTrayIcon();
    if (!startHidden)
        window.showAndActivate();

    return QApplication::exec();
}
