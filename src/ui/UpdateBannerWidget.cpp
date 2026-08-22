#include "UpdateBannerWidget.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>

UpdateBannerWidget::UpdateBannerWidget(QWidget *parent) : QWidget(parent)
{
    // Fixed accent colors (not palette-derived) so the banner reads as a
    // notification regardless of the app's current light/dark theme —
    // the same reasoning as TerminalOutputDialog's fixed terminal colors.
    setStyleSheet(QStringLiteral("UpdateBannerWidget { background-color: #2d7dd2; }"
                                  "QLabel { color: white; }"
                                  "QPushButton { color: white; border: 1px solid white; border-radius: 3px; "
                                  "padding: 3px 10px; background: transparent; }"
                                  "QPushButton:hover { background: rgba(255, 255, 255, 40); }"));

    auto *messageLabel = new QLabel(this);
    messageLabel->setObjectName(QStringLiteral("messageLabel"));

    auto *viewButton = new QPushButton(tr("View Release"), this);
    auto *dismissButton = new QPushButton(tr("Dismiss"), this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->addWidget(messageLabel, 1);
    layout->addWidget(viewButton);
    layout->addWidget(dismissButton);

    connect(dismissButton, &QPushButton::clicked, this, &QWidget::hide);
    connect(viewButton, &QPushButton::clicked, this, [this]() {
        const QString url = property("releaseUrl").toString();
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });

    hide();
}

void UpdateBannerWidget::showUpdate(const QString &version, const QString &htmlUrl)
{
    if (auto *messageLabel = findChild<QLabel *>(QStringLiteral("messageLabel")))
        messageLabel->setText(tr("A new version of Athena is available: %1").arg(version));
    setProperty("releaseUrl", htmlUrl);
    show();
}
