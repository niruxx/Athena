#pragma once

#include <QDialog>

// Shown once, the first time the app is launched (AppSettings::
// hasCompletedFirstRun() gates it) — and reopenable anytime afterward via
// Preferences → General → "Run First-Time Setup Again": a short welcome,
// what native backend was detected, a handful of the most immediately
// relevant settings (theme, startup tab, compact lists, update checks),
// and the same Flatpak/Snap install offer as the Settings page, so a
// fresh install can get fully set up without hunting through Preferences.
class FirstRunDialog : public QDialog {
    Q_OBJECT

public:
    explicit FirstRunDialog(const QString &detectedBackendName, QWidget *parent = nullptr);
};
