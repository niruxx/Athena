#include <QApplication>

#include "MainWindow.h"
#include "core/AppSettings.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("athena");
    QApplication::setOrganizationName("athena");
    QApplication::setApplicationDisplayName("Athena");

    AppSettings::instance().applyCurrentTheme();

    MainWindow window;
    window.show();

    return app.exec();
}
