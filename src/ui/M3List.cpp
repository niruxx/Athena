#include "M3List.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QTableView>
#include <QTreeWidget>

#include "MaterialTheme.h"

namespace M3List {

namespace {

constexpr int kPadding = 16;   // row content inset from the list's edges
constexpr int kLeading = 40;   // icon / symbol tile slot
constexpr int kCheckSlot = 34; // checkbox plus the gap after it

QColor withAlpha(QColor color, double alpha)
{
    color.setAlphaF(alpha);
    return color;
}

} // namespace

Delegate::Delegate(Columns columns, QObject *parent) : QStyledItemDelegate(parent), m_columns(columns) { }

QString Delegate::subtitle(const QModelIndex &index) const
{
    const QVariant own = index.data(SubtitleRole);
    if (own.isValid())
        return own.toString();
    return m_columns.subtitle >= 0 ? index.siblingAtColumn(m_columns.subtitle).data().toString() : QString();
}

QString Delegate::trailing(const QModelIndex &index) const
{
    const QVariant own = index.data(TrailingRole);
    if (own.isValid())
        return own.toString();
    return m_columns.trailing >= 0 ? index.siblingAtColumn(m_columns.trailing).data().toString() : QString();
}

QRect Delegate::checkRect(const QRect &row) const
{
    return QRect(row.left() + kPadding, row.center().y() - 9, 18, 18);
}

QSize Delegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.data(HeaderRole).toBool())
        return QSize(option.rect.width(), 40);
    const bool leading = !index.data(Qt::DecorationRole).isNull() || !index.data(SymbolRole).toString().isEmpty();
    const bool checkable = index.flags() & Qt::ItemIsUserCheckable;
    const int height = !subtitle(index).isEmpty() ? 64 : leading ? 56 : checkable ? 48 : 40;
    return QSize(200, height);
}

void Delegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const bool enabled = option.state & QStyle::State_Enabled;
    const double textAlpha = enabled ? 1.0 : 0.38;
    QRect row = option.rect;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    if (index.data(HeaderRole).toBool()) {
        QFont font = option.font;
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        painter->setPen(withAlpha(s.primary, textAlpha));
        painter->drawText(row.adjusted(kPadding, 0, -kPadding, -6), Qt::AlignLeft | Qt::AlignBottom,
                          index.data().toString());
        painter->restore();
        return;
    }

    // Selection and hover state layers, as rounded shapes inset from the
    // list's edges.
    const QRectF shape = QRectF(row).adjusted(4, 2, -4, -2);
    if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(s.secondaryContainer);
        painter->drawRoundedRect(shape, 12, 12);
    }
    if ((option.state & QStyle::State_MouseOver) && enabled) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(withAlpha(s.onSurface, 0.08));
        painter->drawRoundedRect(shape, 12, 12);
    }
    const bool selected = option.state & QStyle::State_Selected;
    const QColor titleColor = withAlpha(selected ? s.onSecondaryContainer : s.onSurface, textAlpha);
    const QColor supportColor = withAlpha(selected ? s.onSecondaryContainer : s.onSurfaceVariant, textAlpha * (selected ? 0.8 : 1));

    int x = row.left() + kPadding;
    // Checkbox, drawn by the style (AthenaStyle draws Material's).
    const QVariant checkState = index.data(Qt::CheckStateRole);
    if ((index.flags() & Qt::ItemIsUserCheckable) && checkState.isValid()) {
        QStyleOptionViewItem check(option);
        check.rect = checkRect(row);
        check.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
        const auto state = Qt::CheckState(checkState.toInt());
        check.state |= state == Qt::Checked ? QStyle::State_On : state == Qt::PartiallyChecked ? QStyle::State_NoChange : QStyle::State_Off;
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &check, painter, widget);
        x += kCheckSlot;
    }

    // Leading icon, or a tonal tile with a symbol.
    const QVariant decoration = index.data(Qt::DecorationRole);
    const QString symbol = index.data(SymbolRole).toString();
    const QRect slot(x, row.center().y() - kLeading / 2, kLeading, kLeading);
    QIcon icon;
    if (decoration.canConvert<QIcon>())
        icon = decoration.value<QIcon>();
    else if (decoration.canConvert<QPixmap>())
        icon = QIcon(decoration.value<QPixmap>());
    if (!icon.isNull()) {
        icon.paint(painter, slot.adjusted(4, 4, -4, -4), Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        x += kLeading + 16;
    } else if (!symbol.isEmpty()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(withAlpha(s.secondaryContainer, enabled ? 1.0 : 0.5));
        painter->drawRoundedRect(QRectF(slot), 12, 12);
        painter->setFont(MaterialTheme::iconFont(22));
        painter->setPen(withAlpha(s.onSecondaryContainer, textAlpha));
        painter->drawText(slot, Qt::AlignCenter, symbol);
        x += kLeading + 16;
    }

    // Trailing text.
    int right = row.right() - kPadding;
    const QString trail = trailing(index);
    QFont font = option.font;
    if (!trail.isEmpty()) {
        QFont trailFont = font;
        trailFont.setWeight(QFont::Medium);
        painter->setFont(trailFont);
        const int width = QFontMetrics(trailFont).horizontalAdvance(trail);
        painter->setPen(supportColor);
        painter->drawText(QRect(right - width, row.top(), width, row.height()), Qt::AlignRight | Qt::AlignVCenter, trail);
        right -= width + 16;
    }

    // Title and supporting line.
    const QString title = index.data().toString();
    const QString support = subtitle(index);
    const QRect text(x, row.top(), qMax(0, right - x), row.height());
    QFont titleFont = font;
    titleFont.setPixelSize(qMax(13, QFontInfo(font).pixelSize() + 1));
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics supportMetrics(font);
    painter->setFont(titleFont);
    painter->setPen(titleColor);
    if (support.isEmpty()) {
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, titleMetrics.elidedText(title, Qt::ElideRight, text.width()));
    } else {
        const int block = titleMetrics.height() + 2 + supportMetrics.height();
        const int top = row.top() + (row.height() - block) / 2;
        painter->drawText(QRect(x, top, text.width(), titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          titleMetrics.elidedText(title, Qt::ElideRight, text.width()));
        painter->setFont(font);
        painter->setPen(supportColor);
        painter->drawText(QRect(x, top + titleMetrics.height() + 2, text.width(), supportMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, supportMetrics.elidedText(support, Qt::ElideMiddle, text.width()));
    }
    painter->restore();
}

bool Delegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                           const QModelIndex &index)
{
    // The checkbox sits where Material puts it, not where the base
    // delegate looks for it, so clicks on it are handled here.
    if ((event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick)
        && (index.flags() & Qt::ItemIsUserCheckable) && (index.flags() & Qt::ItemIsEnabled)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton
            && checkRect(option.rect).adjusted(-8, -12, 8, 12).contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonRelease) {
                const auto state = Qt::CheckState(index.data(Qt::CheckStateRole).toInt());
                model->setData(index, state == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease)
            return false;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void apply(QAbstractItemView *view, Columns columns)
{
    view->setItemDelegateForColumn(0, new Delegate(columns, view));
    view->setProperty("m3", columns.plain ? QStringLiteral("listPlain") : QStringLiteral("list"));
    if (auto *frame = qobject_cast<QFrame *>(view))
        frame->setFrameShape(QFrame::NoFrame);
    view->style()->unpolish(view);
    view->style()->polish(view);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setIconSize(QSize(32, 32));
    view->setAlternatingRowColors(false);
    if (auto *table = qobject_cast<QTableView *>(view)) {
        table->horizontalHeader()->hide();
        table->verticalHeader()->hide();
        table->setShowGrid(false);
        table->horizontalHeader()->setStretchLastSection(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        // Rows take the delegate's height (with or without a supporting line).
        table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        for (int column = 1; column < table->model()->columnCount(); ++column)
            table->setColumnHidden(column, true);
    }
    if (auto *tree = qobject_cast<QTreeView *>(view)) {
        tree->header()->hide();
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column < tree->model()->columnCount(); ++column)
            tree->setColumnHidden(column, true);
        tree->setIndentation(0);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(false);
        tree->setItemsExpandable(false);
        tree->setExpandsOnDoubleClick(false);
        // Section headers' children are always shown.
        QObject::connect(tree->model(), &QAbstractItemModel::rowsInserted, tree, [tree]() { tree->expandAll(); });
        QObject::connect(tree->model(), &QAbstractItemModel::modelReset, tree, [tree]() { tree->expandAll(); });
        tree->expandAll();
    }
}

void makeHeader(QTreeWidgetItem *item)
{
    item->setData(0, HeaderRole, true);
    item->setFlags(Qt::ItemIsEnabled);
}

} // namespace M3List
