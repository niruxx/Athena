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
    m_snapStatusLabel = new QLabel(this);
    m_snapInstallButton = new QPushButton(tr("Install Snap Support"), this);

    auto *flatpakRow = new QHBoxLayout;
    flatpakRow->addWidget(m_flatpakStatusLabel, 1);
    flatpakRow->addWidget(m_flatpakInstallButton);

    auto *snapRow = new QHBoxLayout;
    snapRow->addWidget(m_snapStatusLabel, 1);
    snapRow->addWidget(m_snapInstallButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(flatpakRow);
    layout->addLayout(snapRow);

    connect(m_flatpakInstallButton, &QPushButton::clicked, this, [this]() {
        runInstall(tr("Flatpak"), DistroSupport::flatpakInstallCommands());
    });
    connect(m_snapInstallButton, &QPushButton::clicked, this,
            [this]() { runInstall(tr("Snap"), DistroSupport::snapInstallCommands()); });

    refreshState();
}

void PackageFormatInstallWidget::refreshState()
{
    const bool flatpakInstalled = DistroSupport::isFlatpakInstalled();
    m_flatpakStatusLabel->setText(flatpakInstalled ? tr("Flatpak: installed")
                                                     : tr("Flatpak: not installed"));
    m_flatpakInstallButton->setVisible(!flatpakInstalled);

    const bool snapInstalled = DistroSupport::isSnapInstalled();
    m_snapStatusLabel->setText(snapInstalled ? tr("Snap: installed") : tr("Snap: not installed"));
    m_snapInstallButton->setVisible(!snapInstalled);

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

    TerminalOutputDialog dialog(title, this);
    // Never auto-close here, even if the user has that setting on for
    // regular package operations — this is a one-off bootstrap action and
    // they should see it actually succeeded before being asked to restart.
    const bool success = dialog.runCommandsModal(commands, false);

    refreshState();

    if (success) {
        const auto reply = QMessageBox::question(
            this, tr("Restart Required"),
            tr("%1 support has been installed. Restart Distore now to start using it?").arg(formatName),
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
