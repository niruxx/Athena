#include "PackageTableModel.h"

PackageTableModel::PackageTableModel(QObject *parent) : QAbstractTableModel(parent) { }

void PackageTableModel::setPackages(QVector<PackageInfo> packages)
{
    beginResetModel();
    m_packages = std::move(packages);
    m_checkedNames.clear();
    for (const PackageInfo &pkg : m_packages) {
        if (pkg.installed)
            m_checkedNames.insert(pkg.name);
    }
    endResetModel();
}

const PackageInfo &PackageTableModel::packageAt(int row) const
{
    return m_packages.at(row);
}

QStringList PackageTableModel::checkedNames() const
{
    return m_checkedNames.values();
}

QVector<PackageInfo> PackageTableModel::checkedPackages() const
{
    QVector<PackageInfo> result;
    for (const PackageInfo &pkg : m_packages) {
        if (m_checkedNames.contains(pkg.name))
            result.append(pkg);
    }
    return result;
}

QVector<PackageInfo> PackageTableModel::uncheckedInstalledPackages() const
{
    QVector<PackageInfo> result;
    for (const PackageInfo &pkg : m_packages) {
        if (pkg.installed && !m_checkedNames.contains(pkg.name))
            result.append(pkg);
    }
    return result;
}

void PackageTableModel::setChecked(const QString &name, bool checked)
{
    if (checked)
        m_checkedNames.insert(name);
    else
        m_checkedNames.remove(name);

    for (int row = 0; row < m_packages.size(); ++row) {
        if (m_packages.at(row).name == name) {
            const QModelIndex idx = index(row, CheckColumn);
            emit dataChanged(idx, idx, {Qt::CheckStateRole});
            break;
        }
    }
    emit checkedChanged();
}

void PackageTableModel::clearChecked()
{
    if (m_checkedNames.isEmpty())
        return;
    m_checkedNames.clear();
    if (!m_packages.isEmpty())
        emit dataChanged(index(0, CheckColumn), index(m_packages.size() - 1, CheckColumn), {Qt::CheckStateRole});
    emit checkedChanged();
}

void PackageTableModel::checkAll(bool checked)
{
    if (m_packages.isEmpty())
        return;

    if (checked) {
        m_checkedNames.clear();
        for (const PackageInfo &pkg : m_packages)
            m_checkedNames.insert(pkg.name);
    } else {
        m_checkedNames.clear();
    }

    emit dataChanged(index(0, CheckColumn), index(m_packages.size() - 1, CheckColumn), {Qt::CheckStateRole});
    emit checkedChanged();
}

int PackageTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_packages.size();
}

int PackageTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant PackageTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_packages.size())
        return {};

    const PackageInfo &pkg = m_packages.at(index.row());

    if (role == Qt::CheckStateRole && index.column() == CheckColumn)
        return m_checkedNames.contains(pkg.name) ? Qt::Checked : Qt::Unchecked;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case CheckColumn: return {};
        case NameColumn: return pkg.name;
        case InstalledVersionColumn: return pkg.installed ? pkg.installedVersion : QStringLiteral("-");
        case AvailableVersionColumn:
            if (!pkg.availableVersion.isEmpty())
                return pkg.availableVersion;
            return pkg.installed ? pkg.installedVersion : QStringLiteral("-");
        case DescriptionColumn: return pkg.description;
        default: return {};
        }
    }

    return {};
}

bool PackageTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != CheckColumn || role != Qt::CheckStateRole)
        return false;
    if (index.row() >= m_packages.size())
        return false;

    const QString &name = m_packages.at(index.row()).name;
    const bool checked = value.toInt() == Qt::Checked;
    if (checked)
        m_checkedNames.insert(name);
    else
        m_checkedNames.remove(name);

    emit dataChanged(index, index, {Qt::CheckStateRole});
    emit checkedChanged();
    return true;
}

QVariant PackageTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    if (role == Qt::DisplayRole) {
        switch (section) {
        case CheckColumn: return QString();
        case NameColumn: return tr("Name");
        case InstalledVersionColumn: return tr("Installed Version");
        case AvailableVersionColumn: return tr("Latest Version");
        case DescriptionColumn: return tr("Description");
        default: return {};
        }
    }

    return {};
}

Qt::ItemFlags PackageTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == CheckColumn)
        itemFlags |= Qt::ItemIsUserCheckable;
    return itemFlags;
}
