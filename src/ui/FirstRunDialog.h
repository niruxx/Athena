#pragma once

#include <QDialog>

// Shown once, the first time the app is launched (AppSettings::
// hasCompletedFirstRun() gates it): a short welcome, what native backend
// was detected, and the same Flatpak/Snap install offer as the Settings
// page, so a fresh install can get fully set up without hunting for it.
class FirstRunDialog : public QDialog {
    Q_OBJECT

public:
    explicit FirstRunDialog(const QString &detectedBackendName, QWidget *parent = nullptr);
};
