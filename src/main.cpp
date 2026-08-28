#include <QApplication>

#include "MainWindow.h"
#include "core/AppSettings.h"
#include "core/Logger.h"

int main(int argc, char *argv[])
{
    Logger::install();

    QApplication app(argc, argv);
    QApplication::setApplicationName("athena");
    QApplication::setOrganizationName("athena");
    QApplication::setApplicationDisplayName("Athena");

    AppSettings::instance().applyCurrentTheme();

    MainWindow window;
    window.show();

    return app.exec();
}
