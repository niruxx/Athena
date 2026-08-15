#pragma once

#include <QWidget>

// Dismissible notice bar shown above the tab widget when
// GitHubReleaseChecker finds a release newer than the running build.
class UpdateBannerWidget : public QWidget {
    Q_OBJECT

public:
    explicit UpdateBannerWidget(QWidget *parent = nullptr);

    void showUpdate(const QString &version, const QString &htmlUrl);
};
