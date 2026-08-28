#include "TerminalOutputDialog.h"

#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

TerminalOutputDialog::TerminalOutputDialog(const QString &title, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(title);
    resize(640, 420);

    m_statusLabel = new QLabel(tr("Running..."), this);
    QFont statusFont = m_statusLabel->font();
    statusFont.setBold(true);
    m_statusLabel->setFont(statusFont);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    // A terminal-like scheme independent of the app's own theme, matching
    // how most terminal/IDE-console panels look regardless of app chrome.
    m_output->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #3c3c3c; }"));

    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setEnabled(false);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_output, 1);
    layout->addLayout(buttonRow);

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

    m_statusLabel->setText(success ? tr("✓ Completed successfully") : tr("✗ Failed"));
    m_statusLabel->setStyleSheet(success ? QStringLiteral("color: #2ecc71;") : QStringLiteral("color: #e74c3c;"));
    m_closeButton->setEnabled(true);

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
