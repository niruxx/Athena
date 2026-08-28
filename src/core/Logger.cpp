#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include "AppSettings.h"

namespace {

QtMessageHandler previousHandler = nullptr;

// Higher rank = more verbose. A message is written when its own rank is
// less than or equal to the configured level's rank (i.e. "Info" writes
// Info/Warning/Error but not Debug).
int levelRank(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return 0;
    case LogLevel::Warning: return 1;
    case LogLevel::Info: return 2;
    case LogLevel::Debug: return 3;
    }
    return 2;
}

int messageRank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 3;
    case QtInfoMsg: return 2;
    case QtWarningMsg: return 1;
    case QtCriticalMsg:
    case QtFatalMsg:
    default: return 0;
    }
}

const char *levelLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "INFO";
}

void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Always forward first so console behavior (including qFatal's abort)
    // is unaffected by whether file logging is enabled.
    if (previousHandler)
        previousHandler(type, context, message);

    if (!AppSettings::instance().loggingEnabled())
        return;
    if (messageRank(type) > levelRank(AppSettings::instance().logLevel()))
        return;

    const QString dir = AppSettings::instance().logDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return;

    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QFile file(QDir(dir).filePath(QStringLiteral("athena.log")));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate) << " [" << levelLabel(type) << "] " << message
           << '\n';
}

} // namespace

void Logger::install()
{
    previousHandler = qInstallMessageHandler(handleMessage);
}
