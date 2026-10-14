#pragma once

#include <functional>

#include <QVector>

#include "Catalog.h"

class CatalogHub;
class DetailsSheet;
class QListView;
class QPoint;
class QWidget;

// The package actions of the redesigned pages, shared by the details
// sheet, right-click menus, and selection bars: one item or a batch, any mix
// of sources (each source's packages go to it as one transaction, one
// source after another), through the same PackageActions flows the classic
// pages use — so AUR builds still go through PKGBUILD review.
namespace CatalogActions {

enum class Action { Install, Remove, Reinstall, Upgrade };

void run(QWidget *parent, const QVector<CatalogSource> &sources, const QVector<CatalogVariant> &variants, Action action,
         std::function<void(bool success)> done);

// Downloads without installing, into a folder the user picks.
void download(QWidget *parent, const QVector<CatalogSource> &sources, const QVector<CatalogVariant> &variants,
              bool includeDependencies, std::function<void(bool success)> done);

// The variant each item's action applies to: its installed one, else its
// first source.
QVector<CatalogVariant> preferredVariants(const QVector<CatalogItem> &items);

// Right-click menu for a catalog list or card grid: Details, then whatever
// applies to the item (Install / Install from…, Update, Reinstall, Remove),
// then Select when the page has a selection mode (`enterSelection` gets the
// clicked row). Transactions refresh the hub when they finish.
void attachContextMenu(QListView *view, CatalogModel *model, CatalogHub *hub, DetailsSheet *sheet,
                       std::function<void(int row)> enterSelection = {});

} // namespace CatalogActions
