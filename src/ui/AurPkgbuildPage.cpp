#include "AurPkgbuildPage.h"

#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/backends/AurBackend.h"
#include "AppIcons.h"
#include "MaterialTheme.h"

AurPkgbuildPage::AurPkgbuildPage(AurBackend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    auto *noticeLabel = new QLabel(
        tr("AUR packages are user-submitted build scripts that run on your machine and aren't reviewed by "
           "Arch Linux. Read a package's PKGBUILD here before installing it — check where its sources are "
           "downloaded from and what its build()/package() steps do."),
        this);
    noticeLabel->setWordWrap(true);

    m_methodLabel = new QLabel(this);
    QFont methodFont = m_methodLabel->font();
    methodFont.setItalic(true);
    m_methodLabel->setFont(methodFont);
    m_methodLabel->setWordWrap(true);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("AUR package name (e.g. paru-bin)"));
    m_loadButton = new QPushButton(AppIcons::search(), tr("Show PKGBUILD"), this);
    MaterialTheme::styleButton(m_loadButton, QStringLiteral("filled"), QStringLiteral("description"));
    m_openPageButton = new QPushButton(tr("Open AUR Page"), this);
    MaterialTheme::styleButton(m_openPageButton, QStringLiteral("text"), QStringLiteral("open_in_new"));
    m_openPageButton->setEnabled(false);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_nameEdit, 1);
    topRow->addWidget(m_loadButton);
    topRow->addWidget(m_openPageButton);

    m_statusLabel = new QLabel(tr("Enter a package name."), this);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(noticeLabel);
    layout->addWidget(m_methodLabel);
    layout->addLayout(topRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_view, 1);

    connect(m_nameEdit, &QLineEdit::returnPressed, this, &AurPkgbuildPage::loadPkgbuild);
    connect(m_loadButton, &QPushButton::clicked, this, &AurPkgbuildPage::loadPkgbuild);
    connect(m_openPageButton, &QPushButton::clicked, this, &AurPkgbuildPage::openAurPage);
    connect(&m_watcher, &QFutureWatcher<QPair<QString, QString>>::finished, this, &AurPkgbuildPage::onLoaded);
}

void AurPkgbuildPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_methodLabel->setText(AurBackend::reviewRequired()
                               ? tr("PKGBUILD review is on: every install, reinstall, and update opens each "
                                    "package's build files for review and editing before anything is built.")
                               : tr("Packages are built with: %1 — without review (turn it on in Preferences → "
                                    "General → AUR).")
                                     .arg(AurBackend::buildMethodName(AurBackend::buildMethod())));
}

void AurPkgbuildPage::loadPkgbuild()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || !m_backend || m_watcher.isRunning())
        return;

    m_loadButton->setEnabled(false);
    m_openPageButton->setEnabled(false);
    m_statusLabel->setText(tr("Fetching PKGBUILD for \"%1\"...").arg(name));
    m_loadedName = name;

    AurBackend *backend = m_backend;
    m_watcher.setFuture(QtConcurrent::run([backend, name]() {
        QString error;
        const QString text = backend->fetchPkgbuild(name, &error);
        return qMakePair(text, error);
    }));
}

void AurPkgbuildPage::onLoaded()
{
    const auto [text, error] = m_watcher.result();
    m_loadButton->setEnabled(true);

    if (text.isEmpty()) {
        m_view->clear();
        m_statusLabel->setText(error.isEmpty() ? tr("No PKGBUILD found for \"%1\".").arg(m_loadedName)
                                               : tr("Couldn't load PKGBUILD: %1").arg(error));
        return;
    }

    m_view->setPlainText(text);
    m_statusLabel->setText(tr("PKGBUILD for %1").arg(m_loadedName));
    m_openPageButton->setEnabled(true);
}

void AurPkgbuildPage::openAurPage()
{
    if (m_loadedName.isEmpty())
        return;
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://aur.archlinux.org/packages/%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(m_loadedName)))));
}
