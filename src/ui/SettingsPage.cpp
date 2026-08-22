#include "SettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "PackageFormatInstallWidget.h"

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    auto *appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceGroup);

    m_themeCombo = new QComboBox(this);
    // Index order matches ThemeMode's declaration order (System, Light,
    // Dark) so the combo index can be cast directly to/from the enum.
    m_themeCombo->addItem(tr("Follow System"));
    m_themeCombo->addItem(tr("Light"));
    m_themeCombo->addItem(tr("Dark"));
    m_themeCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().themeMode()));

    appearanceForm->addRow(tr("Theme:"), m_themeCombo);

    m_compactListsCheck = new QCheckBox(tr("Use compact rows in package lists"), this);
    m_compactListsCheck->setChecked(AppSettings::instance().compactPackageLists());
    appearanceForm->addRow(m_compactListsCheck);

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalForm = new QFormLayout(generalGroup);

    m_startupTabCombo = new QComboBox(this);
    // Index order matches StartupTab's declaration order (System, Flatpak, Snap).
    m_startupTabCombo->addItem(tr("System"));
    m_startupTabCombo->addItem(tr("Flatpak"));
    m_startupTabCombo->addItem(tr("Snap"));
    m_startupTabCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().startupTab()));
    generalForm->addRow(tr("Start on tab:"), m_startupTabCombo);

    m_autoCloseCheck = new QCheckBox(tr("Automatically close the results window after a successful "
                                         "install, uninstall, or reinstall"),
                                      this);
    m_autoCloseCheck->setChecked(AppSettings::instance().autoCloseTerminalOnSuccess());
    generalForm->addRow(m_autoCloseCheck);

    m_checkForUpdatesCheck = new QCheckBox(tr("Check for Athena updates on startup"), this);
    m_checkForUpdatesCheck->setChecked(AppSettings::instance().checkForAppUpdatesOnStartup());
    generalForm->addRow(m_checkForUpdatesCheck);

    auto *formatsGroup = new QGroupBox(tr("Additional Package Formats"), this);
    auto *formatsLayout = new QVBoxLayout(formatsGroup);
    formatsLayout->addWidget(new PackageFormatInstallWidget(formatsGroup));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(appearanceGroup);
    layout->addWidget(generalGroup);
    layout->addWidget(formatsGroup);
    layout->addStretch(1);

    auto *signatureLabel = new QLabel(tr("- niruxxdaboi -"), this);
    signatureLabel->setAlignment(Qt::AlignCenter);
    // Plain default text color (not a dimmed palette role): some native
    // dark themes (e.g. KDE Breeze Dark) make QPalette::Mid nearly the
    // same tone as the window background, which made this unreadable.
    QFont signatureFont = signatureLabel->font();
    signatureFont.setItalic(true);
    signatureLabel->setFont(signatureFont);
    layout->addWidget(signatureLabel);

    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &SettingsPage::onThemeIndexChanged);
    connect(m_startupTabCombo, &QComboBox::currentIndexChanged, this, &SettingsPage::onStartupTabIndexChanged);
    connect(m_autoCloseCheck, &QCheckBox::toggled, this, &SettingsPage::onAutoCloseToggled);
    connect(m_checkForUpdatesCheck, &QCheckBox::toggled, this, &SettingsPage::onCheckForUpdatesToggled);
    connect(m_compactListsCheck, &QCheckBox::toggled, this, &SettingsPage::onCompactListsToggled);
}

void SettingsPage::onThemeIndexChanged(int index)
{
    AppSettings::instance().setThemeMode(static_cast<ThemeMode>(index));
}

void SettingsPage::onStartupTabIndexChanged(int index)
{
    AppSettings::instance().setStartupTab(static_cast<StartupTab>(index));
}

void SettingsPage::onAutoCloseToggled(bool checked)
{
    AppSettings::instance().setAutoCloseTerminalOnSuccess(checked);
}

void SettingsPage::onCheckForUpdatesToggled(bool checked)
{
    AppSettings::instance().setCheckForAppUpdatesOnStartup(checked);
}

void SettingsPage::onCompactListsToggled(bool checked)
{
    AppSettings::instance().setCompactPackageLists(checked);
}
