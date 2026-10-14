#include "Catalog.h"

#include <algorithm>

#include <QFontDatabase>
#include <QHash>
#include <QImageReader>
#include <QSet>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include "../core/AppSettings.h"
#include "AppCatalog.h"
#include "MaterialTheme.h"
#include "Motion.h"

namespace {

// Flatpak runtimes and extensions show up in `flatpak list` alongside apps
// but aren't something anyone launches.
bool looksLikeFlatpakRuntime(const QString &appId)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\.(Platform|Sdk|BaseApp|Extension|Locale|Debug|Sources)\b|\.GL\.|\.GL32\.|\.Codecs|\.openh264|\.ffmpeg)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(appId).hasMatch();
}

QPainterPath roundedPath(const QRectF &rect, qreal topRadius, qreal bottomRadius)
{
    QPainterPath path;
    path.moveTo(rect.left() + topRadius, rect.top());
    path.lineTo(rect.right() - topRadius, rect.top());
    path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + topRadius);
    path.lineTo(rect.right(), rect.bottom() - bottomRadius);
    path.quadTo(rect.right(), rect.bottom(), rect.right() - bottomRadius, rect.bottom());
    path.lineTo(rect.left() + bottomRadius, rect.bottom());
    path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - bottomRadius);
    path.lineTo(rect.left(), rect.top() + topRadius);
    path.quadTo(rect.left(), rect.top(), rect.left() + topRadius, rect.top());
    return path;
}

QFont scaledFont(const QFont &base, int pixelSize, int weight)
{
    QFont font(base);
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Weight(weight));
    return font;
}

// The surface with Material's hover state layer at `amount` (0-1) strength.
QColor hoverOver(const QColor &base, qreal amount)
{
    const QColor &on = MaterialTheme::current().onSurface;
    const float t = float(0.06 * amount);
    return QColor::fromRgbF(base.redF() * (1 - t) + on.redF() * t, base.greenF() * (1 - t) + on.greenF() * t,
                            base.blueF() * (1 - t) + on.blueF() * t);
}

} // namespace

bool CatalogItem::installed() const
{
    for (const CatalogVariant &variant : variants) {
        if (variant.info.installed)
            return true;
    }
    return false;
}

int CatalogItem::preferredVariant() const
{
    for (int i = 0; i < variants.size(); ++i) {
        if (variants[i].info.installed)
            return i;
    }
    return 0;
}

namespace Catalog {

CatalogItem fromPackage(const QVector<CatalogSource> &sources, int source, const PackageInfo &package)
{
    CatalogItem item;
    item.variants.append({source, package});
    const QString &key = sources[source].key;
    const AppCatalog::Entry *entry = AppCatalog::find(package.name);

    if (key == QLatin1String("flatpak")) {
        // FlatpakBackend reports "<Name> — <summary>" as the description.
        const QString pretty = package.description.section(QStringLiteral(" — "), 0, 0).trimmed();
        item.title = !pretty.isEmpty() ? pretty : package.name;
        item.subtitle = package.description.section(QStringLiteral(" — "), 1).trimmed();
        item.isApp = entry || !looksLikeFlatpakRuntime(package.name);
        if (!entry)
            item.icon = QIcon::fromTheme(package.name);
    } else {
        item.title = entry ? entry->name : package.name;
        item.subtitle = package.description;
        item.isApp = entry != nullptr || key == QLatin1String("snap");
    }
    if (entry) {
        item.icon = AppCatalog::iconFor(*entry);
        if (item.subtitle.isEmpty())
            item.subtitle = entry->comment;
    }
    return item;
}

namespace {
// Every name an item can be matched by: its display title, and its package
// identity — a native package's name, or a Flatpak ID's last component
// (org.gimp.GIMP -> "gimp"), since Flathub's title ("GNU Image Manipulation
// Program") often isn't the package's.
// Names compared loosely: "visual-studio-code-bin" and "Visual Studio
// Code" are the same app, as are "brave-bin" and Flathub's "Brave" — AUR
// packaging suffixes and separators don't count.
QString normalizedKey(QString name)
{
    name = name.toLower();
    name.replace('-', ' ').replace('_', ' ');
    name = name.simplified();
    static const QStringList suffixes = {QStringLiteral(" bin"), QStringLiteral(" git"), QStringLiteral(" appimage"),
                                         QStringLiteral(" nightly"), QStringLiteral(" stable")};
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix))
            name.chop(suffix.size());
    }
    return name;
}

QStringList matchKeys(const CatalogItem &item)
{
    QStringList keys = {normalizedKey(item.title)};
    for (const CatalogVariant &variant : item.variants) {
        const QString name = variant.info.name;
        const QString key = normalizedKey(name.contains('.') ? name.section('.', -1) : name);
        if (!keys.contains(key))
            keys << key;
    }
    return keys;
}
} // namespace

CatalogItem fromAppStream(int source, const AppStreamApp &app)
{
    // Catalog icons are files (Arch ships JPEG XL, which needs the
    // kimageformats plugin); fall back to the theme icon the catalog names.
    static QHash<QString, QIcon> iconCache;
    const QString cacheKey = app.iconPath + '|' + app.stockIcon;
    auto cached = iconCache.constFind(cacheKey);
    if (cached == iconCache.constEnd()) {
        QIcon icon;
        if (!app.iconPath.isEmpty() && QImageReader(app.iconPath).canRead())
            icon = QIcon(app.iconPath);
        else if (!app.stockIcon.isEmpty())
            icon = QIcon::fromTheme(app.stockIcon);
        cached = iconCache.insert(cacheKey, icon);
    }

    CatalogItem item;
    item.title = app.name;
    item.subtitle = app.summary;
    item.icon = cached.value();
    item.isApp = true;
    item.screenshots = app.screenshots;
    PackageInfo info;
    info.name = app.package;
    info.availableVersion = app.version;
    // Same "<Name> — <summary>" shape FlatpakBackend reports.
    info.description = app.kind == AppStreamApp::Kind::Flatpak ? app.name + QStringLiteral(" — ") + app.summary : app.summary;
    info.longDescription = app.description;
    info.repository = app.origin;
    info.homepageUrl = app.homepage;
    item.variants.append({source, info});
    return item;
}

QVector<CatalogItem> mergeApps(const QVector<CatalogItem> &items)
{
    // Apps first, so a plain package can join an app of the same name
    // below: a not-yet-installed pacman "gimp" has no .desktop file to mark
    // it as an app, but it's the same program as Flathub's "GIMP".
    QSet<QString> appKeys;
    for (const CatalogItem &item : items) {
        if (item.isApp) {
            for (const QString &key : matchKeys(item))
                appKeys.insert(key);
        }
    }

    QVector<CatalogItem> merged;
    QHash<QString, int> appIndex;
    QSet<int> fromPackage; // merged entries that started from a plain package
    for (const CatalogItem &item : items) {
        const QStringList keys = matchKeys(item);
        const bool matchesApp = std::any_of(keys.begin(), keys.end(), [&appKeys](const QString &k) { return appKeys.contains(k); });
        if (!item.isApp && !matchesApp) {
            merged.append(item);
            continue;
        }
        auto it = appIndex.constEnd();
        for (const QString &key : keys) {
            it = appIndex.constFind(key);
            if (it != appIndex.constEnd())
                break;
        }
        if (it == appIndex.constEnd()) {
            for (const QString &key : keys)
                appIndex.insert(key, merged.size());
            if (!item.isApp)
                fromPackage.insert(merged.size());
            merged.append(item);
            merged.last().isApp = true;
            continue;
        }
        const int targetIndex = it.value();
        CatalogItem &target = merged[targetIndex];
        // The index and a live search can both find the same package.
        for (const CatalogVariant &variant : item.variants) {
            const bool duplicate = std::any_of(target.variants.begin(), target.variants.end(), [&variant](const CatalogVariant &v) {
                return v.source == variant.source && v.info.name == variant.info.name;
            });
            if (!duplicate)
                target.variants.append(variant);
        }
        for (const QString &key : keys)
            appIndex.insert(key, targetIndex);
        // The app's own name and summary beat a bare package's.
        if (item.isApp && fromPackage.remove(targetIndex)) {
            target.title = item.title;
            target.subtitle = item.subtitle;
        }
        if (target.icon.isNull())
            target.icon = item.icon;
        if (target.subtitle.isEmpty())
            target.subtitle = item.subtitle;
        if (target.screenshots.isEmpty())
            target.screenshots = item.screenshots;
    }
    return merged;
}

} // namespace Catalog

CatalogModel::CatalogModel(QObject *parent) : QAbstractListModel(parent) { }

void CatalogModel::setItems(QVector<CatalogItem> items)
{
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
}

QString CatalogModel::keyOf(const CatalogItem &item)
{
    const CatalogVariant &v = item.variants[item.preferredVariant()];
    return QString::number(v.source) + '/' + v.info.name;
}

void CatalogModel::setSelectionMode(bool on)
{
    if (on == m_selectionMode)
        return;
    m_selectionMode = on;
    m_selected.clear();
    emit dataChanged(index(0), index(std::max(0, int(m_items.size()) - 1)));
    emit selectionChanged();
}

void CatalogModel::toggleSelected(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    const QString key = keyOf(m_items[row]);
    if (!m_selected.remove(key))
        m_selected.insert(key);
    emit dataChanged(index(row), index(row));
    emit selectionChanged();
}

void CatalogModel::selectAll()
{
    for (const CatalogItem &item : m_items)
        m_selected.insert(keyOf(item));
    emit dataChanged(index(0), index(std::max(0, int(m_items.size()) - 1)));
    emit selectionChanged();
}

bool CatalogModel::isSelected(int row) const
{
    return row >= 0 && row < m_items.size() && m_selected.contains(keyOf(m_items[row]));
}

QVector<CatalogItem> CatalogModel::selectedItems() const
{
    QVector<CatalogItem> result;
    for (const CatalogItem &item : m_items) {
        if (m_selected.contains(keyOf(item)))
            result.append(item);
    }
    return result;
}

int CatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const CatalogItem &item = m_items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::AccessibleTextRole:
        return item.title;
    case Qt::ToolTipRole:
        return item.subtitle;
    case ItemRole:
        return index.row();
    default:
        return {};
    }
}

namespace CatalogPaint {

void appIcon(QPainter *painter, const QRect &rect, const CatalogItem &item, const QString &fallbackGlyph)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (!item.icon.isNull()) {
        item.icon.paint(painter, rect);
        painter->restore();
        return;
    }
    const bool dark = MaterialTheme::current().dark;
    const int hue = MaterialTheme::hueForName(item.title);
    painter->setPen(Qt::NoPen);
    painter->setBrush(dark ? MaterialTheme::oklch(0.40, 0.09, hue) : MaterialTheme::oklch(0.88, 0.08, hue));
    painter->drawRoundedRect(rect, rect.width() * 0.28, rect.height() * 0.28);
    painter->setPen(dark ? MaterialTheme::oklch(0.93, 0.05, hue) : MaterialTheme::oklch(0.32, 0.10, hue));
    painter->setFont(MaterialTheme::iconFont(int(rect.width() * 0.55), true));
    painter->drawText(rect, Qt::AlignCenter, fallbackGlyph);
    painter->restore();
}

int sourceChip(QPainter *painter, const QPoint &topLeft, const CatalogSource &source)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const bool aur = source.key == QLatin1String("aur");
    QFont font = painter->font();
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    const int width = 8 + 15 + 4 + metrics.horizontalAdvance(source.label) + 8;
    const QRect chip(topLeft, QSize(width, 24));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(aur ? s.tertiaryContainer : s.surfaceContainerHighest);
    painter->drawRoundedRect(chip, 8, 8);
    const QColor fg = aur ? s.onTertiaryContainer : s.onSurfaceVariant;
    painter->setPen(fg);
    painter->setFont(MaterialTheme::iconFont(15));
    painter->drawText(QRect(chip.left() + 8, chip.top(), 15, chip.height()), Qt::AlignCenter, source.icon);
    painter->setFont(font);
    painter->drawText(chip.adjusted(8 + 15 + 4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, source.label);
    painter->restore();
    return width;
}

} // namespace CatalogPaint

CatalogRowDelegate::CatalogRowDelegate(const QVector<CatalogSource> *sources, Kind kind, QObject *parent)
    : QStyledItemDelegate(parent), m_sources(sources), m_kind(kind)
{
}

int CatalogRowDelegate::rowHeight()
{
    return AppSettings::instance().compactPackageLists() ? 56 : 68;
}

QSize CatalogRowDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    // The width is a minimum; list views stretch rows to their viewport.
    return QSize(320, rowHeight());
}

void CatalogRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const auto *model = qobject_cast<const CatalogModel *>(index.model());
    if (!model)
        return;
    const CatalogItem &item = model->item(index.row());
    const CatalogVariant &variant = item.variants[item.preferredVariant()];
    const CatalogSource &source = (*m_sources)[variant.source];
    const MaterialTheme::Scheme &s = MaterialTheme::current();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect rect = option.rect;
    const bool first = index.row() == 0;
    const bool last = index.row() == model->rowCount() - 1;
    QColor bg = s.surface;
    if (option.state & QStyle::State_Selected)
        bg = s.secondaryContainer;
    else
        bg = hoverOver(s.surface, Motion::hoverProgress(option.widget, index.row(), option.state & QStyle::State_MouseOver));
    painter->fillPath(roundedPath(rect, first ? 16 : 0, last ? 16 : 0), bg);
    if (!last) {
        painter->setPen(s.outlineVariant);
        painter->drawLine(rect.left() + 72, rect.bottom(), rect.right() - 16, rect.bottom());
    }

    // Selection mode puts a check circle in front of the icon.
    int left = rect.left() + 16;
    if (model->selectionMode()) {
        const bool selected = model->isSelected(index.row());
        painter->setPen(selected ? s.primary : s.onSurfaceVariant);
        painter->setFont(MaterialTheme::iconFont(24, selected));
        painter->drawText(QRect(left, rect.top(), 24, rect.height()), Qt::AlignCenter,
                          selected ? QStringLiteral("check_circle") : QStringLiteral("radio_button_unchecked"));
        left += 24 + 14;
    }
    const QRect iconRect(left, rect.top() + (rect.height() - 40) / 2, 40, 40);
    CatalogPaint::appIcon(painter, iconRect, item, source.icon);

    // Right side: source chips, then the version.
    int right = rect.right() - 16;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(12);
    QString version;
    if (m_kind == Kind::Installed || m_kind == Kind::Package)
        version = variant.info.installed ? variant.info.installedVersion : variant.info.availableVersion;
    // The old table's optional Architecture/Size columns, where reported.
    QStringList extras;
    if (AppSettings::instance().showArchitectureColumn() && !variant.info.architecture.isEmpty())
        extras << variant.info.architecture;
    if (AppSettings::instance().showSizeColumn() && !variant.info.size.isEmpty())
        extras << variant.info.size;
    if (!extras.isEmpty()) {
        QStringList parts;
        if (!version.isEmpty())
            parts << version;
        version = (parts + extras).join(QStringLiteral("  ·  "));
    }
    if (!version.isEmpty()) {
        const QFontMetrics metrics(mono);
        const QString shown = metrics.elidedText(version, Qt::ElideMiddle, extras.isEmpty() ? 140 : 240);
        const int width = metrics.horizontalAdvance(shown);
        painter->setFont(mono);
        painter->setPen(s.onSurfaceVariant);
        painter->drawText(QRect(right - width, rect.top(), width, rect.height()), Qt::AlignVCenter | Qt::AlignRight, shown);
        right -= width + 12;
    }
    QVector<int> shownSources;
    for (const CatalogVariant &v : item.variants) {
        if (!shownSources.contains(v.source))
            shownSources.append(v.source);
    }
    for (int i = shownSources.size() - 1; i >= 0; --i) {
        QFont chipFont = painter->font();
        chipFont.setPixelSize(12);
        const int width = 8 + 15 + 4 + QFontMetrics(chipFont).horizontalAdvance((*m_sources)[shownSources[i]].label) + 8;
        CatalogPaint::sourceChip(painter, QPoint(right - width, rect.top() + (rect.height() - 24) / 2), (*m_sources)[shownSources[i]]);
        right -= width + 6;
    }

    // Title and subtitle.
    const QRect textRect(iconRect.right() + 16, rect.top() + (rect.height() - 42) / 2, right - iconRect.right() - 28, 42);
    const QFont titleFont = scaledFont(option.font, 15, QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(s.onSurface);
    painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), 22), Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(titleFont).elidedText(item.title, Qt::ElideRight, textRect.width()));
    QString subtitle = item.subtitle;
    QFont subFont = scaledFont(option.font, 13, QFont::Normal);
    if (m_kind == Kind::Update) {
        const QString &from = variant.info.installedVersion;
        const QString &to = variant.info.availableVersion;
        // Flatpak doesn't report the installed version of runtimes, and a
        // same-version update is a rebuild (new commit) of that version.
        subtitle = from.isEmpty() ? QObject::tr("Update to %1").arg(to)
            : from == to          ? QObject::tr("New build of %1").arg(to)
                                  : QStringLiteral("%1  →  %2").arg(from, to);
        subFont = mono;
    } else if (item.isApp && item.title != variant.info.name) {
        subtitle = item.subtitle.isEmpty() ? variant.info.name : item.subtitle;
    }
    painter->setFont(subFont);
    painter->setPen(s.onSurfaceVariant);
    painter->drawText(QRect(textRect.left(), textRect.top() + 22, textRect.width(), 20), Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(subFont).elidedText(subtitle, Qt::ElideRight, textRect.width()));
    painter->restore();
}

CatalogCardDelegate::CatalogCardDelegate(const QVector<CatalogSource> *sources, QObject *parent)
    : QStyledItemDelegate(parent), m_sources(sources)
{
}

QSize CatalogCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return QSize(m_cardWidth, kCardHeight);
}

void CatalogCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const auto *model = qobject_cast<const CatalogModel *>(index.model());
    if (!model)
        return;
    const CatalogItem &item = model->item(index.row());
    const CatalogVariant &variant = item.variants[item.preferredVariant()];
    const MaterialTheme::Scheme &s = MaterialTheme::current();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect card = option.rect.adjusted(6, 6, -6, -6);
    const bool picked = model->selectionMode() && model->isSelected(index.row());
    QColor bg = s.surface;
    if (picked || (option.state & QStyle::State_Selected))
        bg = s.secondaryContainer;
    else
        bg = hoverOver(s.surface, Motion::hoverProgress(option.widget, index.row(), option.state & QStyle::State_MouseOver));
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(card, 16, 16);

    const QRect iconRect(card.left() + 16, card.top() + 16, 56, 56);
    CatalogPaint::appIcon(painter, iconRect, item, (*m_sources)[variant.source].icon);

    const int textLeft = iconRect.right() + 14;
    int textWidth = card.right() - 16 - textLeft;
    if (model->selectionMode()) {
        // Selection mode: a check circle in the card's top-right corner.
        painter->setPen(picked ? s.primary : s.onSurfaceVariant);
        painter->setFont(MaterialTheme::iconFont(24, picked));
        painter->drawText(QRect(card.right() - 16 - 24, card.top() + 14, 24, 24), Qt::AlignCenter,
                          picked ? QStringLiteral("check_circle") : QStringLiteral("radio_button_unchecked"));
        textWidth -= 30;
    }
    const QFont nameFont = scaledFont(option.font, 16, QFont::DemiBold);
    painter->setFont(nameFont);
    painter->setPen(s.onSurface);
    painter->drawText(QRect(textLeft, iconRect.top() + 6, textWidth, 24), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nameFont).elidedText(item.title, Qt::ElideRight, textWidth));
    const QFont metaFont = scaledFont(option.font, 12, QFont::Medium);
    painter->setFont(metaFont);
    painter->setPen(s.onSurfaceVariant);
    painter->drawText(QRect(textLeft, iconRect.top() + 30, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(metaFont).elidedText(variant.info.name, Qt::ElideMiddle, textWidth));

    const QFont bodyFont = scaledFont(option.font, 13, QFont::Normal);
    painter->setFont(bodyFont);
    const QRect summaryRect(card.left() + 16, iconRect.bottom() + 10, card.width() - 32, 40);
    painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                      QFontMetrics(bodyFont).elidedText(item.subtitle, Qt::ElideRight, summaryRect.width() * 2 - 20));

    int x = card.left() + 16;
    const int chipTop = card.bottom() - 16 - 24;
    QVector<int> shownSources;
    for (const CatalogVariant &v : item.variants) {
        if (shownSources.contains(v.source))
            continue;
        shownSources.append(v.source);
        x += CatalogPaint::sourceChip(painter, QPoint(x, chipTop), (*m_sources)[v.source]) + 6;
    }
    if (item.installed()) {
        const QFont installedFont = scaledFont(option.font, 12, QFont::DemiBold);
        const QString text = QObject::tr("Installed");
        const int width = QFontMetrics(installedFont).horizontalAdvance(text);
        painter->setPen(s.primary);
        painter->setFont(installedFont);
        painter->drawText(QRect(card.right() - 16 - width, chipTop, width, 24), Qt::AlignVCenter | Qt::AlignRight, text);
        painter->setFont(MaterialTheme::iconFont(16, true));
        painter->drawText(QRect(card.right() - 16 - width - 20, chipTop, 18, 24), Qt::AlignCenter, QStringLiteral("check_circle"));
    }
    painter->restore();
}
