#include "TerminalOutputDialog.h"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextCursor>
#include <QVBoxLayout>

#include "../core/PolkitAgent.h"
#include "MaterialTheme.h"

TerminalOutputDialog::TerminalOutputDialog(const QString &title, QWidget *parent) : M3Dialog(parent)
{
    setHeadline(title, QStringLiteral("terminal"));
    setResizable(true);
    resize(720, 520);

    m_statusIcon = new QLabel(this);
    m_statusIcon->hide();
    m_statusLabel = new QLabel(tr("Running…"), this);
    QFont statusFont = m_statusLabel->font();
    statusFont.setWeight(QFont::DemiBold);
    m_statusLabel->setFont(statusFont);
    // Material's indeterminate linear progress indicator while running.
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    // What's happening right now, read from the output ("Upgrading
    // firefox — 3 of 10").
    m_stepLabel = new QLabel(this);
    m_stepLabel->setProperty("m3", QStringLiteral("muted"));
    m_stepLabel->setWordWrap(true);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    // A terminal-like scheme independent of the app's own theme, matching
    // how most terminal/IDE-console panels look regardless of app chrome.
    m_output->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: #1b1d1f; color: #d4d4d4; border: none; border-radius: 12px; padding: 10px; }"));
    m_output->setMinimumHeight(240);

    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusRow->addWidget(m_statusIcon);
    statusRow->addWidget(m_statusLabel, 1);
    body()->addLayout(statusRow);
    body()->addWidget(m_progress);
    body()->addWidget(m_stepLabel);
    body()->addWidget(m_output, 1);

    m_closeButton = addAction(tr("Close"), QStringLiteral("filled"));
    m_closeButton->setEnabled(false);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool TerminalOutputDialog::runCommandsModal(const QVector<ProcessRunner::Command> &commands,
                                             bool autoCloseOnSuccess)
{
    m_commands = commands;
    m_currentIndex = 0;
    m_running = true;
    m_autoCloseOnSuccess = autoCloseOnSuccess;

    if (m_commands.isEmpty()) {
        appendLine(tr("Nothing to do."));
        finishAll(true);
    } else {
        runNextCommand();
    }

    exec();
    return m_success;
}

void TerminalOutputDialog::runNextCommand()
{
    if (m_currentIndex >= m_commands.size()) {
        finishAll(true);
        return;
    }

    const ProcessRunner::Command &command = m_commands.at(m_currentIndex);
    // Each command starts indeterminate until its output says how far along it is.
    m_progress->setRange(0, 0);
    m_partialLine.clear();
    const QString program = command.program == QLatin1String("pkexec") && !command.args.isEmpty()
                                ? (command.args.first() == QLatin1String("env") ? command.args.value(2) : command.args.first())
                                : command.program;
    showStep(m_commands.size() > 1 ? tr("Step %1 of %2: running %3…").arg(m_currentIndex + 1).arg(m_commands.size()).arg(program)
                                   : tr("Running %1…").arg(program));
    if (m_currentIndex > 0)
        appendLine(QString()); // blank separator between command outputs
    appendLine(QStringLiteral("$ %1 %2").arg(command.program, command.args.join(' ')));

    m_process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this,
            [this]() { appendText(QString::fromUtf8(m_process->readAllStandardOutput())); });
    connect(m_process, &QProcess::readyReadStandardError, this,
            [this]() { appendText(QString::fromUtf8(m_process->readAllStandardError())); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            appendLine(tr("(failed to start)"));
            finishAll(false);
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                m_process->deleteLater();
                m_process = nullptr;

                if (status != QProcess::NormalExit || exitCode != 0) {
                    appendLine(tr("(exited with code %1)").arg(exitCode));
                    finishAll(false);
                    return;
                }
                ++m_currentIndex;
                runNextCommand();
            });

    if (!command.workingDirectory.isEmpty())
        m_process->setWorkingDirectory(command.workingDirectory);

    if (command.program == QLatin1String("pkexec"))
        PolkitAgent::ensureRunning(); // a password prompt needs an agent to show it
    m_process->start(command.program, ProcessRunner::launchArgs(command.program, command.args));
    m_process->closeWriteChannel();
}

void TerminalOutputDialog::finishAll(bool success)
{
    m_running = false;
    m_success = success;

    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const QColor color = success ? s.primary : s.error;
    QString failure = tr("Failed — see the output below");
    // pkexec's own failures, said plainly: 126 is a dismissed prompt, and
    // without an authentication agent there's no prompt at all.
    if (!success && (m_allOutput.contains(QLatin1String("No authentication agent found"))
                     || m_allOutput.contains(QLatin1String("textual authentication agent"))))
        failure = tr("Couldn't ask for your password: no polkit authentication agent is running in this session. Install "
                     "one (for example polkit-kde-agent or polkit-gnome) and log in again, or start it from your "
                     "compositor's autostart.");
    else if (!success && (m_allOutput.contains(QLatin1String("Request dismissed"))
                          || m_allOutput.contains(QLatin1String("Not authorized"))))
        failure = tr("Cancelled — the password prompt was dismissed, so nothing was changed.");
    m_statusLabel->setText(success ? tr("Completed successfully") : failure);
    m_statusLabel->setWordWrap(true);
    m_stepLabel->setVisible(!success && !m_stepLabel->text().isEmpty());
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    m_statusIcon->setPixmap(MaterialTheme::icon(success ? QStringLiteral("check_circle") : QStringLiteral("error"), color, 20, true)
                                .pixmap(20, 20));
    m_statusIcon->show();
    m_progress->hide();
    m_closeButton->setEnabled(true);
    m_closeButton->setFocus();

    if (success && m_autoCloseOnSuccess)
        accept();
}

void TerminalOutputDialog::showStep(const QString &text, int current, int total)
{
    m_stepLabel->setText(text);
    if (current >= 0 && total > 0) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(qBound(0, int(1000.0 * current / total), 1000));
    }
}

void TerminalOutputDialog::scanProgress(const QString &text)
{
    // Tools redraw progress with carriage returns when they think they're
    // on a terminal; treat those as line ends too.
    QString buffer = m_partialLine + text;
    buffer.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = buffer.split(QLatin1Char('\n'));
    m_partialLine = lines.last();
    for (int i = 0; i < lines.size() - 1; ++i)
        parseProgressLine(lines[i].trimmed());
}

void TerminalOutputDialog::parseProgressLine(const QString &line)
{
    if (line.isEmpty())
        return;
    auto sentence = [](QString text) {
        text = text.trimmed();
        if (!text.isEmpty())
            text[0] = text[0].toUpper();
        return text;
    };
    // APT with -o APT::Status-Fd=1: "pmstatus:firefox:42.8571:Unpacking firefox"
    static const QRegularExpression apt(QStringLiteral(R"(^(dl|pm)status:[^:]*:([\d.]+):(.*)$)"));
    // pacman: "( 3/10) upgrading firefox"
    static const QRegularExpression pacman(QStringLiteral(R"(^\(\s*(\d+)/(\d+)\)\s+(.+)$)"));
    // dnf5: "[ 3/10] Upgrading firefox-131.0-1.fc44.x86_64"
    static const QRegularExpression dnf5(QStringLiteral(R"(^\[\s*(\d+)/(\d+)\]\s+(.+)$)"));
    // dnf4: "  Upgrading        : firefox-131.0-1.fc40.x86_64     3/10"
    static const QRegularExpression dnf4(QStringLiteral(R"(^(\w[\w ]*?)\s*:\s*(\S+)\s+(\d+)/(\d+)$)"));
    // Flatpak (non-interactive): "Updating app/org.mozilla.firefox/x86_64/stable", "Installing 2/3…"
    static const QRegularExpression flatpak(QStringLiteral(R"(^(Installing|Updating|Uninstalling)\s+(\d+)/(\d+)\S*\s*(.*)$)"));
    static const QRegularExpression flatpakRef(QStringLiteral(R"(^(Installing|Updating|Uninstalling)\s+(?:app|runtime)/([^/\s]+))"));
    // Snap: "firefox 131.0 from Mozilla** refreshed"
    static const QRegularExpression snap(QStringLiteral(R"(^(\S+)\s.*\b(installed|refreshed|removed|reverted)$)"));

    QRegularExpressionMatch m;
    if ((m = apt.match(line)).hasMatch()) {
        const double percent = m.captured(2).toDouble();
        showStep(sentence(m.captured(3)), int(percent * 10), 1000);
    } else if ((m = pacman.match(line)).hasMatch()) {
        showStep(tr("%1 — %2 of %3").arg(sentence(m.captured(3))).arg(m.captured(1)).arg(m.captured(2)), m.captured(1).toInt(),
                 m.captured(2).toInt());
    } else if ((m = dnf5.match(line)).hasMatch()) {
        showStep(tr("%1 — %2 of %3").arg(sentence(m.captured(3))).arg(m.captured(1)).arg(m.captured(2)), m.captured(1).toInt(),
                 m.captured(2).toInt());
    } else if ((m = dnf4.match(line)).hasMatch()) {
        showStep(tr("%1 %2 — %3 of %4").arg(sentence(m.captured(1)), m.captured(2)).arg(m.captured(3)).arg(m.captured(4)),
                 m.captured(3).toInt(), m.captured(4).toInt());
    } else if ((m = flatpak.match(line)).hasMatch()) {
        showStep(tr("%1 %2 — %3 of %4").arg(m.captured(1), m.captured(4)).arg(m.captured(2)).arg(m.captured(3)),
                 m.captured(2).toInt() - 1, m.captured(3).toInt());
    } else if ((m = flatpakRef.match(line)).hasMatch()) {
        showStep(tr("%1 %2").arg(m.captured(1), m.captured(2)));
    } else if ((m = snap.match(line)).hasMatch()) {
        showStep(sentence(QStringLiteral("%1 %2").arg(m.captured(1), m.captured(2))));
    }
}

void TerminalOutputDialog::appendText(const QString &text)
{
    m_allOutput += text;
    scanProgress(text);
    // APT's Status-Fd lines are for the progress bar, not for reading.
    static const QRegularExpression statusLines(QStringLiteral("^(?:dl|pm)status:[^\n]*\n?"),
                                                QRegularExpression::MultilineOption);
    QString shown = text;
    shown.remove(statusLines);
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(shown);
    m_output->setTextCursor(cursor);
    m_output->ensureCursorVisible();
}

void TerminalOutputDialog::appendLine(const QString &line)
{
    appendText(line + '\n');
}

void TerminalOutputDialog::closeEvent(QCloseEvent *event)
{
    if (m_running)
        event->ignore();
    else
        QDialog::closeEvent(event);
}
