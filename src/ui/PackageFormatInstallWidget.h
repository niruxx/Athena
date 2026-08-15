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

    QLabel *m_flatpakStatusLabel;
    QPushButton *m_flatpakInstallButton;
    QLabel *m_snapStatusLabel;
    QPushButton *m_snapInstallButton;
};
