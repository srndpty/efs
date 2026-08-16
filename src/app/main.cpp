// Phase 0 walking skeleton: prove that a Qt 6.8 QMainWindow builds and shows.
// The real window is built in Phase 1.
#include <QApplication>
#include <QMainWindow>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("efs"));
    QApplication::setOrganizationName(QStringLiteral("efs"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("efs"));
    window.resize(900, 600);
    window.show();

    return QApplication::exec();
}
