#pragma once

#include <QFutureWatcher>
#include <QPair>
#include <QWidget>

class AurBackend;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Read-only PKGBUILD viewer for the AUR group: AUR packages are arbitrary
// user-submitted build scripts, and the AUR helpers' own interactive
// review step is skipped when Athena drives them, so this is where to read
// one before installing it.
class AurPkgbuildPage : public QWidget {
    Q_OBJECT

public:
    explicit AurPkgbuildPage(AurBackend *backend, QWidget *parent = nullptr);

protected:
    // Re-reads the build method each time the tab is shown, so installing
    // a helper from Preferences is reflected without a restart.
    void showEvent(QShowEvent *event) override;

private slots:
    void loadPkgbuild();
    void onLoaded();
    void openAurPage();

private:
    AurBackend *m_backend;
    QLabel *m_methodLabel;
    QLineEdit *m_nameEdit;
    QPushButton *m_loadButton;
    QPushButton *m_openPageButton;
    QLabel *m_statusLabel;
    QPlainTextEdit *m_view;
    // (PKGBUILD text, error message)
    QFutureWatcher<QPair<QString, QString>> m_watcher;
    QString m_loadedName;
};
