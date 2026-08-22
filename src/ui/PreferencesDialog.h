#pragma once

#include <QDialog>

// Settings/Preferences moved out of the main window's tab rotation (which
// is now just the System/Flatpak/Snap groups, switched via a top-right
// dropdown) into its own dialog, reachable from Settings > Preferences —
// just wraps the existing SettingsPage content with a Close button.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
};
