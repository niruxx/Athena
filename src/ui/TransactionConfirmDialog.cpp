#include "TransactionConfirmDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "../core/AppSettings.h"

TransactionConfirmDialog::TransactionConfirmDialog(const QString &title, const QString &summary,
                                                     const QString &planText, QWidget *parent,
                                                     bool requiresPrivileges)
    : QDialog(parent)
{
    setWindowTitle(title);

    auto *summaryLabel = new QLabel(summary, this);
    summaryLabel->setWordWrap(true);
    QFont summaryFont = summaryLabel->font();
    summaryFont.setBold(true);
    summaryFont.setPointSize(summaryFont.pointSize() + 1);
    summaryLabel->setFont(summaryFont);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(summaryLabel);

    if (!planText.isEmpty()) {
        auto *detailsHeader = new QLabel(tr("This will make the following changes:"), this);
        layout->addWidget(detailsHeader);

        auto *planView = new QPlainTextEdit(this);
        planView->setPlainText(planText);
        planView->setReadOnly(true);
        planView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        planView->setLineWrapMode(QPlainTextEdit::NoWrap);
        // No custom colors: inherits the app's own palette (light/dark),
        // just a plain framed text panel — unlike TerminalOutputDialog,
        // which intentionally always looks like a terminal.
        layout->addWidget(planView, 1);
        resize(520, 380);
    } else {
        resize(420, 140);
    }

    if (requiresPrivileges) {
        auto *privilegeNote = new QLabel(tr("This requires administrator privileges."), this);
        QFont noteFont = privilegeNote->font();
        noteFont.setItalic(true);
        privilegeNote->setFont(noteFont);
        layout->addWidget(privilegeNote);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
    buttons->button(QDialogButtonBox::No)->setDefault(true);
    buttons->button(QDialogButtonBox::No)->setFocus();
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool TransactionConfirmDialog::confirm(QWidget *parent, const QString &title, const QString &summary,
                                        const QString &planText, bool requiresPrivileges)
{
    if (AppSettings::instance().autoConfirmTransactions())
        return true;

    TransactionConfirmDialog dialog(title, summary, planText, parent, requiresPrivileges);
    return dialog.exec() == QDialog::Accepted;
}
