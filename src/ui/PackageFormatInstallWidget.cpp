#include "PackageFormatInstallWidget.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include "../core/DistroSupport.h"
#include "TerminalOutputDialog.h"
#include "TransactionConfirmDialog.h"

PackageFormatInstallWidget::PackageFormatInstallWidget(QWidget *parent) : QWidget(parent)
{
    m_flatpakStatusLabel = new QLabel(this);
    m_flatpakInstallButton = new QPushButton(tr("Install Flatpak Support"), this);
    m_flatpakRemoveButton = new QPushButton(tr("Remove Flatpak Support"), this);
    m_snapStatusLabel = new QLabel(this);
    m_snapInstallButton = new QPushButton(tr("Install Snap Support"), this);
    m_snapRemoveButton = new QPushButton(tr("Remove Snap Support"), this);

    auto *flatpakRow = new QHBoxLayout;
    flatpakRow->addWidget(m_flatpakStatusLabel, 1);
    flatpakRow->addWidget(m_flatpakInstallButton);
    flatpakRow->addWidget(m_flatpakRemoveButton);

    auto *snapRow = new QHBoxLayout;
    snapRow->addWidget(m_snapStatusLabel, 1);
    snapRow->addWidget(m_snapInstallButton);
    snapRow->addWidget(m_snapRemoveButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(flatpakRow);
    layout->addLayout(snapRow);

    connect(m_flatpakInstallButton, &QPushButton::clicked, this, [this]() {
        runInstall(tr("Flatpak"), DistroSupport::flatpakInstallCommands());
    });
    connect(m_flatpakRemoveButton, &QPushButton::clicked, this, [this]() {
        runRemove(tr("Flatpak"), DistroSupport::flatpakRemoveCommands());
    });
    connect(m_snapInstallButton, &QPushButton::clicked, this,
            [this]() { runInstall(tr("Snap"), DistroSupport::snapInstallCommands()); });
    connect(m_snapRemoveButton, &QPushButton::clicked, this,
            [this]() { runRemove(tr("Snap"), DistroSupport::snapRemoveCommands()); });

    refreshState();
}

void PackageFormatInstallWidget::refreshState()
{
    const bool flatpakInstalled = DistroSupport::isFlatpakInstalled();
    m_flatpakStatusLabel->setText(flatpakInstalled ? tr("Flatpak: installed")
                                                     : tr("Flatpak: not installed"));
    m_flatpakInstallButton->setVisible(!flatpakInstalled);
    m_flatpakRemoveButton->setVisible(flatpakInstalled);

    const bool snapInstalled = DistroSupport::isSnapInstalled();
    m_snapStatusLabel->setText(snapInstalled ? tr("Snap: installed") : tr("Snap: not installed"));
    m_snapInstallButton->setVisible(!snapInstalled);
    m_snapRemoveButton->setVisible(snapInstalled);

    if (!snapInstalled && DistroSupport::snapInstallCommands().isEmpty()) {
        m_snapInstallButton->setEnabled(false);
        m_snapInstallButton->setToolTip(
            tr("Snap isn't available in this distribution's official repositories. Install an "
               "AUR helper (e.g. yay) and run: yay -S snapd"));
    } else {
        m_snapInstallButton->setEnabled(true);
        m_snapInstallButton->setToolTip(QString());
    }
}

void PackageFormatInstallWidget::runInstall(const QString &formatName,
                                             const QVector<ProcessRunner::Command> &commands)
{
    if (commands.isEmpty()) {
        QMessageBox::information(
            this, tr("Not Available"),
            tr("No known installation method for %1 on this distribution.").arg(formatName));
        return;
    }

    QStringList commandLines;
    for (const ProcessRunner::Command &command : commands)
        commandLines << QStringLiteral("%1 %2").arg(command.program, command.args.join(' '));

    const QString title = tr("Install %1").arg(formatName);
    if (!TransactionConfirmDialog::confirm(this, title, tr("Install %1 support?").arg(formatName),
                                            commandLines.join('\n')))
        return;

    runCommandsAndOfferRestart(title, commands,
                                tr("%1 support has been installed. Restart Distore now to start using it?")
                                    .arg(formatName));
}

void PackageFormatInstallWidget::runRemove(const QString &formatName,
                                            const QVector<ProcessRunner::Command> &commands)
{
    if (commands.isEmpty()) {
        QMessageBox::information(
            this, tr("Not Available"),
            tr("No known removal method for %1 on this distribution.").arg(formatName));
        return;
    }

    QStringList commandLines;
    for (const ProcessRunner::Command &command : commands)
        commandLines << QStringLiteral("%1 %2").arg(command.program, command.args.join(' '));

    const QString title = tr("Remove %1").arg(formatName);
    if (!TransactionConfirmDialog::confirm(
            this, title,
            tr("Remove %1 support?\n\nAny %1 apps you have installed will no longer be usable "
               "until %1 support is reinstalled.")
                .arg(formatName),
            commandLines.join('\n')))
        return;

    runCommandsAndOfferRestart(title, commands,
                                tr("%1 support has been removed. Restart Distore now so its tab disappears?")
                                    .arg(formatName));
}

void PackageFormatInstallWidget::runCommandsAndOfferRestart(const QString &title,
                                                             const QVector<ProcessRunner::Command> &commands,
                                                             const QString &successMessage)
{
    TerminalOutputDialog dialog(title, this);
    // Never auto-close here, even if the user has that setting on for
    // regular package operations — this is a one-off bootstrap action and
    // they should see it actually succeeded before being asked to restart.
    const bool success = dialog.runCommandsModal(commands, false);

    refreshState();

    if (success) {
        const auto reply = QMessageBox::question(this, tr("Restart Required"), successMessage,
                                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            // arguments() includes the program path itself as element 0;
            // startDetached() already supplies that separately, so it must
            // be dropped here or it'd show up a second time as argv[1].
            QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                     QCoreApplication::arguments().mid(1));
            QCoreApplication::quit();
        }
    }
}
