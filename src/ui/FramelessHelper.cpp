#include "FramelessHelper.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace {
// How far in from the window's edge a press starts a resize.
constexpr int kResizeMargin = 6;
} // namespace

FramelessHelper::FramelessHelper(QWidget *window) : QObject(window), m_window(window) { }

void FramelessHelper::setEnabled(bool enabled)
{
    if (enabled == m_enabled && m_window->windowHandle())
        return;
    m_enabled = enabled;
    const bool wasVisible = m_window->isVisible();
    // Changing window flags recreates the native window, which hides it.
    m_window->setWindowFlag(Qt::FramelessWindowHint, enabled);
    if (wasVisible)
        m_window->show();
    watchWindowHandle();
    if (!enabled)
        setResizeCursor({});
}

void FramelessHelper::watchWindowHandle()
{
    m_window->winId(); // make sure the QWindow exists
    if (QWindow *handle = m_window->windowHandle())
        handle->installEventFilter(this);
}

void FramelessHelper::addDragArea(QWidget *area)
{
    m_dragAreas.insert(area);
    area->installEventFilter(this);
    connect(area, &QObject::destroyed, this, [this, area]() { m_dragAreas.remove(area); });
}

void FramelessHelper::toggleMaximized()
{
    if (m_window->isMaximized())
        m_window->showNormal();
    else
        m_window->showMaximized();
}

Qt::Edges FramelessHelper::edgesAt(const QPoint &pos) const
{
    if (!m_resizable || m_window->isMaximized() || m_window->isFullScreen())
        return {};
    Qt::Edges edges;
    if (pos.x() < kResizeMargin)
        edges |= Qt::LeftEdge;
    if (pos.x() >= m_window->width() - kResizeMargin)
        edges |= Qt::RightEdge;
    if (pos.y() < kResizeMargin)
        edges |= Qt::TopEdge;
    if (pos.y() >= m_window->height() - kResizeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}

void FramelessHelper::setResizeCursor(Qt::Edges edges)
{
    if (edges == m_cursorEdges)
        return;
    if (m_cursorEdges)
        QGuiApplication::restoreOverrideCursor();
    m_cursorEdges = edges;
    if (!edges)
        return;
    Qt::CursorShape shape = Qt::SizeAllCursor;
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    if (horizontal && vertical)
        shape = ((edges & Qt::LeftEdge) && (edges & Qt::TopEdge)) || ((edges & Qt::RightEdge) && (edges & Qt::BottomEdge))
            ? Qt::SizeFDiagCursor
            : Qt::SizeBDiagCursor;
    else
        shape = horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    QGuiApplication::setOverrideCursor(shape);
}

bool FramelessHelper::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled)
        return QObject::eventFilter(watched, event);

    // Window-level mouse events arrive here before any child widget sees
    // them, so the resize border works over whatever is underneath.
    if (watched == m_window->windowHandle()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() == Qt::NoButton)
                setResizeCursor(edgesAt(mouse->position().toPoint()));
            break;
        }
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const Qt::Edges edges = edgesAt(mouse->position().toPoint());
            if (mouse->button() == Qt::LeftButton && edges) {
                m_window->windowHandle()->startSystemResize(edges);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            setResizeCursor({});
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

    auto *area = qobject_cast<QWidget *>(watched);
    if (area && m_dragAreas.contains(area)) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_pressedArea = area;
                m_pressPos = mouse->globalPosition().toPoint();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_pressedArea == area && (mouse->buttons() & Qt::LeftButton)
                && (mouse->globalPosition().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_pressedArea = nullptr;
                m_window->windowHandle()->startSystemMove();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_pressedArea = nullptr;
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
                toggleMaximized();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
