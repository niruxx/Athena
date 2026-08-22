#include "GitHubReleaseChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "Version.h"

namespace {

const char *kReleasesUrl = "https://api.github.com/repos/niruxx/Athena/releases/latest";

QVector<int> parseVersionParts(QString version)
{
    if (version.startsWith('v') || version.startsWith('V'))
        version.remove(0, 1);

    QVector<int> parts;
    for (const QString &part : version.split('.'))
        parts.append(part.toInt());
    return parts;
}

bool isNewerVersion(const QString &candidate, const QString &current)
{
    const QVector<int> candidateParts = parseVersionParts(candidate);
    const QVector<int> currentParts = parseVersionParts(current);
    const int count = qMax(candidateParts.size(), currentParts.size());

    for (int i = 0; i < count; ++i) {
        const int candidatePart = i < candidateParts.size() ? candidateParts[i] : 0;
        const int currentPart = i < currentParts.size() ? currentParts[i] : 0;
        if (candidatePart != currentPart)
            return candidatePart > currentPart;
    }
    return false;
}

} // namespace

GitHubReleaseChecker::GitHubReleaseChecker(QObject *parent) : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
}

void GitHubReleaseChecker::checkForUpdate()
{
    QNetworkRequest request((QUrl(QString::fromLatin1(kReleasesUrl))));
    // GitHub's API rejects requests with no User-Agent header.
    request.setRawHeader("User-Agent", "athena-update-checker");
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // A brand-new project with no tagged releases yet gets a 404
            // here, which is a completely normal outcome, not a real
            // failure — just nothing to report either way.
            emit checkFailed(reply->errorString());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit checkFailed(QStringLiteral("Unexpected response from GitHub"));
            return;
        }

        const QJsonObject obj = doc.object();
        const QString tagName = obj.value(QStringLiteral("tag_name")).toString();
        const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();
        if (tagName.isEmpty()) {
            emit checkFailed(QStringLiteral("No release tag in response"));
            return;
        }

        if (isNewerVersion(tagName, QStringLiteral(ATHENA_VERSION)))
            emit updateAvailable(tagName, htmlUrl);
        else
            emit upToDate();
    });
}
