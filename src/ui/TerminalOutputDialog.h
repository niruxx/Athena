#pragma once

#include <QDialog>
#include <QVector>

#include "../core/ProcessRunner.h"

class QPlainTextEdit;
class QLabel;
class QPushButton;
class QProcess;

// A small terminal-style window shown while install/remove/reinstall
// commands run: prints each command line as it starts, streams its
// stdout/stderr live as the process produces it, then shows a clear
// success/failure status once done. Runs its command sequence entirely on
// the GUI thread — QProcess is asynchronous by nature, so no worker
// thread is needed, and this dialog's own exec() call pumps the event
// loop that delivers its signals.
class TerminalOutputDialog : public QDialog {
    Q_OBJECT

public:
    explicit TerminalOutputDialog(const QString &title, QWidget *parent = nullptr);

    // Runs each command in order (stopping at the first failure), showing
    // the dialog modally. Returns once the user closes it — closing is
    // blocked until the sequence finishes. Returns true if every command
    // exited 0. If autoCloseOnSuccess is set and every command succeeds,
    // the dialog closes itself instead of waiting for the user.
    bool runCommandsModal(const QVector<ProcessRunner::Command> &commands, bool autoCloseOnSuccess);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void runNextCommand();
    void finishAll(bool success);
    void appendText(const QString &text);
    void appendLine(const QString &line);

    QPlainTextEdit *m_output;
    QLabel *m_statusLabel;
    QPushButton *m_closeButton;

    QVector<ProcessRunner::Command> m_commands;
    int m_currentIndex = 0;
    QProcess *m_process = nullptr;
    bool m_running = false;
    bool m_success = false;
    bool m_autoCloseOnSuccess = false;
};
