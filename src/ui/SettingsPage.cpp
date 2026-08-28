#include "SettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "AppIcons.h"
#include "PackageFormatInstallWidget.h"

namespace {
QWidget *buildGeneralTab(QWidget *parent, QComboBox *&themeCombo, QCheckBox *&compactListsCheck,
                          QComboBox *&startupTabCombo, QCheckBox *&autoCloseCheck, QCheckBox *&checkForUpdatesCheck)
{
    auto *tab = new QWidget(parent);

    auto *appearanceGroup = new QGroupBox(QObject::tr("Appearance"), tab);
    auto *appearanceForm = new QFormLayout(appearanceGroup);

    themeCombo = new QComboBox(tab);
    // Index order matches ThemeMode's declaration order (System, Light,
    // Dark) so the combo index can be cast directly to/from the enum.
    themeCombo->addItem(QObject::tr("Follow System"));
    themeCombo->addItem(QObject::tr("Light"));
    themeCombo->addItem(QObject::tr("Dark"));
    themeCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().themeMode()));
    appearanceForm->addRow(QObject::tr("Theme:"), themeCombo);

    compactListsCheck = new QCheckBox(QObject::tr("Use compact rows in package lists"), tab);
    compactListsCheck->setChecked(AppSettings::instance().compactPackageLists());
    appearanceForm->addRow(compactListsCheck);

    auto *generalGroup = new QGroupBox(QObject::tr("General"), tab);
    auto *generalForm = new QFormLayout(generalGroup);

    startupTabCombo = new QComboBox(tab);
    // Index order matches StartupTab's declaration order (System, Flatpak, Snap).
    startupTabCombo->addItem(QObject::tr("System"));
    startupTabCombo->addItem(QObject::tr("Flatpak"));
    startupTabCombo->addItem(QObject::tr("Snap"));
    startupTabCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().startupTab()));
    generalForm->addRow(QObject::tr("Start on tab:"), startupTabCombo);

    autoCloseCheck = new QCheckBox(QObject::tr("Automatically close the results window after a successful "
                                                "install, uninstall, or reinstall"),
                                    tab);
    autoCloseCheck->setChecked(AppSettings::instance().autoCloseTerminalOnSuccess());
    generalForm->addRow(autoCloseCheck);

    checkForUpdatesCheck = new QCheckBox(QObject::tr("Check for Athena updates on startup"), tab);
    checkForUpdatesCheck->setChecked(AppSettings::instance().checkForAppUpdatesOnStartup());
    generalForm->addRow(checkForUpdatesCheck);

    auto *formatsGroup = new QGroupBox(QObject::tr("Additional Package Formats"), tab);
    auto *formatsLayout = new QVBoxLayout(formatsGroup);
    formatsLayout->addWidget(new PackageFormatInstallWidget(formatsGroup));

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(appearanceGroup);
    layout->addWidget(generalGroup);
    layout->addWidget(formatsGroup);
    layout->addStretch(1);

    return tab;
}

QWidget *buildSystemTab(QWidget *parent, QCheckBox *&autoConfirmCheck, QCheckBox *&hideTrayCheck,
                         QSpinBox *&updateIntervalSpin, QSpinBox *&metadataExpireSpin)
{
    auto *tab = new QWidget(parent);

    auto *transactionsGroup = new QGroupBox(QObject::tr("Transactions"), tab);
    auto *transactionsLayout = new QVBoxLayout(transactionsGroup);
    autoConfirmCheck =
        new QCheckBox(QObject::tr("Run transactions on packages automatically without confirmation needed"), tab);
    autoConfirmCheck->setChecked(AppSettings::instance().autoConfirmTransactions());
    transactionsLayout->addWidget(autoConfirmCheck);

    auto *trayGroup = new QGroupBox(QObject::tr("Tray Icon"), tab);
    auto *trayLayout = new QVBoxLayout(trayGroup);
    hideTrayCheck = new QCheckBox(QObject::tr("Hide traybar when no updates are present"), tab);
    hideTrayCheck->setChecked(AppSettings::instance().hideTrayWhenNoUpdates());
    trayLayout->addWidget(hideTrayCheck);

    auto *updatesGroup = new QGroupBox(QObject::tr("Update Checking"), tab);
    auto *updatesForm = new QFormLayout(updatesGroup);

    updateIntervalSpin = new QSpinBox(tab);
    updateIntervalSpin->setRange(1, 24 * 60);
    updateIntervalSpin->setSuffix(QObject::tr(" min"));
    updateIntervalSpin->setValue(AppSettings::instance().updateCheckIntervalMinutes());
    updatesForm->addRow(QObject::tr("Interval to check for updates:"), updateIntervalSpin);

    metadataExpireSpin = new QSpinBox(tab);
    metadataExpireSpin->setRange(1, 24 * 30);
    metadataExpireSpin->setSuffix(QObject::tr(" hr"));
    metadataExpireSpin->setValue(AppSettings::instance().metadataExpireHours());
    updatesForm->addRow(QObject::tr("Metadata expire time:"), metadataExpireSpin);

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(transactionsGroup);
    layout->addWidget(trayGroup);
    layout->addWidget(updatesGroup);
    layout->addStretch(1);

    return tab;
}

QWidget *buildLayoutTab(QWidget *parent, QCheckBox *&disableGroupViewCheck)
{
    auto *tab = new QWidget(parent);

    auto *browsingGroup = new QGroupBox(QObject::tr("Package Browsing"), tab);
    auto *browsingLayout = new QVBoxLayout(browsingGroup);
    disableGroupViewCheck = new QCheckBox(QObject::tr("Disable Group view"), tab);
    disableGroupViewCheck->setChecked(AppSettings::instance().disableGroupView());
    browsingLayout->addWidget(disableGroupViewCheck);

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(browsingGroup);
    layout->addStretch(1);

    return tab;
}

QWidget *buildLoggingTab(QWidget *parent, QCheckBox *&loggingEnabledCheck, QLineEdit *&logDirectoryEdit,
                          QPushButton *&browseButton, QComboBox *&logLevelCombo)
{
    auto *tab = new QWidget(parent);

    auto *loggingGroup = new QGroupBox(QObject::tr("Logging"), tab);
    auto *loggingLayout = new QVBoxLayout(loggingGroup);

    loggingEnabledCheck = new QCheckBox(QObject::tr("Enable logging"), tab);
    loggingEnabledCheck->setChecked(AppSettings::instance().loggingEnabled());
    loggingLayout->addWidget(loggingEnabledCheck);

    auto *directoryForm = new QFormLayout;
    auto *directoryRow = new QHBoxLayout;
    logDirectoryEdit = new QLineEdit(tab);
    logDirectoryEdit->setReadOnly(true);
    logDirectoryEdit->setText(AppSettings::instance().logDirectory());
    browseButton = new QPushButton(AppIcons::folder(), QString(), tab);
    browseButton->setToolTip(QObject::tr("Choose log folder..."));
    directoryRow->addWidget(logDirectoryEdit, 1);
    directoryRow->addWidget(browseButton);
    directoryForm->addRow(QObject::tr("Log folder:"), directoryRow);

    logLevelCombo = new QComboBox(tab);
    // Index order matches LogLevel's declaration order (Error, Warning, Info, Debug).
    logLevelCombo->addItem(QObject::tr("Error"));
    logLevelCombo->addItem(QObject::tr("Warning"));
    logLevelCombo->addItem(QObject::tr("Info"));
    logLevelCombo->addItem(QObject::tr("Debug"));
    logLevelCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().logLevel()));
    directoryForm->addRow(QObject::tr("Log level:"), logLevelCombo);

    loggingLayout->addLayout(directoryForm);

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(loggingGroup);
    layout->addStretch(1);

    return tab;
}
} // namespace

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    auto *tabs = new QTabWidget(this);

    QPushButton *browseLogButton = nullptr;
    tabs->addTab(buildGeneralTab(tabs, m_themeCombo, m_compactListsCheck, m_startupTabCombo, m_autoCloseCheck,
                                  m_checkForUpdatesCheck),
                 tr("General"));
    tabs->addTab(buildSystemTab(tabs, m_autoConfirmCheck, m_hideTrayCheck, m_updateIntervalSpin,
                                 m_metadataExpireSpin),
                 tr("System"));
    tabs->addTab(buildLayoutTab(tabs, m_disableGroupViewCheck), tr("Layout"));
    tabs->addTab(buildLoggingTab(tabs, m_loggingEnabledCheck, m_logDirectoryEdit, browseLogButton, m_logLevelCombo),
                 tr("Logging Options"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);

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

    connect(m_autoConfirmCheck, &QCheckBox::toggled, this, &SettingsPage::onAutoConfirmToggled);
    connect(m_hideTrayCheck, &QCheckBox::toggled, this, &SettingsPage::onHideTrayToggled);
    connect(m_updateIntervalSpin, &QSpinBox::valueChanged, this, &SettingsPage::onUpdateIntervalChanged);
    connect(m_metadataExpireSpin, &QSpinBox::valueChanged, this, &SettingsPage::onMetadataExpireChanged);

    connect(m_disableGroupViewCheck, &QCheckBox::toggled, this, &SettingsPage::onDisableGroupViewToggled);

    connect(m_loggingEnabledCheck, &QCheckBox::toggled, this, &SettingsPage::onLoggingEnabledToggled);
    connect(browseLogButton, &QPushButton::clicked, this, &SettingsPage::onBrowseLogDirectory);
    connect(m_logLevelCombo, &QComboBox::currentIndexChanged, this, &SettingsPage::onLogLevelIndexChanged);
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

void SettingsPage::onAutoConfirmToggled(bool checked)
{
    AppSettings::instance().setAutoConfirmTransactions(checked);
}

void SettingsPage::onHideTrayToggled(bool checked)
{
    AppSettings::instance().setHideTrayWhenNoUpdates(checked);
}

void SettingsPage::onUpdateIntervalChanged(int minutes)
{
    AppSettings::instance().setUpdateCheckIntervalMinutes(minutes);
}

void SettingsPage::onMetadataExpireChanged(int hours)
{
    AppSettings::instance().setMetadataExpireHours(hours);
}

void SettingsPage::onDisableGroupViewToggled(bool checked)
{
    AppSettings::instance().setDisableGroupView(checked);
}

void SettingsPage::onLoggingEnabledToggled(bool checked)
{
    AppSettings::instance().setLoggingEnabled(checked);
}

void SettingsPage::onBrowseLogDirectory()
{
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Choose Log Folder"), m_logDirectoryEdit->text());
    if (chosen.isEmpty())
        return;

    m_logDirectoryEdit->setText(chosen);
    AppSettings::instance().setLogDirectory(chosen);
}

void SettingsPage::onLogLevelIndexChanged(int index)
{
    AppSettings::instance().setLogLevel(static_cast<LogLevel>(index));
}
