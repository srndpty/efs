// Phase 0 の walking skeleton。Qt 6.8 の QMainWindow がビルドでき、実際に表示
// されることだけを確認する。中身は Phase 1 で作る。
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
