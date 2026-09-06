#include "FirstRunDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "PackageFormatInstallWidget.h"

FirstRunDialog::FirstRunDialog(const QString &detectedBackendName, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Athena"));
    setMinimumWidth(480);

    auto *titleLabel = new QLabel(tr("Welcome to Athena"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleLabel->setFont(titleFont);

    auto *introLabel = new QLabel(
        tr("Athena is a cross-distro GUI package manager: it drives your system's native package "
           "manager (DNF, APT, or Pacman), plus Flatpak and Snap side by side in the same window."),
        this);
    introLabel->setWordWrap(true);

    const QString backendText = !detectedBackendName.isEmpty()
        ? tr("Detected system package manager: %1").arg(detectedBackendName)
        : tr("No supported system package manager (DNF, APT, or Pacman) was detected.");
    auto *backendLabel = new QLabel(backendText, this);
    backendLabel->setWordWrap(true);
    QFont backendFont = backendLabel->font();
    backendFont.setItalic(true);
    backendLabel->setFont(backendFont);

    // Live-applies to AppSettings as each control changes (same as
    // SettingsPage), rather than deferring to an Apply/OK step — both
    // because it doubles as the "General" settings this dialog is meant
    // to save a trip to Preferences for, and because it's shown again
    // from Preferences itself (see SettingsPage's "Run First-Time Setup
    // Again" button), where every other control already applies live.
    auto *setupGroup = new QGroupBox(tr("Set Up Your Preferences"), this);
    auto *setupForm = new QFormLayout(setupGroup);

    auto *themeCombo = new QComboBox(this);
    // Index order matches ThemeMode's declaration order (System, Light, Dark).
    themeCombo->addItem(tr("Follow System"));
    themeCombo->addItem(tr("Light"));
    themeCombo->addItem(tr("Dark"));
    themeCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().themeMode()));
    setupForm->addRow(tr("Theme:"), themeCombo);

    auto *startupTabCombo = new QComboBox(this);
    // Index order matches StartupTab's declaration order (System, Flatpak, Snap).
    startupTabCombo->addItem(tr("System"));
    startupTabCombo->addItem(tr("Flatpak"));
    startupTabCombo->addItem(tr("Snap"));
    startupTabCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().startupTab()));
    setupForm->addRow(tr("Start on tab:"), startupTabCombo);

    auto *compactListsCheck = new QCheckBox(tr("Use compact rows in package lists"), this);
    compactListsCheck->setChecked(AppSettings::instance().compactPackageLists());
    setupForm->addRow(compactListsCheck);

    auto *checkForUpdatesCheck = new QCheckBox(tr("Check for Athena updates on startup"), this);
    checkForUpdatesCheck->setChecked(AppSettings::instance().checkForAppUpdatesOnStartup());
    setupForm->addRow(checkForUpdatesCheck);

    auto *formatsGroup = new QGroupBox(tr("Additional Package Formats"), this);
    auto *formatsLayout = new QVBoxLayout(formatsGroup);
    formatsLayout->addWidget(new PackageFormatInstallWidget(formatsGroup));

    auto *noteLabel = new QLabel(tr("You can change these, and more, anytime in Settings."), this);
    noteLabel->setWordWrap(true);
    QFont noteFont = noteLabel->font();
    noteFont.setItalic(true);
    noteLabel->setFont(noteFont);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Get Started"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(introLabel);
    layout->addWidget(backendLabel);
    layout->addWidget(setupGroup);
    layout->addWidget(formatsGroup);
    layout->addWidget(noteLabel);
    layout->addWidget(buttonBox);

    connect(themeCombo, &QComboBox::currentIndexChanged, this,
            [](int index) { AppSettings::instance().setThemeMode(static_cast<ThemeMode>(index)); });
    connect(startupTabCombo, &QComboBox::currentIndexChanged, this,
            [](int index) { AppSettings::instance().setStartupTab(static_cast<StartupTab>(index)); });
    connect(compactListsCheck, &QCheckBox::toggled, this,
            [](bool checked) { AppSettings::instance().setCompactPackageLists(checked); });
    connect(checkForUpdatesCheck, &QCheckBox::toggled, this,
            [](bool checked) { AppSettings::instance().setCheckForAppUpdatesOnStartup(checked); });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    // However it closes — the button, Escape, or the window's own close
    // control — it should never be shown again (on its own; it can still
    // be reopened deliberately from Preferences).
    connect(this, &QDialog::finished, this, []() { AppSettings::instance().setHasCompletedFirstRun(true); });
}
