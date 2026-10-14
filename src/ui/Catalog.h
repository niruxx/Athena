#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QIcon>
#include <QStyledItemDelegate>
#include <QVector>

#include "../core/AppStreamIndex.h"
#include "../core/PackageBackend.h"

// The cross-source view of packages that the redesigned pages (Discover,
// Installed, Updates, the details sheet) share: one item per app or
// package, each with one variant per source it's available from — so
// Firefox from pacman and Firefox from Flathub show up as one card with
// two sources rather than two unrelated rows.

struct CatalogSource {
    PackageBackend *backend = nullptr;
    QString key;   // "pacman", "dnf", "apt", "aur", "flatpak", "snap"
    QString label; // shown on source chips
    QString icon;  // Material Symbols name
};

struct CatalogVariant {
    int source = -1; // index into the sources vector
    PackageInfo info;
};

struct CatalogItem {
    QString title;
    QString subtitle;
    QIcon icon; // the app's own icon, when it has an installed .desktop entry
    bool isApp = false;
    QStringList screenshots; // remote image URLs, from AppStream
    QVector<CatalogVariant> variants;

    bool installed() const;
    // The variant to show first: the installed one, else the first source.
    int preferredVariant() const;
};

namespace Catalog {

CatalogItem fromPackage(const QVector<CatalogSource> &sources, int source, const PackageInfo &package);
// An app from the AppStream index (installed or not), offered by `source`.
CatalogItem fromAppStream(int source, const AppStreamApp &app);
// Folds apps with the same name from different sources into one item;
// non-app packages are left as they are.
QVector<CatalogItem> mergeApps(const QVector<CatalogItem> &items);

} // namespace Catalog

class CatalogModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { ItemRole = Qt::UserRole + 1 };

    explicit CatalogModel(QObject *parent = nullptr);

    void setItems(QVector<CatalogItem> items);
    const QVector<CatalogItem> &items() const { return m_items; }
    const CatalogItem &item(int row) const { return m_items[row]; }

    // Batch selection: while on, rows show a check circle and clicking one
    // toggles it instead of opening the details sheet. Selection is kept
    // by package identity, so it survives a refresh of the list.
    void setSelectionMode(bool on);
    bool selectionMode() const { return m_selectionMode; }
    void toggleSelected(int row);
    void selectAll();
    bool isSelected(int row) const;
    QVector<CatalogItem> selectedItems() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

signals:
    void selectionChanged();

private:
    static QString keyOf(const CatalogItem &item);

    QVector<CatalogItem> m_items;
    bool m_selectionMode = false;
    QSet<QString> m_selected;
};

// A 68px M3 list row: icon, title, subtitle, source chips, and version.
// Consecutive rows read as one rounded card (first/last rows get the
// rounded corners).
class CatalogRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum class Kind { Installed, Update, Package };
    CatalogRowDelegate(const QVector<CatalogSource> *sources, Kind kind, QObject *parent = nullptr);

    // 68px, or 56px with Settings' compact rows.
    static int rowHeight();

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    const QVector<CatalogSource> *m_sources;
    Kind m_kind;
};

// A Discover app card: icon, name, two-line summary, and source chips.
class CatalogCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit CatalogCardDelegate(const QVector<CatalogSource> *sources, QObject *parent = nullptr);

    static constexpr int kCardWidth = 300; // minimum; see setCardWidth()
    static constexpr int kCardHeight = 172;
    // Cards stretch to fill their grid cell (set by the page on resize).
    void setCardWidth(int width) { m_cardWidth = width; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    const QVector<CatalogSource> *m_sources;
    int m_cardWidth = kCardWidth;
};

namespace CatalogPaint {
// The app's icon, or a tonal tile with a glyph when it has none.
void appIcon(QPainter *painter, const QRect &rect, const CatalogItem &item, const QString &fallbackGlyph);
// A small source chip; returns its width.
int sourceChip(QPainter *painter, const QPoint &topLeft, const CatalogSource &source);
} // namespace CatalogPaint
