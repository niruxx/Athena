#include "CatalogActions.h"

#include <memory>

#include <QFileDialog>
#include <QListView>
#include <QMap>
#include <QMenu>

#include "../core/backends/AurBackend.h"
#include "AppCatalog.h"
#include "CatalogHub.h"
#include "DetailsSheet.h"
#include "MaterialTheme.h"
#include "PackageActions.h"

namespace CatalogActions {

namespace {

// Calls `step(source, names, next)` for each source in turn, then `done`
// with whether every step succeeded.
void perSource(const QVector<CatalogVariant> &variants,
               std::function<void(int source, const QStringList &names, std::function<void(bool)> next)> step,
               std::function<void(bool)> done)
{
    QMap<int, QStringList> bySource;
    for (const CatalogVariant &v : variants)
        bySource[v.source].append(v.info.name);
    auto order = std::make_shared<QList<int>>(bySource.keys());
    auto allOk = std::make_shared<bool>(true);
    auto next = std::make_shared<std::function<void()>>();
    *next = [bySource, order, allOk, next, step, done]() {
        if (order->isEmpty()) {
            done(*allOk);
            return;
        }
        const int source = order->takeFirst();
        step(source, bySource[source], [allOk, next](bool ok) {
            *allOk = *allOk && ok;
            (*next)();
        });
    };
    (*next)();
}

QIcon menuIcon(const QString &name)
{
    return MaterialTheme::icon(name, MaterialTheme::current().onSurfaceVariant, 18);
}

} // namespace

void run(QWidget *parent, const QVector<CatalogSource> &sources, const QVector<CatalogVariant> &variants, Action action,
         std::function<void(bool)> done)
{
    if (variants.isEmpty()) {
        done(false);
        return;
    }
    perSource(
        variants,
        [parent, sources, action](int source, const QStringList &names, std::function<void(bool)> next) {
            PackageBackend *backend = sources[source].backend;
            switch (action) {
            case Action::Install:
                PackageActions::installPackages(parent, backend, names, next);
                break;
            case Action::Remove:
                PackageActions::removePackages(parent, backend, names, next);
                break;
            case Action::Reinstall:
                PackageActions::reinstallPackages(parent, backend, names, next);
                break;
            case Action::Upgrade:
                PackageActions::upgradePackages(parent, backend, names, next);
                break;
            }
        },
        [done](bool ok) {
            AppCatalog::invalidate();
            done(ok);
        });
}

void download(QWidget *parent, const QVector<CatalogSource> &sources, const QVector<CatalogVariant> &variants,
              bool includeDependencies, std::function<void(bool)> done)
{
    if (variants.isEmpty()) {
        done(false);
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(parent, QObject::tr("Choose Download Folder"));
    if (folder.isEmpty()) {
        done(false);
        return;
    }
    perSource(
        variants,
        [parent, sources, folder, includeDependencies](int source, const QStringList &names, std::function<void(bool)> next) {
            PackageActions::downloadPackages(parent, sources[source].backend, names, folder, includeDependencies, next);
        },
        done);
}

QVector<CatalogVariant> preferredVariants(const QVector<CatalogItem> &items)
{
    QVector<CatalogVariant> variants;
    for (const CatalogItem &item : items)
        variants.append(item.variants[item.preferredVariant()]);
    return variants;
}

void attachContextMenu(QListView *view, CatalogModel *model, CatalogHub *hub, DetailsSheet *sheet,
                       std::function<void(int)> enterSelection)
{
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QListView::customContextMenuRequested, view, [view, model, hub, sheet, enterSelection](const QPoint &pos) {
        const QModelIndex index = view->indexAt(pos);
        if (!index.isValid())
            return;
        const int row = index.row();
        const CatalogItem item = model->item(row);
        const CatalogVariant variant = item.variants[item.preferredVariant()];
        const CatalogSource &source = hub->sources()[variant.source];
        const bool aurReview = source.key == QLatin1String("aur") && AurBackend::reviewRequired();

        // Is there a pending update for this exact package?
        bool hasUpdate = false;
        for (const CatalogItem &update : hub->updates()) {
            const CatalogVariant &u = update.variants.first();
            if (u.source == variant.source && u.info.name == variant.info.name)
                hasUpdate = true;
        }

        QWidget *parent = view->window();
        auto act = [parent, hub](const CatalogVariant &v, Action action) {
            run(parent, hub->sources(), {v}, action, [hub](bool ok) {
                if (ok)
                    hub->refreshAll();
            });
        };

        QMenu menu(view);
        menu.addAction(menuIcon(QStringLiteral("info")), QObject::tr("Details"), view, [sheet, item, hasUpdate]() {
            sheet->open(item, hasUpdate);
        });
        menu.addSeparator();
        if (variant.info.installed) {
            if (hasUpdate)
                menu.addAction(menuIcon(aurReview ? QStringLiteral("verified_user") : QStringLiteral("system_update_alt")),
                               aurReview ? QObject::tr("Review && Update") : QObject::tr("Update"), view,
                               [act, variant]() { act(variant, Action::Upgrade); });
            menu.addAction(menuIcon(QStringLiteral("restart_alt")), aurReview ? QObject::tr("Review && Reinstall") : QObject::tr("Reinstall"),
                           view, [act, variant]() { act(variant, Action::Reinstall); });
            menu.addAction(menuIcon(QStringLiteral("delete")), QObject::tr("Remove"), view,
                           [act, variant]() { act(variant, Action::Remove); });
        } else if (item.variants.size() > 1) {
            // Offered by several sources: pick which one to install from.
            QMenu *from = menu.addMenu(menuIcon(QStringLiteral("download")), QObject::tr("Install from"));
            for (const CatalogVariant &v : item.variants) {
                const CatalogSource &s = hub->sources()[v.source];
                from->addAction(menuIcon(s.icon), s.label, view, [act, v]() { act(v, Action::Install); });
            }
        } else {
            menu.addAction(menuIcon(aurReview ? QStringLiteral("verified_user") : QStringLiteral("download")),
                           aurReview ? QObject::tr("Review && Install") : QObject::tr("Install"), view,
                           [act, variant]() { act(variant, Action::Install); });
        }
        if (enterSelection) {
            menu.addSeparator();
            menu.addAction(menuIcon(QStringLiteral("checklist")), QObject::tr("Select"), view, [enterSelection, row]() { enterSelection(row); });
        }
        menu.exec(view->viewport()->mapToGlobal(pos));
    });
}

} // namespace CatalogActions
