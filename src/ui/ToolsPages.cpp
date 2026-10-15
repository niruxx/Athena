#include "ToolsPages.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "../core/SystemTools.h"
#include "CatalogActions.h"
#include "CatalogHub.h"
#include "M3Dialog.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "TerminalOutputDialog.h"

namespace {

constexpr int kSourceKeyRole = Qt::UserRole;
constexpr int kPackageRole = Qt::UserRole + 1;
const QString kFormat = QStringLiteral("athena-setup");

using M3::section;

} // namespace

// ---------------------------------------------------------------- Export / import

ExportImportPage::ExportImportPage(CatalogHub *hub, const QString &nativeKey, QWidget *parent)
    : QWidget(parent), m_hub(hub), m_nativeKey(nativeKey)
{
    QVBoxLayout *exportBox = nullptr;
    QFrame *exportCard = section(this, QStringLiteral("backup"), tr("Export your setup"),
                                 tr("Saves the apps and packages you chose to install — from every source, not the dependencies "
                                    "they pulled in — to one file, to set up another machine the same way."),
                                 exportBox);
    auto *exportButton = new QPushButton(tr("Export…"), exportCard);
    MaterialTheme::styleButton(exportButton, QStringLiteral("filled"), QStringLiteral("backup"));
    exportBox->addWidget(exportButton, 0, Qt::AlignLeft);

    QVBoxLayout *importBox = nullptr;
    QFrame *importCard = section(this, QStringLiteral("restore"), tr("Import a setup"),
                                 tr("Opens an exported file and lists what isn't installed here yet. Pick what to install; AUR "
                                    "packages still go through PKGBUILD review."),
                                 importBox);
    auto *importButton = new QPushButton(tr("Open file…"), importCard);
    MaterialTheme::styleButton(importButton, QStringLiteral("tonal"), QStringLiteral("folder_open"));
    importBox->addWidget(importButton, 0, Qt::AlignLeft);
    m_importSummary = M3::label(QString(), QStringLiteral("label"), importCard);
    m_importSummary->setWordWrap(true);
    m_importSummary->hide();
    importBox->addWidget(m_importSummary);
    m_importList = new QListWidget(importCard);
    m_importList->setMinimumHeight(220);
    m_importList->hide();
    importBox->addWidget(m_importList, 1);
    m_installButton = new QPushButton(tr("Install selected"), importCard);
    MaterialTheme::styleButton(m_installButton, QStringLiteral("filled"), QStringLiteral("download"));
    m_installButton->hide();
    importBox->addWidget(m_installButton, 0, Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(exportCard);
    layout->addWidget(importCard);
    layout->addStretch(1);
    m_importCard = importCard;

    connect(exportButton, &QPushButton::clicked, this, &ExportImportPage::exportSetup);
    connect(importButton, &QPushButton::clicked, this, &ExportImportPage::importSetup);
    connect(m_installButton, &QPushButton::clicked, this, &ExportImportPage::installSelected);
}

void ExportImportPage::exportSetup()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Setup"),
                                                      QDir::homePath() + QStringLiteral("/athena-setup.json"),
                                                      tr("Athena setup (*.json)"));
    if (path.isEmpty())
        return;

    QJsonObject sources;
    if (!m_nativeKey.isEmpty())
        sources.insert(m_nativeKey, QJsonArray::fromStringList(SystemTools::explicitlyInstalled(m_nativeKey)));
    // The other sources: everything installed from them (AUR packages are
    // all deliberate installs; Flatpak/Snap apps rather than runtimes).
    QMap<QString, QStringList> others;
    for (const CatalogItem &item : m_hub->installed()) {
        for (const CatalogVariant &v : item.variants) {
            const QString key = m_hub->sources()[v.source].key;
            if (key == m_nativeKey || !v.info.installed)
                continue;
            if (key == QLatin1String("aur") ? !v.info.name.endsWith(QLatin1String("-debug")) : item.isApp)
                others[key] << v.info.name;
        }
    }
    for (auto it = others.begin(); it != others.end(); ++it) {
        it.value().removeDuplicates();
        it.value().sort();
        sources.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("machine"), QSysInfo::machineHostName());
    root.insert(QStringLiteral("system"), QSysInfo::prettyProductName());
    root.insert(QStringLiteral("sources"), sources);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0) {
        M3Message::critical(this, tr("Export Setup"), tr("Couldn't write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    int total = 0;
    for (const QJsonValue &list : sources)
        total += list.toArray().size();
    M3Message::information(this, tr("Setup exported"), tr("Saved %n package(s) to %1.", "", total).arg(path));
}

void ExportImportPage::importSetup()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Setup"), QDir::homePath(), tr("Athena setup (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const QJsonObject root = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    if (root.value(QStringLiteral("format")).toString() != kFormat) {
        M3Message::warning(this, tr("Import Setup"), tr("%1 isn't an Athena setup file.").arg(path));
        return;
    }

    // What's installed here, per source.
    QMap<QString, QSet<QString>> installed;
    for (const CatalogItem &item : m_hub->installed()) {
        for (const CatalogVariant &v : item.variants)
            installed[m_hub->sources()[v.source].key].insert(v.info.name);
    }

    m_importList->clear();
    int missing = 0;
    QStringList unavailable;
    const QJsonObject sources = root.value(QStringLiteral("sources")).toObject();
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        const int source = m_hub->indexOf(it.key());
        if (source < 0) {
            unavailable << it.key();
            continue;
        }
        QStringList names;
        for (const QJsonValue &value : it.value().toArray()) {
            const QString name = value.toString();
            if (!name.isEmpty() && !installed.value(it.key()).contains(name))
                names << name;
        }
        if (names.isEmpty())
            continue;
        auto *header = new QListWidgetItem(tr("%1 — %n not installed", "", names.size()).arg(m_hub->sources()[source].label), m_importList);
        header->setFlags(Qt::ItemIsEnabled);
        QFont bold = header->font();
        bold.setWeight(QFont::DemiBold);
        header->setFont(bold);
        for (const QString &name : names) {
            auto *item = new QListWidgetItem(name, m_importList);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setData(kSourceKeyRole, it.key());
            item->setData(kPackageRole, name);
            ++missing;
        }
    }
    QString summary = missing == 0 ? tr("Everything in %1 is already installed here.").arg(QFileInfo(path).fileName())
                                   : tr("%n package(s) from %1 aren't installed here.", "", missing)
                                         .arg(root.value(QStringLiteral("machine")).toString().isEmpty()
                                                  ? QFileInfo(path).fileName()
                                                  : root.value(QStringLiteral("machine")).toString());
    if (!unavailable.isEmpty())
        summary += QLatin1Char(' ') + tr("Skipped (not available on this system): %1.").arg(unavailable.join(QStringLiteral(", ")));
    m_importSummary->setText(summary);
    m_importSummary->show();
    setListVisible(missing > 0);
}

void ExportImportPage::installSelected()
{
    QVector<CatalogVariant> variants;
    for (int i = 0; i < m_importList->count(); ++i) {
        const QListWidgetItem *item = m_importList->item(i);
        if (!(item->flags() & Qt::ItemIsUserCheckable) || item->checkState() != Qt::Checked)
            continue;
        CatalogVariant variant;
        variant.source = m_hub->indexOf(item->data(kSourceKeyRole).toString());
        variant.info.name = item->data(kPackageRole).toString();
        if (variant.source >= 0)
            variants.append(variant);
    }
    CatalogActions::run(window(), m_hub->sources(), variants, CatalogActions::Action::Install, [this](bool) {
        m_hub->refreshAll();
        setListVisible(false);
        m_importSummary->setText(tr("Done. Open the file again to check for anything left."));
    });
}

// The import card hugs its content until there's a list to pick from.
void ExportImportPage::setListVisible(bool visible)
{
    m_importList->setVisible(visible);
    m_installButton->setVisible(visible);
    static_cast<QVBoxLayout *>(layout())->setStretchFactor(m_importCard, visible ? 1 : 0);
}

// ---------------------------------------------------------------- Mirrors

MirrorsPage::MirrorsPage(const QString &nativeKey, QWidget *parent) : QWidget(parent), m_nativeKey(nativeKey)
{
    const bool pacman = nativeKey == QLatin1String("pacman");
    QVBoxLayout *box = nullptr;
    QFrame *card = section(this, QStringLiteral("public"), pacman ? tr("Package mirrors") : tr("Fastest mirror"),
                           pacman ? tr("pacman downloads from the first mirrors in your mirror list. Ranking them by speed "
                                       "from where you are usually makes updates faster. You'll see the new list before "
                                       "anything changes, and the current one is backed up.")
                                  : tr("DNF can pick the fastest mirror itself. Turning this on sets fastestmirror=True in "
                                       "/etc/dnf/dnf.conf (backed up first)."),
                           box);
    m_status = M3::label(QString(), QStringLiteral("label"), card);
    box->addWidget(m_status);
    m_current = new QPlainTextEdit(card);
    m_current->setReadOnly(true);
    m_current->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_current->setMaximumHeight(180);
    m_current->setVisible(pacman);
    box->addWidget(m_current);
    m_rankButton = new QPushButton(pacman ? tr("Find faster mirrors") : tr("Turn on fastest-mirror selection"), card);
    MaterialTheme::styleButton(m_rankButton, QStringLiteral("filled"), QStringLiteral("sync"));
    box->addWidget(m_rankButton, 0, Qt::AlignLeft);

    m_previewBox = new QWidget(card);
    auto *previewLayout = new QVBoxLayout(m_previewBox);
    previewLayout->setContentsMargins(0, 8, 0, 0);
    previewLayout->addWidget(M3::label(tr("The ranked list, fastest first:"), QStringLiteral("titleM"), m_previewBox));
    m_preview = new QPlainTextEdit(m_previewBox);
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    previewLayout->addWidget(m_preview, 1);
    auto *previewButtons = new QHBoxLayout;
    auto *keep = new QPushButton(tr("Keep current list"), m_previewBox);
    MaterialTheme::styleButton(keep, QStringLiteral("text"));
    auto *use = new QPushButton(tr("Use these mirrors"), m_previewBox);
    MaterialTheme::styleButton(use, QStringLiteral("filled"), QStringLiteral("check"));
    previewButtons->addStretch(1);
    previewButtons->addWidget(keep);
    previewButtons->addWidget(use);
    previewLayout->addLayout(previewButtons);
    m_previewBox->hide();
    box->addWidget(m_previewBox, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addWidget(card);
    layout->addStretch(1);
    m_card = card;

    connect(m_rankButton, &QPushButton::clicked, this, [this, pacman]() {
        if (pacman)
            rank();
        else
            apply();
    });
    connect(keep, &QPushButton::clicked, this, [this]() {
        setPreviewVisible(false);
        QFile::remove(m_rankedFile);
    });
    connect(use, &QPushButton::clicked, this, &MirrorsPage::apply);
    reload();
}

void MirrorsPage::reload()
{
    QFile file(SystemTools::mirrorListPath(m_nativeKey));
    const QString text = file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
    if (m_nativeKey == QLatin1String("pacman")) {
        QStringList servers;
        for (const QString &line : text.split('\n')) {
            if (line.trimmed().startsWith(QLatin1String("Server")))
                servers << line.section('=', 1).trimmed();
        }
        m_status->setText(tr("%n mirror(s) in use; these come first:", "", servers.size()));
        m_current->setPlainText(servers.mid(0, 12).join('\n'));
    } else {
        const bool on = text.contains(QRegularExpression(QStringLiteral(R"(^fastestmirror\s*=\s*(True|true|1))"),
                                                         QRegularExpression::MultilineOption));
        m_status->setText(on ? tr("Fastest-mirror selection is on.") : tr("Fastest-mirror selection is off."));
        m_rankButton->setEnabled(!on);
    }
}

void MirrorsPage::rank()
{
    // reflector (or rate-mirrors) first, if neither is installed.
    const QString needed = SystemTools::mirrorRankingRequirement(m_nativeKey);
    if (!needed.isEmpty()) {
        if (M3Message::question(this, tr("Install %1?").arg(needed),
                                tr("Ranking mirrors uses %1, from the official repositories. Install it now?").arg(needed))
            != QMessageBox::Yes)
            return;
        TerminalOutputDialog install(tr("Install %1").arg(needed), window());
        if (!install.runCommandsModal({{"pkexec", {"pacman", "-S", "--needed", "--noconfirm", needed}}}, true))
            return;
    }

    // Ranks into a file of the user's own; nothing privileged yet.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    m_rankedFile = dir + QStringLiteral("/mirrorlist.ranked");
    QFile::remove(m_rankedFile);
    TerminalOutputDialog terminal(tr("Rank mirrors"), window());
    if (!terminal.runCommandsModal(SystemTools::rankMirrorsCommands(m_nativeKey, m_rankedFile), true))
        return;
    QFile ranked(m_rankedFile);
    if (!ranked.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QStringList servers;
    for (const QString &line : QString::fromUtf8(ranked.readAll()).split('\n')) {
        if (line.trimmed().startsWith(QLatin1String("Server")))
            servers << line.section('=', 1).trimmed();
    }
    m_preview->setPlainText(servers.join('\n'));
    setPreviewVisible(true);
}

void MirrorsPage::apply()
{
    TerminalOutputDialog terminal(m_nativeKey == QLatin1String("pacman") ? tr("Use ranked mirrors") : tr("Turn on fastest mirror"),
                                  window());
    if (terminal.runCommandsModal(SystemTools::applyMirrorsCommands(m_nativeKey, m_rankedFile),
                                  AppSettings::instance().autoCloseTerminalOnSuccess())) {
        setPreviewVisible(false);
        QFile::remove(m_rankedFile);
        reload();
    }
}

// The card hugs its content until there's a ranked list to show, then
// grows so the preview gets the room.
void MirrorsPage::setPreviewVisible(bool visible)
{
    m_previewBox->setVisible(visible);
    static_cast<QVBoxLayout *>(layout())->setStretchFactor(m_card, visible ? 1 : 0);
}
