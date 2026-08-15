#include <QApplication>

#include "MainWindow.h"
#include "core/AppSettings.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("distore-qt");
    QApplication::setOrganizationName("distore-qt");
    QApplication::setApplicationDisplayName("Distore");

    AppSettings::instance().applyCurrentTheme();

    MainWindow window;
    window.show();

    return app.exec();
}
