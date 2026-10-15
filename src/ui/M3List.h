#pragma once

#include <QStyledItemDelegate>

class QAbstractItemView;
class QTreeWidgetItem;

// Material 3 list rows for the classic pages' QListWidget/QTreeWidget
// views: an optional checkbox, a leading app icon or symbol tile, a title
// with a supporting line, and trailing text (a size, a count), with a
// hover state layer and a rounded selection. Multi-column trees keep
// their columns as data: the row reads its supporting and trailing text
// from sibling columns, which apply() hides along with the header.
namespace M3List {

enum Role {
    SubtitleRole = Qt::UserRole + 200, // supporting line (overrides the subtitle column)
    TrailingRole,                      // trailing text (overrides the trailing column)
    SymbolRole,                        // Material Symbols name for the tile when there's no icon
    HeaderRole,                        // bool: a section label, not a row
};

struct Columns {
    int subtitle = -1; // sibling column shown as the supporting line
    int trailing = -1; // sibling column shown as trailing text
    bool plain = false; // no container (a sidebar on the page background)
};

class Delegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    Delegate(Columns columns, QObject *parent);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

private:
    QString subtitle(const QModelIndex &index) const;
    QString trailing(const QModelIndex &index) const;
    QRect checkRect(const QRect &row) const;

    Columns m_columns;
};

// Turns `view` into an M3 list: the delegate on column 0, every other
// column and the header hidden, no tree indentation, hover tracking, and
// the "list" container style. Sorting still works through the model.
void apply(QAbstractItemView *view, Columns columns = {});

// Marks a tree/list item as a section header (unselectable, label style).
void makeHeader(QTreeWidgetItem *item);

} // namespace M3List
