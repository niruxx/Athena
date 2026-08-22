#include "FirstRunDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "PackageFormatInstallWidget.h"

FirstRunDialog::FirstRunDialog(const QString &detectedBackendName, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Athena"));
    setMinimumWidth(480);

    auto *titleLabel = new QLabel(tr("Welcome to Athena"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleLabel->setFont(titleFont);

    auto *introLabel = new QLabel(
        tr("Athena is a cross-distro GUI package manager: it drives your system's native package "
           "manager (DNF, APT, or Pacman), plus Flatpak and Snap side by side in the same window."),
        this);
    introLabel->setWordWrap(true);

    const QString backendText = !detectedBackendName.isEmpty()
        ? tr("Detected system package manager: %1").arg(detectedBackendName)
        : tr("No supported system package manager (DNF, APT, or Pacman) was detected.");
    auto *backendLabel = new QLabel(backendText, this);
    backendLabel->setWordWrap(true);
    QFont backendFont = backendLabel->font();
    backendFont.setItalic(true);
    backendLabel->setFont(backendFont);

    auto *formatsGroup = new QGroupBox(tr("Additional Package Formats"), this);
    auto *formatsLayout = new QVBoxLayout(formatsGroup);
    formatsLayout->addWidget(new PackageFormatInstallWidget(formatsGroup));

    auto *noteLabel =
        new QLabel(tr("You can change theme, startup tab, and more anytime in Settings."), this);
    noteLabel->setWordWrap(true);
    QFont noteFont = noteLabel->font();
    noteFont.setItalic(true);
    noteLabel->setFont(noteFont);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Get Started"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(introLabel);
    layout->addWidget(backendLabel);
    layout->addWidget(formatsGroup);
    layout->addWidget(noteLabel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    // However it closes — the button, Escape, or the window's own close
    // control — it should never be shown again.
    connect(this, &QDialog::finished, this, []() { AppSettings::instance().setHasCompletedFirstRun(true); });
}
