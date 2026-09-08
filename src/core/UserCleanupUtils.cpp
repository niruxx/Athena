#include "UserCleanupUtils.h"

#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace UserCleanupUtils {

namespace {
bool ownedByCurrentUser(const QFileInfo &info)
{
    return info.ownerId() == ::geteuid();
}
} // namespace

qint64 ownedSize(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return 0;
    if (!ownedByCurrentUser(info))
        return 0;
    if (!info.isDir())
        return info.size();

    qint64 total = 0;
    const QStringList entries = QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries)
        total += ownedSize(QDir(path).filePath(entry));
    return total;
}

bool clearOwnedPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return true;

    if (!info.isDir())
        return ownedByCurrentUser(info) ? QFile::remove(path) : true;

    bool ok = true;
    const QDir dir(path);
    const QStringList entries = dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QString entryPath = dir.filePath(entry);
        const QFileInfo entryInfo(entryPath);
        if (!ownedByCurrentUser(entryInfo))
            continue;
        const bool removed = entryInfo.isDir() ? QDir(entryPath).removeRecursively() : QFile::remove(entryPath);
        ok = ok && removed;
    }
    return ok;
}

} // namespace UserCleanupUtils
