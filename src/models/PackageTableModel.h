#pragma once

#include <QAbstractTableModel>
#include <QSet>
#include <QVector>

#include "../core/PackageInfo.h"

// Package list model with a checkbox column (column 0) for marking
// packages for a bulk install/uninstall action, independent of row
// selection (which the view uses to drive a details panel instead).
// Checked state is tracked by name so it survives sorting.
class PackageTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { CheckColumn = 0, NameColumn, InstalledVersionColumn, AvailableVersionColumn, DescriptionColumn, ColumnCount };

    explicit PackageTableModel(QObject *parent = nullptr);

    void setPackages(QVector<PackageInfo> packages);
    const PackageInfo &packageAt(int row) const;
    int packageCount() const { return m_packages.size(); }
    bool isEmpty() const { return m_packages.isEmpty(); }

    QStringList checkedNames() const;
    QVector<PackageInfo> checkedPackages() const;
    void setChecked(const QString &name, bool checked);
    void clearChecked();
    // Checks or unchecks every currently-loaded row in one shot (e.g. for
    // a "Select All" / "Select None" action) rather than one setChecked()
    // call per row.
    void checkAll(bool checked);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void checkedChanged();

private:
    QVector<PackageInfo> m_packages;
    QSet<QString> m_checkedNames;
};
