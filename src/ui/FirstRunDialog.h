#pragma once

#include "M3Dialog.h"

class QLabel;
class QPushButton;
class QStackedWidget;

// The first-run setup wizard, shown once on first launch
// (AppSettings::hasCompletedFirstRun() gates it) and reopenable from
// Settings → General → "Run First-Time Setup Again". Five steps — welcome,
// look, sources, preferences, done — each applying its choices live (like
// Settings), with Back/Next, page dots, and a skip at any point.
class FirstRunDialog : public M3Dialog {
    Q_OBJECT

public:
    explicit FirstRunDialog(const QString &detectedBackendName, QWidget *parent = nullptr);

private:
    void goTo(int page);

    QStackedWidget *m_pages;
    class PageDots *m_dots;
    QPushButton *m_back;
    QPushButton *m_next;
    QPushButton *m_skip;
    QLabel *m_doneSummary;
};
