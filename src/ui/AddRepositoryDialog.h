#pragma once

#include <QDialog>

#include "../core/RepositoryInfo.h"

class QLineEdit;

// Builds its form dynamically from PackageBackend::repositoryAddFields(),
// so one dialog works for every backend instead of hardcoding fields per
// package manager.
class AddRepositoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddRepositoryDialog(const QVector<RepositoryAddField> &fields, QWidget *parent = nullptr);

    RepositoryAddValues values() const;

private slots:
    void validateAndAccept();

private:
    QVector<RepositoryAddField> m_fields;
    QMap<QString, QLineEdit *> m_edits;
};
