#include "AppSettings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace {
const QString kThemeModeKey = QStringLiteral("appearance/themeMode");
// Deliberately "app", not "general": QSettings' INI backend specially
// reserves a section named "General" (case-insensitively) for legacy
// Windows-.ini compatibility, silently mangling any group with that name
// on disk — keys would read back fine within the process that wrote them
// (in-memory cache) but fail to load on the next launch, since a fresh
// QSettings reading the file back gets a differently-cased group it
// doesn't recognize. Discovered via an actual cross-process round-trip
// test while wiring up the first-run flag below.
const QString kStartupTabKey = QStringLiteral("app/startupTab");
const QString kAutoCloseTerminalKey = QStringLiteral("app/autoCloseTerminalOnSuccess");
const QString kCheckForAppUpdatesKey = QStringLiteral("app/checkForAppUpdatesOnStartup");
const QString kCompactListsKey = QStringLiteral("appearance/compactPackageLists");
const QString kFirstRunCompletedKey = QStringLiteral("app/firstRunCompleted");

// The classic "Fusion dark" palette. Native widget styles (KDE's Breeze,
// GNOME's Adwaita-Qt, etc.) mostly ignore QStyleHints::setColorScheme and
// derive their colors from the desktop's own theme config instead, so
// forcing an explicit Light/Dark mode has to go through the Fusion style
// (which always paints strictly from QPalette) with an explicit palette.
QPalette darkFusionPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(37, 37, 38));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    palette.setColor(QPalette::Button, QColor(45, 45, 48));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(42, 130, 218));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));
    return palette;
}
} // namespace

AppSettings::AppSettings() : QObject(nullptr) { }

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

ThemeMode AppSettings::themeMode() const
{
    QSettings settings;
    const int value = settings.value(kThemeModeKey, static_cast<int>(ThemeMode::System)).toInt();
    if (value == static_cast<int>(ThemeMode::Light))
        return ThemeMode::Light;
    if (value == static_cast<int>(ThemeMode::Dark))
        return ThemeMode::Dark;
    return ThemeMode::System;
}

void AppSettings::setThemeMode(ThemeMode mode)
{
    QSettings settings;
    settings.setValue(kThemeModeKey, static_cast<int>(mode));

    applyTheme(mode);
    emit themeModeChanged(mode);
}

void AppSettings::applyCurrentTheme() const
{
    applyTheme(themeMode());
}

StartupTab AppSettings::startupTab() const
{
    QSettings settings;
    const int value = settings.value(kStartupTabKey, static_cast<int>(StartupTab::System)).toInt();
    if (value == static_cast<int>(StartupTab::Flatpak))
        return StartupTab::Flatpak;
    if (value == static_cast<int>(StartupTab::Snap))
        return StartupTab::Snap;
    return StartupTab::System;
}

void AppSettings::setStartupTab(StartupTab tab)
{
    QSettings settings;
    settings.setValue(kStartupTabKey, static_cast<int>(tab));
}

bool AppSettings::autoCloseTerminalOnSuccess() const
{
    QSettings settings;
    return settings.value(kAutoCloseTerminalKey, false).toBool();
}

void AppSettings::setAutoCloseTerminalOnSuccess(bool autoClose)
{
    QSettings settings;
    settings.setValue(kAutoCloseTerminalKey, autoClose);
}

bool AppSettings::checkForAppUpdatesOnStartup() const
{
    QSettings settings;
    return settings.value(kCheckForAppUpdatesKey, true).toBool();
}

void AppSettings::setCheckForAppUpdatesOnStartup(bool check)
{
    QSettings settings;
    settings.setValue(kCheckForAppUpdatesKey, check);
}

bool AppSettings::compactPackageLists() const
{
    QSettings settings;
    return settings.value(kCompactListsKey, false).toBool();
}

void AppSettings::setCompactPackageLists(bool compact)
{
    QSettings settings;
    settings.setValue(kCompactListsKey, compact);
    emit compactPackageListsChanged(compact);
}

bool AppSettings::hasCompletedFirstRun() const
{
    QSettings settings;
    return settings.value(kFirstRunCompletedKey, false).toBool();
}

void AppSettings::setHasCompletedFirstRun(bool completed)
{
    QSettings settings;
    settings.setValue(kFirstRunCompletedKey, completed);
}

void AppSettings::applyTheme(ThemeMode mode)
{
    // Captured on the first call (always at startup, before any override
    // is applied), so "System" can restore the platform's real default
    // style rather than whatever we last switched to.
    static const QString systemStyleName = QApplication::style()->objectName();

    // Also hint the generic Qt color scheme, for platform integrations
    // (native Windows/macOS styles, some GTK/portal-backed dialogs) that
    // do honor it, even though Fusion below is what does the real work.
    QGuiApplication::styleHints()->setColorScheme(
        mode == ThemeMode::Light ? Qt::ColorScheme::Light
        : mode == ThemeMode::Dark ? Qt::ColorScheme::Dark
                                   : Qt::ColorScheme::Unknown);

    if (mode == ThemeMode::System) {
        QApplication::setStyle(QStyleFactory::create(systemStyleName));
        QApplication::setPalette(QApplication::style()->standardPalette());
        return;
    }

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(mode == ThemeMode::Dark ? darkFusionPalette()
                                                       : QApplication::style()->standardPalette());
}
