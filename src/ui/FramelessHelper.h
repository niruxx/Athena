#pragma once

#include <QObject>
#include <QPoint>
#include <QSet>

class QWidget;

// Lets a top-level window draw its own title bar: removes the desktop's
// decoration, turns chosen widgets into drag areas (press to move,
// double-click to maximize), and resizes from the window's edges. Moving
// and resizing are handed to the compositor (QWindow::startSystemMove /
// startSystemResize), so they behave natively on Wayland and X11 alike —
// snapping, tiling, and multi-monitor included.
class FramelessHelper : public QObject {
    Q_OBJECT

public:
    explicit FramelessHelper(QWidget *window);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void addDragArea(QWidget *area);
    // Whether the window's edges resize it (on by default).
    void setResizable(bool resizable) { m_resizable = resizable; }
    void toggleMaximized();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &windowPos) const;
    void setResizeCursor(Qt::Edges edges);
    void watchWindowHandle();

    QWidget *m_window;
    QSet<QWidget *> m_dragAreas;
    bool m_enabled = false;
    bool m_resizable = true;
    Qt::Edges m_cursorEdges;
    // Where a drag-area press started; the system move begins once the
    // pointer travels past the drag threshold (starting it on press would
    // grab the pointer and swallow double-clicks).
    QWidget *m_pressedArea = nullptr;
    QPoint m_pressPos;
};
