#include "app/MainWindow.h"
#include "app/Theme.h"
#include "backend/everything/EverythingBackend.h"

#include <QApplication>

#include <memory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("efs"));
    QApplication::setOrganizationName(QStringLiteral("efs"));

    // Phase 2 は Dark 固定。切替 UI と永続化は Phase 3。
    efs::applyDarkTheme(app);

    // backend の選択は 1 箇所だけ。BackendFactory は 2 つ目の実装が実際に
    // 必要になる Phase 4 まで作らない。
    efs::MainWindow window(std::make_unique<efs::EverythingBackend>());
    window.resize(1000, 640);
    window.show();

    return QApplication::exec();
}
