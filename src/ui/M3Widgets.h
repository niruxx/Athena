#pragma once

#include <functional>

#include <QAbstractButton>
#include <QFrame>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

// Small Material 3 building blocks shared by the redesigned pages.

// Bottom-center snackbar: a short status message, optionally with one
// action, that hides itself after a few seconds. Lives over its parent.
class Snackbar : public QFrame {
    Q_OBJECT

public:
    explicit Snackbar(QWidget *parent);
    void showMessage(const QString &text, const QString &actionText = QString(), std::function<void()> action = {});

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();
    void dismiss();
    QPoint restingPosition() const;

    class QGraphicsOpacityEffect *m_opacity;
    class QParallelAnimationGroup *m_animation;
    class QPropertyAnimation *m_slide;
    class QPropertyAnimation *m_fade;
    QLabel *m_text;
    QPushButton *m_action;
    QTimer *m_timer;
    std::function<void()> m_callback;
};

// A horizontal bar of rounded segments in proportion to their values (the
// Home updates breakdown).
class StackedBar : public QWidget {
    Q_OBJECT

public:
    explicit StackedBar(QWidget *parent = nullptr);
    void setSegments(const QVector<QPair<int, QColor>> &segments);
    QSize sizeHint() const override { return QSize(200, 12); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPair<int, QColor>> m_segments;
};

// Material 3 switch: a pill track with a thumb that grows and slides when
// turned on.
class M3Switch : public QAbstractButton {
    Q_OBJECT

public:
    explicit M3Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(52, 32); }
    // Sets the state without animating (e.g. when reflecting saved state).
    void setCheckedQuietly(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    QVariantAnimation m_position; // 0 = off, 1 = on
};

namespace SystemTools {
struct Activity;
}

namespace M3 {

// One line of activity history: an action icon tile, "Upgraded firefox
// 1.0 → 1.1", and its source and relative time.
QWidget *activityRow(const SystemTools::Activity &activity, QWidget *parent);


// A label carrying an M3 type role ("display", "headline", "title",
// "titleM", "label", "muted") for the stylesheet.
QLabel *label(const QString &text, const QString &role, QWidget *parent);

// A row of exclusive filter chips; returns the group (button ids are the
// indices of `labels`).
QButtonGroup *chipRow(const QVector<QPair<QString, QString>> &labelsAndIcons, QWidget *parent, QHBoxLayout *into);

// A connected segmented button; returns the group (ids are indices).
QButtonGroup *segmented(const QVector<QPair<QString, QString>> &labelsAndIcons, QWidget *parent, QHBoxLayout *into);

// The Material You accent picker: seed swatches plus a custom-hue slider,
// applied live through AppSettings (Settings and the first-run dialog).
QWidget *accentPicker(QWidget *parent);

// A rounded card frame of the given role ("card", "hero", "warn", "callout").
QFrame *card(const QString &role, QWidget *parent);

// A Material Symbols icon as a label, in the primary color — or, with
// `tile`, on a 40px secondary-container tile (as list rows lead with).
// Follows theme changes.
QLabel *symbolLabel(const QString &symbol, int size, QWidget *parent, bool tile = false);

// A card with an icon + title header and optional supporting text;
// `content` is its layout, to add the section's own widgets below.
QFrame *section(QWidget *parent, const QString &icon, const QString &title, const QString &text, QVBoxLayout *&content);

// A list row with a leading symbol tile, a title and supporting line, and
// a trailing M3 switch (returned in `toggle`); clicking the row flips it.
QWidget *switchRow(QWidget *parent, const QString &symbol, const QString &title, const QString &supporting, M3Switch *&toggle);

// A 1px outline-variant line between rows in a card.
QFrame *divider(QWidget *parent);

} // namespace M3
