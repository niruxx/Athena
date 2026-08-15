#pragma once

#include <QObject>

enum class ThemeMode { System, Light, Dark };
enum class StartupTab { System, Flatpak, Snap };

// Thin QSettings wrapper for the app's persisted preferences. Singleton
// since there's exactly one settings store per process and multiple UI
// widgets (SettingsPage, MainWindow at startup) need to read/write it.
class AppSettings : public QObject {
    Q_OBJECT

public:
    static AppSettings &instance();

    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    // Applies the currently-persisted theme to the running application.
    // Call once at startup, before showing any windows.
    void applyCurrentTheme() const;

    // Which top-level tab (System/Flatpak/Snap) is active when the app opens.
    StartupTab startupTab() const;
    void setStartupTab(StartupTab tab);

    // Whether the install/uninstall/reinstall terminal output window
    // closes itself automatically once the operation succeeds, instead of
    // waiting for the user to dismiss it.
    bool autoCloseTerminalOnSuccess() const;
    void setAutoCloseTerminalOnSuccess(bool autoClose);

    // Whether to check GitHub for a newer distore-qt release at startup
    // and show the update banner if one is found.
    bool checkForAppUpdatesOnStartup() const;
    void setCheckForAppUpdatesOnStartup(bool check);

    // Tighter row height in package list tables, for fitting more on
    // screen at once.
    bool compactPackageLists() const;
    void setCompactPackageLists(bool compact);

    // Whether the first-run welcome dialog has already been shown (and
    // should stay hidden from now on).
    bool hasCompletedFirstRun() const;
    void setHasCompletedFirstRun(bool completed);

signals:
    void themeModeChanged(ThemeMode mode);
    void compactPackageListsChanged(bool compact);

private:
    AppSettings();

    static void applyTheme(ThemeMode mode);
};
