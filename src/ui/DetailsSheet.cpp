#include "DetailsSheet.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/ProcessRunner.h"
#include "../core/backends/AurBackend.h"
#include "../core/backends/FlatpakBackend.h"
#include "AppCatalog.h"
#include "CatalogActions.h"
#include "MaterialTheme.h"
#include "ToolsDialogs.h"
#include "Motion.h"

namespace {

constexpr int kSheetWidth = 460;

QLabel *roleLabel(const QString &text, const QString &role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("m3", role);
    return label;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

} // namespace

DetailsSheet::DetailsSheet(const QVector<CatalogSource> *sources, FlatpakBackend *flatpak, QWidget *parent)
    : QFrame(parent), m_sources(sources), m_flatpak(flatpak)
{
    setObjectName(QStringLiteral("DetailsSheet"));
    setAttribute(Qt::WA_StyledBackground);
    hide();

    m_scrim = new QWidget(parent);
    m_scrim->setAttribute(Qt::WA_StyledBackground);
    m_scrim->setStyleSheet(QStringLiteral("background: rgba(0, 0, 0, 0.32);"));
    m_scrimEffect = new QGraphicsOpacityEffect(m_scrim);
    m_scrim->setGraphicsEffect(m_scrimEffect);
    m_scrim->hide();
    m_scrim->installEventFilter(this);
    parent->installEventFilter(this);

    m_animation = new QPropertyAnimation(this, "pos", this);
    m_animation->setDuration(280);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    // The scrim fades with the sheet's slide.
    connect(m_animation, &QPropertyAnimation::valueChanged, this, [this]() {
        const QWidget *host = parentWidget();
        const int travel = std::max(1, width());
        const qreal shown = 1.0 - qreal(pos().x() - (host->width() - width())) / travel;
        m_scrimEffect->setOpacity(std::clamp(shown, 0.0, 1.0));
    });
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_open) {
            hide();
            m_scrim->hide();
        }
    });

    auto *closeButton = new QPushButton(this);
    MaterialTheme::styleButton(closeButton, QStringLiteral("icon"), QStringLiteral("close"));
    closeButton->setToolTip(tr("Close"));
    connect(closeButton, &QPushButton::clicked, this, &DetailsSheet::close);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &DetailsSheet::close);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(24, 12, 12, 0);
    header->addWidget(roleLabel(tr("Details"), QStringLiteral("title"), this), 1);
    header->addWidget(closeButton);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(24, 12, 24, 32);
    bodyLayout->setSpacing(18);

    m_icon = new QLabel(body);
    m_icon->setFixedSize(88, 88);
    m_name = roleLabel(QString(), QStringLiteral("headline"), body);
    m_name->setWordWrap(true);
    m_subtitle = roleLabel(QString(), QStringLiteral("muted"), body);
    m_subtitle->setWordWrap(true);
    auto *nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(4);
    nameColumn->addStretch(1);
    nameColumn->addWidget(m_name);
    nameColumn->addWidget(m_subtitle);
    nameColumn->addStretch(1);
    auto *hero = new QHBoxLayout;
    hero->setSpacing(18);
    hero->addWidget(m_icon);
    hero->addLayout(nameColumn, 1);
    bodyLayout->addLayout(hero);

    bodyLayout->addWidget(roleLabel(tr("GET IT FROM"), QStringLiteral("label"), body));
    m_sourceRow = new QHBoxLayout;
    m_sourceRow->setSpacing(0);
    auto *sourceLine = new QHBoxLayout;
    sourceLine->addLayout(m_sourceRow);
    sourceLine->addStretch(1);
    bodyLayout->addLayout(sourceLine);

    m_actionRow = new QHBoxLayout;
    m_actionRow->setSpacing(8);
    bodyLayout->addLayout(m_actionRow);

    // Screenshots from the app's AppStream data, fetched on first view.
    m_shotsBox = new QWidget(body);
    m_shotsRow = new QHBoxLayout(m_shotsBox);
    m_shotsRow->setContentsMargins(0, 0, 0, 0);
    m_shotsRow->setSpacing(10);
    auto *shotsScroll = new QScrollArea(body);
    shotsScroll->setProperty("m3", QStringLiteral("page"));
    shotsScroll->setFrameShape(QFrame::NoFrame);
    shotsScroll->setWidgetResizable(true);
    shotsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shotsScroll->setFixedHeight(214);
    shotsScroll->setWidget(m_shotsBox);
    m_shotsBox->setProperty("scrollArea", QVariant::fromValue<QObject *>(shotsScroll));
    bodyLayout->addWidget(shotsScroll);
    shotsScroll->hide();

    m_description = new QLabel(body);
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bodyLayout->addWidget(m_description);

    m_facts = new QGridLayout;
    m_facts->setSpacing(12);
    bodyLayout->addLayout(m_facts);

    m_homepage = new QLabel(body);
    m_homepage->setTextFormat(Qt::RichText);
    m_homepage->setOpenExternalLinks(true);
    m_homepage->setWordWrap(true);
    bodyLayout->addWidget(m_homepage);

    m_permissionsBox = new QWidget(body);
    auto *permissionsLayout = new QVBoxLayout(m_permissionsBox);
    permissionsLayout->setContentsMargins(0, 0, 0, 0);
    permissionsLayout->setSpacing(8);
    permissionsLayout->addWidget(roleLabel(tr("SANDBOX PERMISSIONS"), QStringLiteral("label"), m_permissionsBox));
    m_permissions = new QLabel(m_permissionsBox);
    m_permissions->setTextFormat(Qt::RichText);
    m_permissions->setWordWrap(true);
    permissionsLayout->addWidget(m_permissions);
    auto *editPermissionsButton = new QPushButton(tr("Edit permissions"), m_permissionsBox);
    MaterialTheme::styleButton(editPermissionsButton, QStringLiteral("text"), QStringLiteral("tune"));
    permissionsLayout->addWidget(editPermissionsButton, 0, Qt::AlignLeft);
    connect(editPermissionsButton, &QPushButton::clicked, this, [this]() {
        const CatalogVariant &variant = m_item.variants[m_variant];
        close();
        emit editPermissions(variant.info.name);
    });
    bodyLayout->addWidget(m_permissionsBox);

    m_callout = new QFrame(body);
    m_callout->setProperty("m3", QStringLiteral("callout"));
    auto *calloutLayout = new QHBoxLayout(m_callout);
    calloutLayout->setContentsMargins(16, 14, 16, 14);
    m_calloutText = new QLabel(m_callout);
    m_calloutText->setWordWrap(true);
    calloutLayout->addWidget(m_calloutText);
    bodyLayout->addWidget(m_callout);
    bodyLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setProperty("m3", QStringLiteral("page"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(scroll, 1);

    connect(&m_watcher, &QFutureWatcher<Details>::finished, this, &DetailsSheet::onDetailsLoaded);
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, [this]() {
        if (m_open)
            showVariant(m_variant);
    });
}

void DetailsSheet::open(const CatalogItem &item, bool offerUpdate)
{
    m_item = item;
    m_offerUpdate = offerUpdate;
    m_name->setText(item.title);
    m_subtitle->setText(item.subtitle);
    m_subtitle->setVisible(!item.subtitle.isEmpty());

    QPixmap icon(QSize(88, 88) * 2);
    icon.setDevicePixelRatio(2);
    icon.fill(Qt::transparent);
    {
        QPainter painter(&icon);
        const CatalogVariant &first = item.variants[item.preferredVariant()];
        CatalogPaint::appIcon(&painter, QRect(0, 0, 88, 88), item, (*m_sources)[first.source].icon);
    }
    m_icon->setPixmap(icon);

    showScreenshots(item.screenshots);
    showVariant(item.preferredVariant());

    if (!m_open) {
        m_open = true;
        QWidget *host = parentWidget();
        m_scrim->setGeometry(host->rect());
        m_scrim->show();
        m_scrim->raise();
        const int width = std::min(kSheetWidth, host->width());
        resize(width, host->height());
        move(host->width(), 0);
        show();
        raise();
        m_animation->stop();
        m_animation->setDuration(std::max(1, Motion::duration(280)));
        m_animation->setStartValue(QPoint(host->width(), 0));
        m_animation->setEndValue(QPoint(host->width() - width, 0));
        m_animation->start();
    }
    setFocus();
}

void DetailsSheet::close()
{
    if (!m_open)
        return;
    m_open = false;
    m_animation->stop();
    m_animation->setDuration(std::max(1, Motion::duration(220)));
    m_animation->setStartValue(pos());
    m_animation->setEndValue(QPoint(parentWidget()->width(), 0));
    m_animation->start();
}

bool DetailsSheet::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        reposition();
    if (watched == m_scrim && event->type() == QEvent::MouseButtonPress) {
        close();
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void DetailsSheet::reposition()
{
    QWidget *host = parentWidget();
    m_scrim->setGeometry(host->rect());
    const int width = std::min(kSheetWidth, host->width());
    resize(width, host->height());
    if (m_animation->state() != QAbstractAnimation::Running)
        move(m_open ? host->width() - width : host->width(), 0);
}

void DetailsSheet::showVariant(int index)
{
    if (index < 0 || index >= m_item.variants.size())
        return;
    m_variant = index;
    rebuildSourceButtons();
    rebuildActions();

    const CatalogVariant &variant = m_item.variants[index];
    const CatalogSource &source = (*m_sources)[variant.source];
    m_callout->setVisible(source.key == QLatin1String("aur"));
    m_calloutText->setText(AurBackend::reviewRequired()
                               ? tr("Built on your machine from a community build script. You'll read and approve its "
                                    "PKGBUILD before anything runs.")
                               : tr("Built on your machine from a community build script, without review. Turn PKGBUILD "
                                    "review on in Settings to read it first."));

    // Show what's already known right away; the full details (long
    // description, size, homepage, sandbox) fill in when they load.
    Details known;
    known.info = variant.info;
    render(known);
    m_watcher.setFuture(QtConcurrent::run(&DetailsSheet::loadDetails, source.backend, m_flatpak, source.key, variant.info));
}

void DetailsSheet::showScreenshots(const QStringList &urls)
{
    auto *scroll = qobject_cast<QWidget *>(m_shotsBox->property("scrollArea").value<QObject *>());
    while (QLayoutItem *item = m_shotsRow->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    scroll->setVisible(!urls.isEmpty());
    const int token = ++m_shotsToken;
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/screenshots");
    QDir().mkpath(cacheDir);
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    for (const QString &url : urls) {
        auto *shot = new QLabel(m_shotsBox);
        shot->setFixedSize(320, 200);
        shot->setAlignment(Qt::AlignCenter);
        shot->setStyleSheet(QStringLiteral("background: %1; border-radius: 12px; color: %2;")
                                .arg(s.surfaceContainerHigh.name(), s.onSurfaceVariant.name()));
        shot->setText(tr("Loading screenshot…"));
        m_shotsRow->addWidget(shot);

        // Cached by URL; fetched once, then reused.
        const QString path = cacheDir + '/' + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex())
            + QStringLiteral(".img");
        auto *watcher = new QFutureWatcher<bool>(shot);
        connect(watcher, &QFutureWatcher<bool>::finished, shot, [this, watcher, shot, path, token]() {
            if (token != m_shotsToken)
                return;
            QPixmap pixmap;
            if (watcher->result() && pixmap.load(path)) {
                const QPixmap scaled = pixmap.scaledToHeight(200, Qt::SmoothTransformation);
                shot->setFixedWidth(std::min(scaled.width(), 420));
                shot->setPixmap(scaled.copy(0, 0, std::min(scaled.width(), 420), scaled.height()));
                shot->setStyleSheet(QString());
            } else {
                shot->setText(tr("Screenshot unavailable offline"));
            }
        });
        watcher->setFuture(QtConcurrent::run([url, path]() {
            if (QFile::exists(path))
                return true;
            const ProcessRunner::Result result = ProcessRunner::run(
                "curl", {"--fail", "--silent", "--location", "--max-time", "20", "--output", path + QStringLiteral(".part"), "--", url},
                25000);
            return result.started && result.exitCode == 0 && QFile::rename(path + QStringLiteral(".part"), path);
        }));
    }
    m_shotsRow->addStretch(1);
}

void DetailsSheet::rebuildSourceButtons()
{
    clearLayout(m_sourceRow);
    QVector<int> seen;
    for (int i = 0; i < m_item.variants.size(); ++i) {
        const CatalogSource &source = (*m_sources)[m_item.variants[i].source];
        auto *button = new QPushButton(source.label, this);
        button->setCheckable(true);
        button->setChecked(i == m_variant);
        MaterialTheme::styleButton(button, QStringLiteral("segment"), i == m_variant ? QStringLiteral("check") : source.icon);
        const int count = m_item.variants.size();
        button->setProperty("m3pos", count == 1 ? "only" : i == 0 ? "first" : i == count - 1 ? "last" : "middle");
        connect(button, &QPushButton::clicked, this, [this, i]() { showVariant(i); });
        m_sourceRow->addWidget(button);
    }
}

void DetailsSheet::rebuildActions()
{
    clearLayout(m_actionRow);
    const CatalogVariant &variant = m_item.variants[m_variant];
    const CatalogSource &source = (*m_sources)[variant.source];
    const bool aur = source.key == QLatin1String("aur");
    const PackageInfo &info = variant.info;

    auto add = [this](const QString &text, const QString &role, const QString &icon, const QString &kind) {
        auto *button = new QPushButton(text, this);
        MaterialTheme::styleButton(button, role, icon);
        connect(button, &QPushButton::clicked, this, [this, kind]() { runTransaction(kind); });
        m_actionRow->addWidget(button);
    };

    if (info.installed) {
        const bool hasUpdate = m_offerUpdate && !info.availableVersion.isEmpty() && info.availableVersion != info.installedVersion;
        if (hasUpdate)
            add(aur && AurBackend::reviewRequired() ? tr("Review & update") : tr("Update"), QStringLiteral("filled"),
                aur ? QStringLiteral("verified_user") : QStringLiteral("system_update_alt"), QStringLiteral("update"));
        const bool launchable = source.key == QLatin1String("flatpak") || AppCatalog::find(info.name);
        if (launchable && m_item.isApp)
            add(tr("Open"), QStringLiteral("tonal"), QStringLiteral("open_in_new"), QStringLiteral("open"));
        if (source.key == QLatin1String("pacman"))
            add(tr("Other versions…"), QStringLiteral("text"), QStringLiteral("history"), QStringLiteral("downgrade"));
        add(aur && AurBackend::reviewRequired() ? tr("Review & reinstall") : tr("Reinstall"), QStringLiteral("outlined"),
            QStringLiteral("restart_alt"), QStringLiteral("reinstall"));
        add(tr("Remove"), QStringLiteral("danger"), QStringLiteral("delete"), QStringLiteral("remove"));
    } else {
        add(aur && AurBackend::reviewRequired() ? tr("Review & install") : tr("Install"), QStringLiteral("filled"),
            aur ? QStringLiteral("verified_user") : QStringLiteral("download"), QStringLiteral("install"));
    }
    m_actionRow->addStretch(1);
}

DetailsSheet::Details DetailsSheet::loadDetails(PackageBackend *backend, FlatpakBackend *flatpak, const QString &sourceKey,
                                                const PackageInfo &info)
{
    Details details;
    details.info = info;
    const QVector<PackageInfo> fetched = backend->packageDetails({info.name});
    if (!fetched.isEmpty()) {
        const PackageInfo &full = fetched.first();
        auto fill = [](QString &target, const QString &value) {
            if (!value.isEmpty())
                target = value;
        };
        // The AppStream description (when the item came from the index) is
        // usually far richer than a backend's one-line summary.
        if (full.longDescription.size() > details.info.longDescription.size())
            details.info.longDescription = full.longDescription;
        fill(details.info.description, full.description);
        fill(details.info.size, full.size);
        fill(details.info.architecture, full.architecture);
        fill(details.info.homepageUrl, full.homepageUrl);
        fill(details.info.repository, full.repository);
        fill(details.info.availableVersion, full.availableVersion);
        fill(details.info.installedVersion, full.installedVersion);
    }

    if (sourceKey == QLatin1String("flatpak") && info.installed && flatpak) {
        const FlatpakBackend::Permissions p = flatpak->permissionsForApp(info.name);
        auto add = [&details](const QString &text, bool risky) {
            details.permissions << text;
            if (risky)
                details.riskyPermissions << text;
        };
        if (p.shared.contains(QStringLiteral("network")))
            add(QObject::tr("Network"), false);
        if (p.sockets.contains(QStringLiteral("pulseaudio")))
            add(QObject::tr("Audio"), false);
        if (p.sockets.contains(QStringLiteral("x11")) && !p.sockets.contains(QStringLiteral("wayland")))
            add(QObject::tr("X11 display"), false);
        if (p.devices.contains(QStringLiteral("all")))
            add(QObject::tr("All devices"), true);
        else if (p.devices.contains(QStringLiteral("dri")))
            add(QObject::tr("GPU"), false);
        for (const QString &fs : p.filesystems) {
            const QString base = fs.section(':', 0, 0);
            if (base == QLatin1String("host"))
                add(QObject::tr("Entire filesystem"), true);
            else if (base == QLatin1String("home"))
                add(QObject::tr("Home folder"), true);
            else if (base == QLatin1String("host-os") || base == QLatin1String("host-etc"))
                add(QObject::tr("System files"), true);
            else
                add(fs, false);
        }
        if (p.sessionBusTalk.contains(QStringLiteral("org.freedesktop.Flatpak")))
            add(QObject::tr("Runs host commands"), true);
    }
    return details;
}

void DetailsSheet::onDetailsLoaded()
{
    if (m_variant >= m_item.variants.size())
        return;
    const Details details = m_watcher.result();
    CatalogVariant &variant = m_item.variants[m_variant];
    if (details.info.name != variant.info.name)
        return; // a result for a variant the user has since switched away from
    const bool installed = variant.info.installed;
    variant.info = details.info;
    variant.info.installed = installed;
    Details merged = details;
    merged.info = variant.info;
    render(merged);
}

void DetailsSheet::render(const Details &details)
{
    const CatalogVariant &variant = m_item.variants[m_variant];
    const PackageInfo &info = details.info;
    const QString key = (*m_sources)[variant.source].key;

    const QString text = !info.longDescription.isEmpty() ? info.longDescription : info.description;
    m_description->setText(key == QLatin1String("flatpak") ? text.section(QStringLiteral(" — "), -1) : text);

    clearLayout(m_facts);
    QVector<QPair<QString, QString>> facts;
    if (info.installed)
        facts.append({tr("Installed"), info.installedVersion});
    if (!info.availableVersion.isEmpty() && (!info.installed || info.availableVersion != info.installedVersion))
        facts.append({info.installed ? tr("Available") : tr("Version"), info.availableVersion});
    if (!info.size.isEmpty())
        facts.append({key == QLatin1String("aur") ? tr("Download") : tr("Size"), info.size});
    facts.append({key == QLatin1String("flatpak") ? tr("App ID") : key == QLatin1String("aur") ? tr("AUR package") : tr("Package"),
                  info.name});
    if (!info.repository.isEmpty() && info.repository != QLatin1String("installed"))
        facts.append({tr("Repository"), info.repository});
    if (!info.architecture.isEmpty())
        facts.append({tr("Architecture"), info.architecture});
    for (int i = 0; i < facts.size(); ++i) {
        auto *fact = new QFrame(this);
        fact->setProperty("m3", QStringLiteral("fact"));
        auto *factLayout = new QVBoxLayout(fact);
        factLayout->setContentsMargins(14, 10, 14, 10);
        factLayout->setSpacing(2);
        factLayout->addWidget(roleLabel(facts[i].first, QStringLiteral("label"), fact));
        auto *value = new QLabel(facts[i].second, fact);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont bold = value->font();
        bold.setWeight(QFont::DemiBold);
        value->setFont(bold);
        factLayout->addWidget(value);
        m_facts->addWidget(fact, i / 2, i % 2);
    }

    m_homepage->setVisible(!info.homepageUrl.isEmpty());
    m_permissionsBox->setVisible(!details.permissions.isEmpty());
    if (!info.homepageUrl.isEmpty()) {
        m_homepage->setText(tr("Homepage: <a href=\"%1\">%1</a>").arg(info.homepageUrl.toHtmlEscaped()));
    }

    if (!details.permissions.isEmpty()) {
        const MaterialTheme::Scheme &s = MaterialTheme::current();
        QStringList chips;
        for (const QString &permission : details.permissions) {
            const bool risky = details.riskyPermissions.contains(permission);
            // Qt's rich text ignores white-space:nowrap, so keep a chip on one
            // line with non-breaking spaces and hyphens instead.
            QString text = permission.toHtmlEscaped();
            text.replace(' ', QStringLiteral("&nbsp;")).replace('-', QChar(0x2011));
            chips << QStringLiteral("<span style=\"background:%1; color:%2;\">&nbsp;&nbsp;%3&nbsp;&nbsp;</span>")
                         .arg((risky ? s.warnContainer : s.surfaceContainerHighest).name(),
                              (risky ? s.onWarnContainer : s.onSurface).name(), text);
        }
        m_permissions->setText(chips.join(QStringLiteral(" &nbsp;")));
    }
}

void DetailsSheet::runTransaction(const QString &kind)
{
    const CatalogVariant variant = m_item.variants[m_variant];
    const CatalogSource &source = (*m_sources)[variant.source];
    const QString name = variant.info.name;
    const QString title = m_item.title;

    if (kind == QLatin1String("open")) {
        if (source.key == QLatin1String("flatpak")) {
            QProcess::startDetached(QStringLiteral("flatpak"), {QStringLiteral("run"), name});
        } else if (const AppCatalog::Entry *entry = AppCatalog::find(name)) {
            if (!AppCatalog::launch(*entry)) {
                emit message(tr("Couldn't start %1.").arg(title));
                return;
            }
        }
        emit message(tr("Opening %1…").arg(title));
        return;
    }
    if (kind == QLatin1String("downgrade")) {
        DowngradeDialog dialog(name, variant.info.installedVersion, window());
        if (dialog.runModal()) {
            emit message(tr("Installed another version of %1").arg(title));
            emit packagesChanged();
            close();
        }
        return;
    }

    auto done = [this, kind, title](bool success) {
        if (!success)
            return;
        emit message(kind == QLatin1String("remove")      ? tr("Removed %1").arg(title)
                     : kind == QLatin1String("update")    ? tr("Updated %1").arg(title)
                     : kind == QLatin1String("reinstall") ? tr("Reinstalled %1").arg(title)
                                                          : tr("Installed %1").arg(title));
        emit packagesChanged();
        close();
    };
    using CatalogActions::Action;
    const Action action = kind == QLatin1String("remove")      ? Action::Remove
                        : kind == QLatin1String("update")    ? Action::Upgrade
                        : kind == QLatin1String("reinstall") ? Action::Reinstall
                                                             : Action::Install;
    CatalogActions::run(window(), *m_sources, {variant}, action, done);
}
