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
        // A plain file (a dotfile like .bashrc) is its own size; iterating
        // it as a directory finds nothing.
        const QFileInfo info(path);
        if (info.isFile()) {
            emit sizeComputed(path, info.size());
            continue;
        }
        qint64 total = 0;
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
        }
        emit sizeComputed(path, total);
    }
}
