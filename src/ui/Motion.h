#pragma once

#include <QPixmap>
#include <QRect>

class QAbstractItemView;
class QApplication;
class QStackedWidget;
class QWidget;

// Material motion for the whole app: page and tab transitions, list
// crossfades, hover fades on list rows and cards, and state layers (hover
// tint + press ripple) on every push button. Durations follow the
// desktop's own animation speed (KDE's AnimationDurationFactor, GNOME's
// enable-animations) and collapse to zero when the user turns animations
// off there or in Athena's settings.
namespace Motion {

bool enabled();
// `ms` scaled by the desktop's animation speed; 0 when animations are off.
int duration(int ms);

// App-wide hooks: state layers on buttons and transitions on every tab
// widget, attached as widgets are first polished. Call once at startup.
void install(QApplication *app);

// Switches a stacked widget's page with a fade-through from the old page.
void switchStack(QStackedWidget *stack, int index);
// Fades `before` (a snapshot of what `rect` of `host` showed) out over the
// live content underneath.
void fadeOver(QWidget *host, const QRect &rect, const QPixmap &before);
// Crossfades an item view's contents whenever its model resets.
void crossfadeOnReset(QAbstractItemView *view);

// Smooth hover for custom delegates: call trackHover() on the view, then
// read the row's 0-1 hover progress while painting.
void trackHover(QAbstractItemView *view);
qreal hoverProgress(const QWidget *view, int row, bool hoveredNow);

} // namespace Motion
