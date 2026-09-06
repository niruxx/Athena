#pragma once

#include <QStringList>
#include <QThread>

// Computes the recursive size of each given directory on a background
// thread, so scanning potentially large ~/.var/app directories never
// blocks the UI. One sizeComputed() signal per input path, in order.
class DirSizeScanner : public QThread {
    Q_OBJECT

public:
    explicit DirSizeScanner(const QStringList &paths, QObject *parent = nullptr);

signals:
    void sizeComputed(const QString &path, qint64 bytes);

protected:
    void run() override;

private:
    QStringList m_paths;
};
