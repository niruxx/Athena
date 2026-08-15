#pragma once

#include <QString>
#include <QStringList>

// A group is either a regular package group (contains packages) or a
// meta-group (contains other groups, e.g. dnf "environments" or Debian
// tasksel "tasks"). subGroups is only populated for meta-groups, packages
// is only populated for regular groups.
struct PackageGroupInfo {
    QString id;
    QString name;
    QString description;
    bool isMeta = false;
    bool installed = false;
    QStringList packages;
    QStringList subGroups;
};
