#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "../core/PackageBackend.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class PackageBrowser;

class SearchPage : public QWidget {
    Q_OBJECT

public:
    explicit SearchPage(PackageBackend *backend, QWidget *parent = nullptr);

    PackageBrowser *browser() const { return m_browser; }

private slots:
    void runSearch();
    void onSearchFinished();
    void onDependencyQueryToggled(bool checked);
    void applyResultFilters();
    void downloadSelected();
    void downloadSelectedWithDependencies();

private:
    // Rebuilds the Repository/Architecture filter combos from the distinct
    // values seen in m_allResults, keeping the current selection if it's
    // still one of them.
    void populateFilterCombos();
    void runDownload(bool includeDependencies);
    // PackageTableModel::setPackages() pre-checks already-installed rows
    // without emitting checkedChanged (only explicit checkbox toggles do),
    // so applyResultFilters() calls this directly rather than relying on
    // the signal alone.
    void updateDownloadButtonsEnabled();

    PackageBackend *m_backend;
    PackageBrowser *m_browser;
    QLineEdit *m_queryEdit;
    QPushButton *m_searchButton;
    QLabel *m_statusLabel;
    QFutureWatcher<QVector<PackageInfo>> m_watcher;

    QCheckBox *m_dependencyQueryCheck;
    QComboBox *m_dependencyDirectionCombo;
    QComboBox *m_repositoryFilterCombo;
    QComboBox *m_architectureFilterCombo;
    QPushButton *m_downloadButton;
    QPushButton *m_downloadWithDepsButton;

    // The full, unfiltered result set from the last search/dependency
    // query; the Repository/Architecture combos slice this client-side
    // rather than re-querying the backend on every filter change.
    QVector<PackageInfo> m_allResults;
};
