#include "AurReviewDialog.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../core/AppSettings.h"
#include "AthenaStyle.h"
#include "TerminalOutputDialog.h"

namespace {

constexpr int kLineRole = Qt::UserRole;
constexpr qint64 kMaxReviewFileSize = 1024 * 1024;

// Heuristic hints for lines worth a second look — not a verdict either
// way: plenty of legitimate PKGBUILDs trip one of these, and a malicious
// one needn't trip any.
struct RiskPattern {
    QRegularExpression pattern;
    QString reason;
};

const QVector<RiskPattern> &riskPatterns()
{
    static const QVector<RiskPattern> patterns = {
        {QRegularExpression(QStringLiteral(R"(\b(curl|wget)\b[^|#]*\|\s*(sudo\s+)?(ba|z|da|k)?sh\b)")),
         QObject::tr("Downloads something and pipes it straight into a shell")},
        {QRegularExpression(QStringLiteral(R"(\bsudo\b|\bpkexec\b|\bdoas\b)")),
         QObject::tr("Asks for root — a PKGBUILD never needs to")},
        {QRegularExpression(QStringLiteral(R"(\brm\s+-[a-zA-Z]*[rR][a-zA-Z]*\s+["']?(/(?!\$|tmp)|~|\$HOME))")),
         QObject::tr("Recursive delete outside the build directory")},
        {QRegularExpression(QStringLiteral(R"(\bbase64\s+(-d|--decode)\b|\bxxd\s+-r\b)")),
         QObject::tr("Decodes embedded data (a common way to hide code)")},
        {QRegularExpression(QStringLiteral(R"(\beval\b)")), QObject::tr("Uses eval to run constructed code")},
        {QRegularExpression(QStringLiteral(R"(/dev/(tcp|udp)/|\b(nc|ncat|netcat|socat)\s)")),
         QObject::tr("Opens a raw network connection")},
        {QRegularExpression(QStringLiteral(R"(\bchmod\s+([0-7]*[2367]{2}7|[ug]?\+s|[4-7][0-7]{3})\b)")),
         QObject::tr("Sets setuid/setgid or world-writable permissions")},
        {QRegularExpression(QStringLiteral(R"((\.bashrc|\.zshrc|\.profile|/etc/sudoers|/etc/profile|crontab|authorized_keys))")),
         QObject::tr("Touches shell startup files, sudoers, cron, or SSH keys")},
        {QRegularExpression(QStringLiteral(R"(\bsystemctl\s+(enable|start)\b)")),
         QObject::tr("Enables or starts a system service")},
        {QRegularExpression(QStringLiteral(R"(\bLD_PRELOAD\b)")), QObject::tr("Uses LD_PRELOAD")},
        {QRegularExpression(QStringLiteral(R"(\bhttp://)")),
         QObject::tr("Downloads over plain HTTP (no TLS) — check that a checksum covers it")},
        {QRegularExpression(QStringLiteral(R"(['"]SKIP['"])")),
         QObject::tr("A source's checksum is skipped — expected for git/VCS sources, suspicious for tarballs")},
    };
    return patterns;
}

// PKGBUILD first, then install scriptlets (they run as root), then the rest.
int fileRank(const QString &fileName)
{
    if (fileName == QLatin1String("PKGBUILD"))
        return 0;
    if (fileName.endsWith(QLatin1String(".install")))
        return 1;
    if (fileName == QLatin1String(".SRCINFO"))
        return 3;
    return 2;
}

QIcon viewedIcon()
{
    return QIcon::fromTheme(QStringLiteral("emblem-ok-symbolic"),
                            qApp->style()->standardIcon(QStyle::SP_DialogApplyButton));
}

QIcon pendingIcon()
{
    return QIcon::fromTheme(QStringLiteral("emblem-important"),
                            qApp->style()->standardIcon(QStyle::SP_MessageBoxWarning));
}

} // namespace

AurReviewDialog::AurReviewDialog(const AurBackend::PreparedBuild &build, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Review AUR Build Files"));
    resize(1150, 760);

    // Load every reviewable file up front; anything that can't be shown
    // (symlinks, binaries, oversized files, subdirectories) becomes a
    // warning instead, so it's never silently skipped.
    QStringList structuralWarnings;
    for (const AurBackend::PreparedBuild::Package &source : build.packages) {
        ReviewPackage pkg;
        pkg.source = source;

        const QFileInfoList entries =
            QDir(source.directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString label = QStringLiteral("%1/%2").arg(source.info.packageBase, entry.fileName());
            if (entry.fileName() == QLatin1String(".git"))
                continue;
            if (entry.isSymLink()) {
                structuralWarnings << tr("%1 is a symlink to %2 — not shown").arg(label, entry.symLinkTarget());
                continue;
            }
            if (entry.isDir()) {
                structuralWarnings << tr("%1 is a subdirectory — its contents aren't shown").arg(label);
                continue;
            }
            if (entry.size() > kMaxReviewFileSize) {
                structuralWarnings << tr("%1 is too large to review here (%2 bytes)").arg(label).arg(entry.size());
                continue;
            }
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray data = file.readAll();
            if (data.contains('\0')) {
                structuralWarnings << tr("%1 is a binary file — not shown").arg(label);
                continue;
            }
            const QString text = QString::fromUtf8(data);
            pkg.files.append({entry.absoluteFilePath(), text, text});
        }
        std::stable_sort(pkg.files.begin(), pkg.files.end(), [](const ReviewFile &a, const ReviewFile &b) {
            return fileRank(QFileInfo(a.path).fileName()) < fileRank(QFileInfo(b.path).fileName());
        });
        m_packages.append(pkg);
    }

    auto *noticeLabel = new QLabel(
        tr("<b>Review before building.</b> AUR packages are user-submitted scripts that run on your machine, "
           "and their install scriptlets (<i>.install</i> files) run as root. Check where each source is "
           "downloaded from, that checksums are present, and what <i>build()</i>, <i>package()</i> and any "
           "scriptlets do. You can edit any file here — exactly what you see is what gets built."),
        this);
    noticeLabel->setWordWrap(true);

    QStringList planLines;
    planLines << tr("%n AUR package(s) to build, in this order.", "", m_packages.size());
    if (!build.repoDependencies.isEmpty())
        planLines << tr("Repository dependencies pacman will install: %1").arg(build.repoDependencies.join(", "));
    if (!build.unresolved.isEmpty())
        planLines << tr("<b>Couldn't resolve:</b> %1 — the build will likely fail.").arg(build.unresolved.join(", "));
    for (const QString &warning : structuralWarnings)
        planLines << tr("<b>Note:</b> %1").arg(warning.toHtmlEscaped());
    auto *planLabel = new QLabel(planLines.join(QStringLiteral("<br>")), this);
    planLabel->setWordWrap(true);
    planLabel->setTextFormat(Qt::RichText);

    m_packageList = new QListWidget(this);
    AthenaStyle::makeSidebar(m_packageList);
    for (int i = 0; i < m_packages.size(); ++i) {
        new QListWidgetItem(m_packageList);
        updatePackageItem(i);
    }

    m_packageInfo = new QLabel(this);
    m_packageInfo->setWordWrap(true);
    m_packageInfo->setTextFormat(Qt::RichText);
    m_packageInfo->setOpenExternalLinks(true);

    m_fileCombo = new QComboBox(this);
    m_fileCombo->setMinimumContentsLength(24);
    m_revertButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-revert")), tr("Revert File"), this);
    m_revertButton->setEnabled(false);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("File:"), this));
    fileRow->addWidget(m_fileCombo, 1);
    fileRow->addWidget(m_revertButton);

    m_editor = new QPlainTextEdit(this);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));

    auto *warningsLabel = new QLabel(tr("Worth a closer look in this file (heuristic hints, not a verdict):"), this);
    m_warningList = new QListWidget(this);
    m_warningList->setMaximumHeight(100);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->setContentsMargins(AthenaStyle::kSidebarGap, 0, 0, 0);
    rightLayout->addWidget(m_packageInfo);
    rightLayout->addLayout(fileRow);
    rightLayout->addWidget(m_editor, 1);
    rightLayout->addWidget(warningsLabel);
    rightLayout->addWidget(m_warningList);
    auto *rightPanel = new QWidget(this);
    rightPanel->setLayout(rightLayout);

    auto *splitter = new QSplitter(this);
    AthenaStyle::makeSidebarSplitter(splitter);
    splitter->addWidget(AthenaStyle::wrapSidebar(m_packageList, this));
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({260, 890});

    m_confirmCheck = new QCheckBox(tr("I have reviewed these build files and trust what they do"), this);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_buildButton = buttons->addButton(tr("Build && Install"), QDialogButtonBox::AcceptRole);
    m_buildButton->setEnabled(false);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_confirmCheck, 1);
    bottomRow->addWidget(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(noticeLabel);
    layout->addWidget(planLabel);
    layout->addWidget(splitter, 1);
    layout->addLayout(bottomRow);

    connect(m_packageList, &QListWidget::currentRowChanged, this, &AurReviewDialog::onPackageSelected);
    connect(m_fileCombo, &QComboBox::currentIndexChanged, this, &AurReviewDialog::onFileSelected);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &AurReviewDialog::onEditorChanged);
    connect(m_revertButton, &QPushButton::clicked, this, &AurReviewDialog::revertCurrentFile);
    connect(m_warningList, &QListWidget::currentRowChanged, this, &AurReviewDialog::jumpToWarning);
    connect(m_confirmCheck, &QCheckBox::toggled, this, &AurReviewDialog::updateBuildEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &AurReviewDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AurReviewDialog::reject);

    if (!m_packages.isEmpty())
        m_packageList->setCurrentRow(0);
}

AurReviewDialog::ReviewFile *AurReviewDialog::currentFile()
{
    if (m_currentPackage < 0 || m_currentFile < 0 || m_currentFile >= m_packages[m_currentPackage].files.size())
        return nullptr;
    return &m_packages[m_currentPackage].files[m_currentFile];
}

void AurReviewDialog::onPackageSelected(int row)
{
    if (row < 0 || row >= m_packages.size())
        return;

    m_currentPackage = row;
    m_currentFile = -1;
    ReviewPackage &pkg = m_packages[row];
    pkg.viewed = true;
    updatePackageItem(row);
    updateBuildEnabled();

    const AurBackend::AurPackage &info = pkg.source.info;
    QStringList facts;
    facts << tr("%n vote(s)", "", info.numVotes);
    facts << (info.maintainer.isEmpty() ? tr("<b>orphaned</b> (no maintainer)")
                                        : tr("maintained by %1").arg(info.maintainer.toHtmlEscaped()));
    if (info.outOfDate > 0) {
        facts << tr("<b>flagged out of date</b> since %1")
                     .arg(QDateTime::fromSecsSinceEpoch(info.outOfDate).date().toString(Qt::ISODate));
    }
    const QString aurUrl = QStringLiteral("https://aur.archlinux.org/packages/%1").arg(info.name.toHtmlEscaped());
    m_packageInfo->setText(tr("<span style=\"font-size:large\"><b>%1</b> %2</span>%3<br>%4<br>%5 · <a href=\"%6\">%6</a>")
                               .arg(info.name.toHtmlEscaped(), info.version.toHtmlEscaped(),
                                    pkg.source.asDependency ? tr(" — dependency") : QString(),
                                    info.description.toHtmlEscaped(), facts.join(QStringLiteral(" · ")), aurUrl));

    const QSignalBlocker blocker(m_fileCombo);
    m_fileCombo->clear();
    for (const ReviewFile &file : pkg.files)
        m_fileCombo->addItem(QFileInfo(file.path).fileName());
    if (!pkg.files.isEmpty()) {
        m_fileCombo->setCurrentIndex(0);
        onFileSelected(0);
    } else {
        m_loadingEditor = true;
        m_editor->setPlainText(tr("(no reviewable files)"));
        m_editor->setReadOnly(true);
        m_loadingEditor = false;
        refreshWarnings();
    }
}

void AurReviewDialog::onFileSelected(int index)
{
    if (m_currentPackage < 0 || index < 0 || index >= m_packages[m_currentPackage].files.size())
        return;

    m_currentFile = index;
    const ReviewFile &file = m_packages[m_currentPackage].files[index];
    m_loadingEditor = true;
    m_editor->setReadOnly(false);
    m_editor->setPlainText(file.current);
    m_loadingEditor = false;
    m_revertButton->setEnabled(file.current != file.original);
    refreshWarnings();
}

void AurReviewDialog::onEditorChanged()
{
    if (m_loadingEditor)
        return;
    ReviewFile *file = currentFile();
    if (!file)
        return;

    file->current = m_editor->toPlainText();
    const bool edited = file->current != file->original;
    m_revertButton->setEnabled(edited);
    const QString name = QFileInfo(file->path).fileName();
    m_fileCombo->setItemText(m_currentFile, edited ? tr("%1 (edited)").arg(name) : name);
    updatePackageItem(m_currentPackage);
    refreshWarnings();
}

void AurReviewDialog::revertCurrentFile()
{
    ReviewFile *file = currentFile();
    if (!file)
        return;
    file->current = file->original;
    m_loadingEditor = true;
    m_editor->setPlainText(file->current);
    m_loadingEditor = false;
    m_fileCombo->setItemText(m_currentFile, QFileInfo(file->path).fileName());
    m_revertButton->setEnabled(false);
    updatePackageItem(m_currentPackage);
    refreshWarnings();
}

void AurReviewDialog::refreshWarnings()
{
    m_warningList->clear();
    const ReviewFile *file = currentFile();
    if (!file)
        return;

    const QStringList lines = file->current.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith('#'))
            continue;
        for (const RiskPattern &risk : riskPatterns()) {
            if (!risk.pattern.match(lines[i]).hasMatch())
                continue;
            auto *item = new QListWidgetItem(
                tr("Line %1: %2 — %3").arg(i + 1).arg(risk.reason, trimmed.left(100)), m_warningList);
            item->setData(kLineRole, i);
            item->setIcon(pendingIcon());
        }
    }
    if (m_warningList->count() == 0)
        new QListWidgetItem(tr("Nothing flagged — still read it yourself."), m_warningList);
}

void AurReviewDialog::jumpToWarning(int row)
{
    QListWidgetItem *item = m_warningList->item(row);
    if (!item || !item->data(kLineRole).isValid())
        return;
    const QTextBlock block = m_editor->document()->findBlockByNumber(item->data(kLineRole).toInt());
    QTextCursor cursor(block);
    cursor.select(QTextCursor::LineUnderCursor);
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    m_editor->setFocus();
}

void AurReviewDialog::updatePackageItem(int row)
{
    QListWidgetItem *item = m_packageList->item(row);
    if (!item)
        return;
    const ReviewPackage &pkg = m_packages[row];
    const bool edited = std::any_of(pkg.files.begin(), pkg.files.end(),
                                    [](const ReviewFile &f) { return f.current != f.original; });

    QString text = pkg.source.info.packageBase;
    if (pkg.source.asDependency)
        text += tr(" (dependency)");
    if (edited)
        text += tr(" — edited");
    item->setText(text);
    item->setIcon(pkg.viewed ? viewedIcon() : pendingIcon());
    item->setToolTip(pkg.viewed ? tr("Opened for review") : tr("Not opened yet"));
}

void AurReviewDialog::updateBuildEnabled()
{
    const bool allViewed =
        std::all_of(m_packages.begin(), m_packages.end(), [](const ReviewPackage &p) { return p.viewed; });
    m_buildButton->setEnabled(!m_packages.isEmpty() && allViewed && m_confirmCheck->isChecked());
    m_buildButton->setToolTip(allViewed ? QString() : tr("Open every package in the list first."));
}

void AurReviewDialog::accept()
{
    // QSaveFile writes a temporary file and renames it over the target,
    // so an edit can never be redirected through a symlink planted in the
    // cloned repo.
    for (const ReviewPackage &pkg : m_packages) {
        for (const ReviewFile &file : pkg.files) {
            if (file.current == file.original)
                continue;
            QSaveFile out(file.path);
            if (!out.open(QIODevice::WriteOnly) || out.write(file.current.toUtf8()) < 0 || !out.commit()) {
                QMessageBox::critical(this, windowTitle(),
                                      tr("Couldn't save your changes to %1:\n%2").arg(file.path, out.errorString()));
                return;
            }
        }
    }
    QDialog::accept();
}

void AurReviewDialog::reviewAndBuild(QWidget *parent, const QStringList &packageNames, bool reinstall,
                                     std::function<void(bool)> onFinished)
{
    if (packageNames.isEmpty()) {
        onFinished(false);
        return;
    }

    auto *progress = new QProgressDialog(tr("Downloading build files for review..."), QString(), 0, 0, parent);
    progress->setWindowTitle(tr("Review AUR Build Files"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->show();

    auto *watcher = new QFutureWatcher<AurBackend::PreparedBuild>();
    QObject::connect(
        watcher, &QFutureWatcher<AurBackend::PreparedBuild>::finished, parent,
        [watcher, progress, parent, reinstall, onFinished]() {
            const AurBackend::PreparedBuild build = watcher->result();
            watcher->deleteLater();
            progress->deleteLater();

            if (!build.error.isEmpty()) {
                QMessageBox::critical(parent, tr("Review AUR Build Files"), build.error);
                onFinished(false);
                return;
            }

            // Deliberately not skipped by "run transactions without
            // confirmation": this is a security review, not an "are you sure".
            AurReviewDialog dialog(build, parent);
            if (dialog.exec() != QDialog::Accepted) {
                onFinished(false);
                return;
            }

            TerminalOutputDialog terminal(tr("Build AUR packages"), parent);
            onFinished(terminal.runCommandsModal(AurBackend::reviewedBuildCommands(build, reinstall),
                                                 AppSettings::instance().autoCloseTerminalOnSuccess()));
        });
    watcher->setFuture(QtConcurrent::run([packageNames]() { return AurBackend::prepareReviewedBuild(packageNames); }));
}
