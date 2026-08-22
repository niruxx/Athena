#pragma once

#include <QObject>

class QNetworkAccessManager;

// Checks GitHub's REST API for the latest release of this project and
// compares its tag against the running build's version (see Version.h).
// One-shot per instance: call checkForUpdate() once, wait for exactly one
// of the signals below.
class GitHubReleaseChecker : public QObject {
    Q_OBJECT

public:
    explicit GitHubReleaseChecker(QObject *parent = nullptr);

    void checkForUpdate();

signals:
    // A release newer than ATHENA_VERSION was found.
    void updateAvailable(const QString &version, const QString &htmlUrl);
    // Checked successfully; nothing newer than the running build.
    void upToDate();
    // Could not complete the check (offline, no releases published yet,
    // rate-limited, etc.) — not shown to the user as an error, since this
    // runs silently at startup.
    void checkFailed(const QString &reason);

private:
    QNetworkAccessManager *m_networkManager;
};
