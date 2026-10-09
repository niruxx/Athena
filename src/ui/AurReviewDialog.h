#pragma once

#include <functional>

#include <QDialog>
#include <QMap>
#include <QStringList>

#include "../core/backends/AurBackend.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Mandatory pre-build review for AUR packages: lists every package base
// about to be built (dependencies included), shows each one's build files
// (PKGBUILD, .install scripts, patches...) in an editor, flags lines worth
// a closer look, and only enables "Build & Install" once every package has
// been opened and the user confirms. Edits are written back into the
// cloned directories that are then built as-is.
class AurReviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit AurReviewDialog(const AurBackend::PreparedBuild &build, QWidget *parent = nullptr);

    // The whole flow, for any caller about to build AUR packages: fetches
    // the build files in the background, shows this dialog, then runs the
    // reviewed build in a TerminalOutputDialog. onFinished(success) fires
    // exactly once, on the UI thread — false if anything failed or the user
    // backed out at any step.
    static void reviewAndBuild(QWidget *parent, const QStringList &packageNames, bool reinstall,
                               std::function<void(bool success)> onFinished);

    void accept() override;

private:
    struct ReviewFile {
        QString path;
        QString original;
        QString current;
    };
    struct ReviewPackage {
        AurBackend::PreparedBuild::Package source;
        QVector<ReviewFile> files;
        bool viewed = false;
    };

    void onPackageSelected(int row);
    void onFileSelected(int index);
    void onEditorChanged();
    void revertCurrentFile();
    void refreshWarnings();
    void jumpToWarning(int row);
    void updatePackageItem(int row);
    void updateBuildEnabled();
    ReviewFile *currentFile();

    QVector<ReviewPackage> m_packages;
    int m_currentPackage = -1;
    int m_currentFile = -1;
    bool m_loadingEditor = false;

    QListWidget *m_packageList;
    QLabel *m_packageInfo;
    QComboBox *m_fileCombo;
    QPushButton *m_revertButton;
    QPlainTextEdit *m_editor;
    QListWidget *m_warningList;
    QCheckBox *m_confirmCheck;
    QPushButton *m_buildButton;
};
