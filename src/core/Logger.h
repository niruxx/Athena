#pragma once

// Installs a Qt message handler that appends qDebug/qInfo/qWarning/
// qCritical/qFatal output to a log file, gated by AppSettings' logging
// preferences (enabled, directory, minimum level). Call once at startup,
// before constructing QApplication.
namespace Logger {
void install();
}
