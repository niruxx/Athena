#pragma once

#include <QObject>

enum class ThemeMode { System, Light, Dark };
enum class StartupTab { System, Flatpak, Snap };
enum class LogLevel { Error, Warning, Info, Debug };

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

    // Whether to check GitHub for a newer Athena release at startup
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

    // Whether the Architecture/Size columns are shown in package list
    // tables. Off by default to match the table's existing appearance;
    // toggled from the table's own header context menu.
    bool showArchitectureColumn() const;
    void setShowArchitectureColumn(bool show);
    bool showSizeColumn() const;
    void setShowSizeColumn(bool show);

    // Whether install/remove/reinstall/update transactions apply
    // immediately once requested, skipping the "are you sure" confirmation
    // dialog (including the tray's "Update" action). Off by default since
    // it removes the last check before a privileged transaction runs.
    bool autoConfirmTransactions() const;
    void setAutoConfirmTransactions(bool autoConfirm);

    // Whether the tray icon hides itself when there are no pending
    // updates, rather than staying visible at all times.
    bool hideTrayWhenNoUpdates() const;
    void setHideTrayWhenNoUpdates(bool hide);

    // How often (in minutes) the tray polls every active backend for
    // available updates.
    int updateCheckIntervalMinutes() const;
    void setUpdateCheckIntervalMinutes(int minutes);

    // How long (in hours) a backend's repository metadata cache is
    // considered fresh before it's treated as stale. Currently only
    // honored by the DNF backend, via dnf5's own --setopt=metadata_expire.
    int metadataExpireHours() const;
    void setMetadataExpireHours(int hours);

    // Whether the "Groups" (category browser) tab is hidden from the
    // System group's tab bar.
    bool disableGroupView() const;
    void setDisableGroupView(bool disabled);

    // Whether application log messages are appended to a file on disk.
    bool loggingEnabled() const;
    void setLoggingEnabled(bool enabled);

    // Directory the log file is written into. Defaults to a directory
    // under the app's standard data location if never explicitly set.
    QString logDirectory() const;
    void setLogDirectory(const QString &dir);

    // Minimum severity a message must have to be written to the log file.
    LogLevel logLevel() const;
    void setLogLevel(LogLevel level);

signals:
    void themeModeChanged(ThemeMode mode);
    void compactPackageListsChanged(bool compact);
    void disableGroupViewChanged(bool disabled);
    void updateCheckIntervalMinutesChanged(int minutes);

private:
    AppSettings();

    static void applyTheme(ThemeMode mode);
};
