#include "AurHelperInstallWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "../core/ProcessRunner.h"
#include "../core/backends/AurBackend.h"
#include "AppIcons.h"
#include "AurReviewDialog.h"
#include "TerminalOutputDialog.h"
#include "TransactionConfirmDialog.h"

AurHelperInstallWidget::AurHelperInstallWidget(QWidget *parent) : QWidget(parent)
{
    m_reviewCheck = new QCheckBox(tr("Review and edit PKGBUILDs before building AUR packages (recommended)"), this);
    m_reviewCheck->setChecked(AppSettings::instance().reviewAurBuilds());

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_helperCombo = new QComboBox(this);
    // Item data is the helper's package/executable name.
    m_helperCombo->addItem(tr("yay"), QStringLiteral("yay"));
    m_helperCombo->setItemData(0, tr("Written in Go; builds quickly."), Qt::ToolTipRole);
    m_helperCombo->addItem(tr("paru"), QStringLiteral("paru"));
    m_helperCombo->setItemData(1, tr("Written in Rust; takes a few minutes to compile."), Qt::ToolTipRole);

    m_installButton = new QPushButton(AppIcons::install(), QString(), this);
    m_uninstallButton = new QPushButton(AppIcons::uninstall(), QString(), this);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("AUR helper:"), this));
    row->addWidget(m_helperCombo);
    row->addWidget(m_installButton);
    row->addWidget(m_uninstallButton);
    row->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_reviewCheck);
    layout->addWidget(m_statusLabel);
    layout->addLayout(row);

    connect(m_helperCombo, &QComboBox::currentIndexChanged, this, &AurHelperInstallWidget::refreshState);
    connect(m_installButton, &QPushButton::clicked, this, &AurHelperInstallWidget::runInstall);
    connect(m_uninstallButton, &QPushButton::clicked, this, &AurHelperInstallWidget::runUninstall);
    connect(m_reviewCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            const auto reply = QMessageBox::warning(
                this, tr("Turn Off PKGBUILD Review?"),
                tr("AUR packages will then be built and installed without you seeing their build scripts "
                   "first. Anyone who can push to an AUR package you install could run code on your "
                   "machine.\n\nTurn review off anyway?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                const QSignalBlocker blocker(m_reviewCheck);
                m_reviewCheck->setChecked(true);
                return;
            }
        }
        AppSettings::instance().setReviewAurBuilds(checked);
        refreshState();
    });

    // Preselect whichever helper is already installed, if any, so the
    // button reads "Reinstall" for it rather than offering the other one.
    if (AurBackend::buildMethod() == AurBackend::BuildMethod::Paru)
        m_helperCombo->setCurrentIndex(1);
    refreshState();
}

void AurHelperInstallWidget::refreshState()
{
    m_statusLabel->setText(
        AurBackend::reviewRequired()
            ? tr("PKGBUILD review is on: Athena builds each reviewed copy itself with makepkg, so an AUR helper "
                 "is only used when review is turned off.")
            : tr("AUR packages are currently built with: %1")
                  .arg(AurBackend::buildMethodName(AurBackend::buildMethod())));

    const QString helper = m_helperCombo->currentData().toString();
    const bool installed = ProcessRunner::executableExists(helper);
    m_installButton->setText(installed ? tr("Reinstall %1").arg(helper) : tr("Install %1").arg(helper));

    // Only offered when pacman actually owns the binary — a copy dropped
    // into ~/.local/bin by hand isn't something pacman can remove.
    const QString package = installed ? AurBackend::helperOwningPackage(helper) : QString();
    m_uninstallButton->setVisible(!package.isEmpty());
    m_uninstallButton->setText(tr("Uninstall %1").arg(package));
}

void AurHelperInstallWidget::runUninstall()
{
    const QString helper = m_helperCombo->currentData().toString();
    const QString package = AurBackend::helperOwningPackage(helper);
    const QVector<ProcessRunner::Command> commands = AurBackend::helperRemoveCommands(helper);
    if (commands.isEmpty()) {
        refreshState();
        return;
    }

    QStringList commandLines;
    for (const ProcessRunner::Command &command : commands)
        commandLines << QStringLiteral("%1 %2").arg(command.program, command.args.join(' '));

    const QString title = tr("Uninstall %1").arg(package);
    const QString summary =
        tr("Uninstall %1, plus any of its dependencies nothing else needs?\n\nAthena keeps AUR support either "
           "way: without a helper it builds AUR packages itself with makepkg.")
            .arg(package);
    if (!TransactionConfirmDialog::confirm(this, title, summary, commandLines.join('\n')))
        return;

    TerminalOutputDialog dialog(title, this);
    dialog.runCommandsModal(commands, false);
    refreshState();
}

void AurHelperInstallWidget::runInstall()
{
    const QString helper = m_helperCombo->currentData().toString();
    if (AurBackend::reviewRequired()) {
        runReviewedInstall(helper);
        return;
    }

    const QVector<ProcessRunner::Command> commands = AurBackend::helperInstallCommands(helper);
    if (commands.isEmpty())
        return;

    QStringList commandLines;
    for (const ProcessRunner::Command &command : commands) {
        const QString line = QStringLiteral("%1 %2").arg(command.program, command.args.join(' '));
        commandLines << (command.workingDirectory.isEmpty()
                             ? line
                             : QStringLiteral("(in %1) %2").arg(command.workingDirectory, line));
    }

    const QString title = tr("Install %1").arg(helper);
    const QString summary =
        tr("Build and install %1 from its official AUR repository (https://aur.archlinux.org/%1.git)?\n\n"
           "This installs git and base-devel if they're missing, then builds %1 with makepkg as your own "
           "user. Its build tools are installed along the way.")
            .arg(helper);
    if (!TransactionConfirmDialog::confirm(this, title, summary, commandLines.join('\n')))
        return;

    TerminalOutputDialog dialog(title, this);
    // Never auto-close: a bootstrap build like this is worth seeing finish.
    const bool success = dialog.runCommandsModal(commands, false);

    refreshState();

    if (success) {
        QMessageBox::information(this, title,
                                 tr("%1 is installed. The AUR tab will use it for builds from now on.").arg(helper));
    }
}

void AurHelperInstallWidget::runReviewedInstall(const QString &helper)
{
    const QString title = tr("Install %1").arg(helper);

    // Fetching the build files for review needs git, and building needs
    // base-devel — install whichever is missing first.
    const ProcessRunner::Result missing = ProcessRunner::run("pacman", {"-T", "git", "base-devel"}, 15000);
    const QStringList missingNames = missing.stdOut.split('\n', Qt::SkipEmptyParts);
    if (!missingNames.isEmpty()) {
        if (!TransactionConfirmDialog::confirm(
                this, title, tr("Building %1 needs %2 first. Install from the official repositories now?")
                                 .arg(helper, missingNames.join(", "))))
            return;
        TerminalOutputDialog dialog(title, this);
        QStringList args = {"pacman", "-S", "--needed", "--noconfirm"};
        if (!dialog.runCommandsModal({{"pkexec", args + missingNames}}, true))
            return;
    }

    AurReviewDialog::reviewAndBuild(this, {helper}, ProcessRunner::executableExists(helper),
                                    [this, title, helper](bool success) {
                                        refreshState();
                                        if (success) {
                                            QMessageBox::information(
                                                this, title,
                                                tr("%1 is installed. It's used for AUR builds whenever PKGBUILD "
                                                   "review is turned off.")
                                                    .arg(helper));
                                        }
                                    });
}
