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
#include <QTextCursor>
#include <QVBoxLayout>

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

    m_process->start(command.program, command.args);
    m_process->closeWriteChannel();
}

void TerminalOutputDialog::finishAll(bool success)
{
    m_running = false;
    m_success = success;

    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const QColor color = success ? s.primary : s.error;
    m_statusLabel->setText(success ? tr("Completed successfully") : tr("Failed — see the output below"));
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

void TerminalOutputDialog::appendText(const QString &text)
{
    QTextCursor cursor = m_output->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
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
