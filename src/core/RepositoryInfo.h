#pragma once

#include <QMap>
#include <QString>

struct RepositoryInfo {
    QString id; // opaque identifier used to route a toggle back to this repo
    QString name;
    QString url;
    bool enabled = false;
};

// Describes one input field a backend needs to manually add a repository,
// so a single generic dialog can build itself from whichever backend it's
// pointed at rather than hardcoding fields per package manager.
struct RepositoryAddField {
    QString key;         // stable key the value comes back under in addRepository()'s map
    QString label;
    QString placeholder;
    bool required = true;
};

using RepositoryAddValues = QMap<QString, QString>;
