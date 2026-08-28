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
    QString size;            // human-readable (e.g. "12.3 MB"); empty if not reported by the backend
    QString homepageUrl;     // project/vendor homepage; empty if not reported by the backend
    bool installed = false;
};
