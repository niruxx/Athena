#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>

class QButtonGroup;

// One destination in the navigation rail: the icon (filled and tinted when
// active) over a label, and an optional badge. The active pill behind the
// icon is painted by the rail itself, so it can slide between destinations.
class RailButton : public QAbstractButton {
    Q_OBJECT

public:
    RailButton(const QString &iconName, const QString &label, QWidget *parent = nullptr);

    void setBadge(int count);
    QSize sizeHint() const override;
    // The pill's place inside the button.
    static QRect pillRect(int buttonWidth) { return QRect((buttonWidth - 56) / 2, 4, 56, 32); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_iconName;
    int m_badge = 0;
    bool m_hovered = false;
};

// Material 3 navigation rail: the app's top-level destinations down the
// left edge, replacing the old source dropdown + per-source tab bars.
class NavigationRail : public QWidget {
    Q_OBJECT

public:
    // Vertical: the rail down the window's left edge. Horizontal: Material's
    // bottom navigation bar, for narrow windows.
    explicit NavigationRail(QWidget *parent = nullptr, Qt::Orientation orientation = Qt::Vertical);

    // Destinations added after addStretch() sit at the bottom of the rail.
    int addDestination(const QString &iconName, const QString &label);
    void addStretch();

    void setCurrentIndex(int index);
    int currentIndex() const;
    // No destination selected (the current page isn't one of this bar's).
    void clearSelection();
    void setBadge(int index, int count);
    // Shows or hides a destination (a page turned off in Settings).
    void setDestinationVisible(int index, bool visible);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QRectF targetIndicator() const;
    void moveIndicator(bool animate);

    class QBoxLayout *m_layout;
    Qt::Orientation m_orientation;
    QButtonGroup *m_group;
    QVector<RailButton *> m_buttons;
    QRectF m_indicator;
    QVariantAnimation m_indicatorAnimation;
};
