#pragma once

#include <QWidget>

class CatalogHub;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Tools → Export & Import: the apps and packages you chose to install, from
// every source, saved to one file — and installing whatever such a file
// lists that this machine doesn't have yet.
class ExportImportPage : public QWidget {
    Q_OBJECT

public:
    ExportImportPage(CatalogHub *hub, const QString &nativeKey, QWidget *parent = nullptr);

private:
    void exportSetup();
    void importSetup();
    void installSelected();
    void setListVisible(bool visible);

    CatalogHub *m_hub;
    QString m_nativeKey;
    QWidget *m_importCard = nullptr;
    QLabel *m_importSummary;
    QListWidget *m_importList;
    QPushButton *m_installButton;
};

// Sources → Mirrors: rank pacman mirrors by speed (reflector or
// rate-mirrors), previewing the new list before it replaces the old one;
// on DNF, turn on its own fastest-mirror selection.
class MirrorsPage : public QWidget {
    Q_OBJECT

public:
    explicit MirrorsPage(const QString &nativeKey, QWidget *parent = nullptr);

private:
    void reload();
    void rank();
    void apply();
    void setPreviewVisible(bool visible);

    QString m_nativeKey;
    QWidget *m_card = nullptr;
    QString m_rankedFile;
    QLabel *m_status;
    QPlainTextEdit *m_current;
    QPlainTextEdit *m_preview;
    QWidget *m_previewBox;
    QPushButton *m_rankButton;
};
