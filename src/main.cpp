#include <QApplication>
#include <QDir>
#include <QIcon>

#include "MainWindow.h"
#include "core/AppSettings.h"
#include "core/Logger.h"
#include "ui/Motion.h"

int main(int argc, char *argv[])
{
    Logger::install();

    QApplication app(argc, argv);
    QApplication::setApplicationName("athena");
    QApplication::setOrganizationName("athena");
    QApplication::setApplicationDisplayName("Athena");

    // Flatpak exports its apps' icons here; desktop sessions usually add
    // these to XDG_DATA_DIRS, but not always (and never for a bare `athena`
    // launched from a terminal without the flatpak profile script).
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths()
                               << QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons")
                               << QStringLiteral("/var/lib/flatpak/exports/share/icons"));

    AppSettings::instance().applyCurrentTheme();
    Motion::install(&app);

    MainWindow window;
    window.show();

    return app.exec();
}
