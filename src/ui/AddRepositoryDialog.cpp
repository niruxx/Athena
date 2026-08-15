#include "AddRepositoryDialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

AddRepositoryDialog::AddRepositoryDialog(const QVector<RepositoryAddField> &fields, QWidget *parent)
    : QDialog(parent), m_fields(fields)
{
    setWindowTitle(tr("Add Repository"));

    auto *form = new QFormLayout;
    for (const RepositoryAddField &field : m_fields) {
        auto *edit = new QLineEdit(this);
        edit->setPlaceholderText(field.placeholder);
        m_edits[field.key] = edit;
        form->addRow(field.required ? field.label + QStringLiteral(" *") : field.label, edit);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddRepositoryDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    if (std::any_of(m_fields.begin(), m_fields.end(), [](const RepositoryAddField &f) { return f.required; }))
        layout->addWidget(new QLabel(tr("* required"), this));
    layout->addWidget(buttons);
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
            QMessageBox::warning(this, tr("Missing Field"), tr("\"%1\" is required.").arg(field.label));
            m_edits[field.key]->setFocus();
            return;
        }
    }
    accept();
}
