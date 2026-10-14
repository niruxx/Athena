#include "AddRepositoryDialog.h"

#include <algorithm>

#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

AddRepositoryDialog::AddRepositoryDialog(const QVector<RepositoryAddField> &fields, QWidget *parent)
    : M3Dialog(parent), m_fields(fields)
{
    setHeadline(tr("Add Repository"), QStringLiteral("add"));
    setSupportingText(tr("Packages from it become available once package lists are refreshed."));

    auto *form = new QFormLayout;
    for (const RepositoryAddField &field : m_fields) {
        auto *edit = new QLineEdit(this);
        edit->setPlaceholderText(field.placeholder);
        m_edits[field.key] = edit;
        form->addRow(field.required ? field.label + QStringLiteral(" *") : field.label, edit);
    }

    body()->addLayout(form);
    if (std::any_of(m_fields.begin(), m_fields.end(), [](const RepositoryAddField &f) { return f.required; })) {
        auto *note = new QLabel(tr("* required"), this);
        note->setProperty("m3", QStringLiteral("muted"));
        body()->addWidget(note);
    }
    QPushButton *cancel = addAction(tr("Cancel"), QStringLiteral("text"));
    QPushButton *add = addAction(tr("Add"), QStringLiteral("filled"), QStringLiteral("add"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, &AddRepositoryDialog::validateAndAccept);
}

RepositoryAddValues AddRepositoryDialog::values() const
{
    RepositoryAddValues result;
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it)
        result[it.key()] = it.value()->text().trimmed();
    return result;
}

void AddRepositoryDialog::validateAndAccept()
{
    for (const RepositoryAddField &field : m_fields) {
        if (field.required && m_edits[field.key]->text().trimmed().isEmpty()) {
            M3Message::warning(this, tr("Missing Field"), tr("\"%1\" is required.").arg(field.label));
            m_edits[field.key]->setFocus();
            return;
        }
    }
    accept();
}
