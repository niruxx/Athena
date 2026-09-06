#include "DirSizeScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

DirSizeScanner::DirSizeScanner(const QStringList &paths, QObject *parent) : QThread(parent), m_paths(paths)
{
}

void DirSizeScanner::run()
{
    for (const QString &path : m_paths) {
        qint64 total = 0;
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
        }
        emit sizeComputed(path, total);
    }
}
