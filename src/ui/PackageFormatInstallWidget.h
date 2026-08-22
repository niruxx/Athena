#pragma once

#include <QVector>
#include <QWidget>

#include "../core/ProcessRunner.h"

class QLabel;
class QPushButton;

// A small "Flatpak: not installed [Install Flatpak Support]" / same-for-
// Snap pair of rows, shared by SettingsPage and FirstRunDialog so the
// bootstrap-install flow (confirm -> live terminal -> offer restart) only
// exists in one place.
class PackageFormatInstallWidget : public QWidget {
    Q_OBJECT

public:
    explicit PackageFormatInstallWidget(QWidget *parent = nullptr);

private:
    void refreshState();
    void runInstall(const QString &formatName, const QVector<ProcessRunner::Command> &commands);
    void runRemove(const QString &formatName, const QVector<ProcessRunner::Command> &commands);
    // Shared by both flows: runs the commands live in a TerminalOutputDialog,
    // refreshes the displayed state, and (on success) offers to restart the
    // app so MainWindow re-evaluates which backend tabs exist.
    void runCommandsAndOfferRestart(const QString &title, const QVector<ProcessRunner::Command> &commands,
                                     const QString &successMessage);

    QLabel *m_flatpakStatusLabel;
    QPushButton *m_flatpakInstallButton;
    QPushButton *m_flatpakRemoveButton;
    QLabel *m_snapStatusLabel;
    QPushButton *m_snapInstallButton;
    QPushButton *m_snapRemoveButton;
};
