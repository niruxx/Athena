#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// The AUR section of Preferences: the PKGBUILD-review toggle, plus a row
// for installing an AUR helper (yay or paru) straight from
// its official AUR git repo via makepkg -si, so the AUR group can hand
// builds to a full helper instead of Athena's built-in makepkg fallback.
// Only meaningful on Arch; SettingsPage doesn't add it anywhere else.
class AurHelperInstallWidget : public QWidget {
    Q_OBJECT

public:
    explicit AurHelperInstallWidget(QWidget *parent = nullptr);

private:
    void refreshState();
    void runInstall();
    // With PKGBUILD review on, the helper itself is built through the same
    // review dialog as any other AUR package.
    void runReviewedInstall(const QString &helper);
    void runUninstall();

    QCheckBox *m_reviewCheck;
    QLabel *m_statusLabel;
    QComboBox *m_helperCombo;
    QPushButton *m_installButton;
    QPushButton *m_uninstallButton;
};
