#pragma once

#include <QDialog>

// Clean, structured replacement for a plain QMessageBox::question when
// confirming an install/remove/reinstall: a short bold summary line, then
// (if the backend produced one) the dependency-resolved plan in its own
// scrollable panel rather than jammed into a message box's body text.
class TransactionConfirmDialog : public QDialog {
    Q_OBJECT

public:
    TransactionConfirmDialog(const QString &title, const QString &summary, const QString &planText,
                              QWidget *parent = nullptr);

    // Builds and execs a dialog; returns true if the user confirmed.
    static bool confirm(QWidget *parent, const QString &title, const QString &summary,
                         const QString &planText = QString());
};
