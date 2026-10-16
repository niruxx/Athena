#include "PolkitAgent.h"

#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QThread>

namespace PolkitAgent {

namespace {

// Programs that register a polkit agent: standalone agents, and desktop
// shells with one built in.
const QSet<QString> &agentNames()
{
    static const QSet<QString> names = {
        QStringLiteral("polkit-kde-authentication-agent-1"),
        QStringLiteral("polkit-gnome-authentication-agent-1"),
        QStringLiteral("polkit-mate-authentication-agent-1"),
        QStringLiteral("polkit-efl-authentication-agent-1"),
        QStringLiteral("lxqt-policykit-agent"),
        QStringLiteral("lxpolkit"),
        QStringLiteral("xfce-polkit"),
        QStringLiteral("hyprpolkitagent"),
        QStringLiteral("soteria"),
        QStringLiteral("mate-polkit"),
        QStringLiteral("ukui-polkit"),
        QStringLiteral("io.elementary.desktop.agent-polkit"),
        QStringLiteral("budgie-polkit-dialog"),
        QStringLiteral("gnome-shell"),
        QStringLiteral("cinnamon"),
        QStringLiteral("cosmic-osd"),
        QStringLiteral("phosh"),
    };
    return names;
}

// Where distributions install the agents Athena can start, in order of
// preference (Qt first, to match the app).
const QStringList &agentPaths()
{
    static const QStringList paths = {
        QStringLiteral("/usr/lib/polkit-kde-authentication-agent-1"),                        // Arch
        QStringLiteral("/usr/libexec/polkit-kde-authentication-agent-1"),                    // Fedora, openSUSE
        QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec/polkit-kde-authentication-agent-1"), // Debian, Ubuntu
        QStringLiteral("/usr/lib/aarch64-linux-gnu/libexec/polkit-kde-authentication-agent-1"),
        QStringLiteral("/usr/bin/lxqt-policykit-agent"),
        QStringLiteral("/usr/lib/hyprpolkitagent/hyprpolkitagent"),
        QStringLiteral("/usr/libexec/hyprpolkitagent"),
        QStringLiteral("/usr/bin/soteria"),
        QStringLiteral("/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1"),         // Arch, Debian
        QStringLiteral("/usr/libexec/polkit-gnome-authentication-agent-1"),                  // Fedora
        QStringLiteral("/usr/lib/mate-polkit/polkit-mate-authentication-agent-1"),
        QStringLiteral("/usr/libexec/polkit-mate-authentication-agent-1"),
        QStringLiteral("/usr/bin/lxpolkit"),
        QStringLiteral("/usr/lib/xfce-polkit/xfce-polkit"),
    };
    return paths;
}

QString programName(const QString &pid)
{
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray first = cmdline.read(4096).split('\0').value(0);
    return QFileInfo(QString::fromLocal8Bit(first)).fileName();
}

} // namespace

bool isRunning()
{
    const uint uid = ::getuid();
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);
    for (const QString &pid : pids) {
        if (QFileInfo(QStringLiteral("/proc/") + pid).ownerId() != uid)
            continue;
        if (agentNames().contains(programName(pid)))
            return true;
    }
    return false;
}

QString installedAgent()
{
    for (const QString &path : agentPaths()) {
        if (QFileInfo(path).isExecutable())
            return path;
    }
    return QString();
}

bool ensureRunning()
{
    if (isRunning())
        return true;
    const QString agent = installedAgent();
    if (agent.isEmpty())
        return false;
    if (!QProcess::startDetached(agent, {}))
        return false;
    // Give it a moment to register with polkit before pkexec asks for it.
    for (int i = 0; i < 10 && !isRunning(); ++i)
        QThread::msleep(100);
    QThread::msleep(300);
    return true;
}

} // namespace PolkitAgent
