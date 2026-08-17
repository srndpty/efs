#include "app/MainWindow.h"
#include "app/Settings.h"
#include "app/Theme.h"
#include "backend/everything/EverythingBackend.h"

#include <QApplication>
#include <QSettings>

#include <memory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("efs"));
    QApplication::setOrganizationName(QStringLiteral("efs"));
    // INI にするのは中身を目視・手編集できるようにするため (計画 8)。
    // → %APPDATA%\efs\efs.ini
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // 設定が無い / 壊れている / スキーマが違う場合は既定値 (Dark / All /
    // Regex OFF / Name 昇順) が返る。
    const efs::Settings settings = efs::Settings::load();
    efs::applyTheme(app, settings.theme);

    // backend の選択は 1 箇所だけ。BackendFactory は 2 つ目の実装が実際に
    // 必要になる Phase 4 まで作らない。
    efs::MainWindow window(std::make_unique<efs::EverythingBackend>(), settings);
    window.show();

    return QApplication::exec();
}
