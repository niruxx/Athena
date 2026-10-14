#include "TransactionConfirmDialog.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "MaterialTheme.h"

TransactionConfirmDialog::TransactionConfirmDialog(const QString &title, const QString &summary,
                                                     const QString &planText, QWidget *parent,
                                                     bool requiresPrivileges)
    : M3Dialog(parent)
{
    setHeadline(title, QStringLiteral("help"));
    setSupportingText(summary);

    if (!planText.isEmpty()) {
        body()->addWidget(new QLabel(tr("This will make the following changes:"), this));
        auto *planView = new QPlainTextEdit(this);
        planView->setPlainText(planText);
        planView->setReadOnly(true);
        planView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        planView->setLineWrapMode(QPlainTextEdit::NoWrap);
        planView->setMinimumHeight(200);
        body()->addWidget(planView, 1);
        setResizable(true);
        resize(620, 520);
    }

    if (requiresPrivileges) {
        auto *note = new QHBoxLayout;
        note->setSpacing(8);
        auto *lock = new QLabel(this);
        lock->setPixmap(MaterialTheme::icon(QStringLiteral("lock"), MaterialTheme::current().onSurfaceVariant, 16).pixmap(16, 16));
        auto *noteText = new QLabel(tr("Your password will be asked for, through the system's authentication prompt."), this);
        noteText->setProperty("m3", QStringLiteral("muted"));
        noteText->setWordWrap(true);
        note->addWidget(lock, 0, Qt::AlignTop);
        note->addWidget(noteText, 1);
        body()->addLayout(note);
    }

    QPushButton *cancel = addAction(tr("Cancel"), QStringLiteral("text"));
    // Name the confirming button after the action ("Uninstall", "Update
    // All") rather than a bare "Yes", when the title is short enough.
    QPushButton *confirm = addAction(title.size() <= 28 ? title : tr("Continue"), QStringLiteral("filled"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    // Declining stays the safe default for Enter.
    confirm->setDefault(false);
    cancel->setDefault(true);
    cancel->setFocus();
}

bool TransactionConfirmDialog::confirm(QWidget *parent, const QString &title, const QString &summary,
                                        const QString &planText, bool requiresPrivileges)
{
    if (AppSettings::instance().autoConfirmTransactions())
        return true;

    TransactionConfirmDialog dialog(title, summary, planText, parent, requiresPrivileges);
    return dialog.exec() == QDialog::Accepted;
}
