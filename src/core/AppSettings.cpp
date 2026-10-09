#include "AppSettings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "../ui/AthenaStyle.h"

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
const QString kShowArchitectureColumnKey = QStringLiteral("appearance/showArchitectureColumn");
const QString kShowSizeColumnKey = QStringLiteral("appearance/showSizeColumn");
const QString kAutoConfirmTransactionsKey = QStringLiteral("system/autoConfirmTransactions");
const QString kHideTrayWhenNoUpdatesKey = QStringLiteral("system/hideTrayWhenNoUpdates");
const QString kUpdateCheckIntervalKey = QStringLiteral("system/updateCheckIntervalMinutes");
const QString kMetadataExpireHoursKey = QStringLiteral("system/metadataExpireHours");
const QString kDisableGroupViewKey = QStringLiteral("layout/disableGroupView");
const QString kReviewAurBuildsKey = QStringLiteral("aur/reviewBuilds");
const QString kLoggingEnabledKey = QStringLiteral("logging/enabled");
const QString kLogDirectoryKey = QStringLiteral("logging/directory");
const QString kLogLevelKey = QStringLiteral("logging/level");

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
    if (value == static_cast<int>(StartupTab::Aur))
        return StartupTab::Aur;
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

bool AppSettings::showArchitectureColumn() const
{
    QSettings settings;
    return settings.value(kShowArchitectureColumnKey, false).toBool();
}

void AppSettings::setShowArchitectureColumn(bool show)
{
    QSettings settings;
    settings.setValue(kShowArchitectureColumnKey, show);
}

bool AppSettings::showSizeColumn() const
{
    QSettings settings;
    return settings.value(kShowSizeColumnKey, false).toBool();
}

void AppSettings::setShowSizeColumn(bool show)
{
    QSettings settings;
    settings.setValue(kShowSizeColumnKey, show);
}

bool AppSettings::autoConfirmTransactions() const
{
    QSettings settings;
    return settings.value(kAutoConfirmTransactionsKey, false).toBool();
}

void AppSettings::setAutoConfirmTransactions(bool autoConfirm)
{
    QSettings settings;
    settings.setValue(kAutoConfirmTransactionsKey, autoConfirm);
}

bool AppSettings::hideTrayWhenNoUpdates() const
{
    QSettings settings;
    return settings.value(kHideTrayWhenNoUpdatesKey, true).toBool();
}

void AppSettings::setHideTrayWhenNoUpdates(bool hide)
{
    QSettings settings;
    settings.setValue(kHideTrayWhenNoUpdatesKey, hide);
}

int AppSettings::updateCheckIntervalMinutes() const
{
    QSettings settings;
    const int value = settings.value(kUpdateCheckIntervalKey, 60).toInt();
    return value > 0 ? value : 60;
}

void AppSettings::setUpdateCheckIntervalMinutes(int minutes)
{
    QSettings settings;
    settings.setValue(kUpdateCheckIntervalKey, minutes);
    emit updateCheckIntervalMinutesChanged(minutes);
}

int AppSettings::metadataExpireHours() const
{
    QSettings settings;
    const int value = settings.value(kMetadataExpireHoursKey, 24).toInt();
    return value > 0 ? value : 24;
}

void AppSettings::setMetadataExpireHours(int hours)
{
    QSettings settings;
    settings.setValue(kMetadataExpireHoursKey, hours);
}

bool AppSettings::disableGroupView() const
{
    QSettings settings;
    return settings.value(kDisableGroupViewKey, false).toBool();
}

void AppSettings::setDisableGroupView(bool disabled)
{
    QSettings settings;
    settings.setValue(kDisableGroupViewKey, disabled);
    emit disableGroupViewChanged(disabled);
}

bool AppSettings::reviewAurBuilds() const
{
    QSettings settings;
    return settings.value(kReviewAurBuildsKey, true).toBool();
}

void AppSettings::setReviewAurBuilds(bool review)
{
    QSettings settings;
    settings.setValue(kReviewAurBuildsKey, review);
}

bool AppSettings::loggingEnabled() const
{
    QSettings settings;
    return settings.value(kLoggingEnabledKey, false).toBool();
}

void AppSettings::setLoggingEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(kLoggingEnabledKey, enabled);
}

QString AppSettings::logDirectory() const
{
    QSettings settings;
    const QString value = settings.value(kLogDirectoryKey).toString();
    if (!value.isEmpty())
        return value;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
}

void AppSettings::setLogDirectory(const QString &dir)
{
    QSettings settings;
    settings.setValue(kLogDirectoryKey, dir);
}

LogLevel AppSettings::logLevel() const
{
    QSettings settings;
    const int value = settings.value(kLogLevelKey, static_cast<int>(LogLevel::Info)).toInt();
    if (value == static_cast<int>(LogLevel::Error))
        return LogLevel::Error;
    if (value == static_cast<int>(LogLevel::Warning))
        return LogLevel::Warning;
    if (value == static_cast<int>(LogLevel::Debug))
        return LogLevel::Debug;
    return LogLevel::Info;
}

void AppSettings::setLogLevel(LogLevel level)
{
    QSettings settings;
    settings.setValue(kLogLevelKey, static_cast<int>(level));
}

void AppSettings::applyTheme(ThemeMode mode)
{
    // Captured on the first call (always at startup, before any override
    // is applied), so "System" can restore the platform's real default
    // style and icon theme rather than whatever we last switched to.
    static const QString systemStyleName = QApplication::style()->objectName();
    static const QString systemIconTheme = QIcon::themeName();

    // Breeze paints the menu bar/header area from a KDE color scheme file
    // rather than QPalette; this property is how KDE apps (via
    // KColorSchemeManager) point it at a different scheme than the
    // desktop's. Empty means "the desktop's own".
    auto setKdeColorScheme = [](const QString &schemeFile) {
        const QString path = schemeFile.isEmpty()
            ? QString()
            : QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                     QStringLiteral("color-schemes/%1").arg(schemeFile));
        qApp->setProperty("KDE_COLOR_SCHEME_PATH", path.isEmpty() ? QVariant() : QVariant(path));
    };

    // Breeze's icon theme ships separate light-on-dark and dark-on-light
    // variants; follow the forced mode when the desktop uses either.
    auto setBreezeIconVariant = [](bool dark) {
        if (!systemIconTheme.startsWith(QLatin1String("breeze")))
            return;
        const QString wanted = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
        if (!QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("icons/%1").arg(wanted),
                                    QStandardPaths::LocateDirectory)
                 .isEmpty())
            QIcon::setThemeName(wanted);
    };

    // Also hint the generic Qt color scheme, for platform integrations
    // (native Windows/macOS styles, some GTK/portal-backed dialogs) that
    // do honor it, even though the explicit palette below does the real work.
    QGuiApplication::styleHints()->setColorScheme(
        mode == ThemeMode::Light ? Qt::ColorScheme::Light
        : mode == ThemeMode::Dark ? Qt::ColorScheme::Dark
                                   : Qt::ColorScheme::Unknown);

    if (mode == ThemeMode::System) {
        // Qt's bare "Windows" style is what you get with no platform theme
        // at all (no desktop integration plugin) — not a real choice
        // anyone made, so prefer Breeze/Fusion over it.
        const QString styleName = systemStyleName.compare(QLatin1String("windows"), Qt::CaseInsensitive) == 0
            ? AthenaStyle::preferredStyleName()
            : systemStyleName;
        setKdeColorScheme(QString());
        QIcon::setThemeName(systemIconTheme);
        QApplication::setStyle(new AthenaStyle(QStyleFactory::create(styleName)));
        // An empty palette resets to the platform theme's own (e.g. the
        // user's KDE color scheme) instead of pinning the style's default.
        QApplication::setPalette(QPalette());
        return;
    }

    // Native styles mostly ignore setColorScheme and take colors from the
    // desktop's config instead, so a forced Light/Dark mode uses Breeze (or
    // Fusion) — both paint strictly from QPalette — with an explicit one.
    const bool dark = mode == ThemeMode::Dark;
    setKdeColorScheme(dark ? QStringLiteral("BreezeDark.colors") : QStringLiteral("BreezeLight.colors"));
    setBreezeIconVariant(dark);
    QApplication::setStyle(new AthenaStyle(QStyleFactory::create(AthenaStyle::preferredStyleName())));
    QApplication::setPalette(mode == ThemeMode::Dark ? AthenaStyle::breezeDarkPalette()
                                                       : AthenaStyle::breezeLightPalette());
}
