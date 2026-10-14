#pragma once

#include <QFrame>
#include <QFutureWatcher>

#include "Catalog.h"

class FlatpakBackend;
class QGraphicsOpacityEffect;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

// Material 3 modal side sheet with one app's or package's details: which
// sources it's available from (switchable), the main install / remove /
// update / open actions, its description, key facts, and — for Flatpak
// apps — a summary of what its sandbox allows. Slides in over the content
// from the right edge of its parent, above a scrim.
class DetailsSheet : public QFrame {
    Q_OBJECT

public:
    DetailsSheet(const QVector<CatalogSource> *sources, FlatpakBackend *flatpak, QWidget *parent);

    // offerUpdate: show "Update" for installed variants with a newer
    // version (the Updates page opens the sheet this way).
    void open(const CatalogItem &item, bool offerUpdate = false);
    void close();
    bool isOpen() const { return m_open; }

signals:
    // A transaction from the sheet changed what's installed.
    void packagesChanged();
    void message(const QString &text);
    // The user asked to edit a Flatpak app's sandbox permissions.
    void editPermissions(const QString &appId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Details {
        PackageInfo info;
        QStringList permissions;      // Flatpak sandbox summary
        QStringList riskyPermissions; // the subset worth flagging
    };

    void showVariant(int index);
    void rebuildSourceButtons();
    void rebuildActions();
    void onDetailsLoaded();
    void render(const Details &details);
    void reposition();
    void runTransaction(const QString &kind);
    void showScreenshots(const QStringList &urls);
    static Details loadDetails(PackageBackend *backend, FlatpakBackend *flatpak, const QString &sourceKey,
                               const PackageInfo &info);

    const QVector<CatalogSource> *m_sources;
    FlatpakBackend *m_flatpak;
    CatalogItem m_item;
    int m_variant = 0;
    bool m_offerUpdate = false;
    bool m_open = false;

    QWidget *m_scrim;
    QGraphicsOpacityEffect *m_scrimEffect;
    QPropertyAnimation *m_animation;
    QLabel *m_icon;
    QLabel *m_name;
    QLabel *m_subtitle;
    QHBoxLayout *m_sourceRow;
    QHBoxLayout *m_actionRow;
    QLabel *m_description;
    QGridLayout *m_facts;
    QLabel *m_homepage;
    QWidget *m_permissionsBox;
    QLabel *m_permissions;
    QWidget *m_shotsBox;
    class QHBoxLayout *m_shotsRow;
    int m_shotsToken = 0;
    QFrame *m_callout;
    QLabel *m_calloutText;
    QFutureWatcher<Details> m_watcher;
};
