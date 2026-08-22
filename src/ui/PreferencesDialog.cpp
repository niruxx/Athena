#include "PreferencesDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "SettingsPage.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new SettingsPage(this));

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}
