#pragma once

#include <QString>

struct PackageInfo {
    QString name;
    QString installedVersion; // empty if not installed
    QString availableVersion; // latest version known to be available; empty if unknown
    QString architecture;
    QString repository;
    QString description;     // short, one-line summary
    QString longDescription; // full description text, empty if not fetched
    bool installed = false;
};
