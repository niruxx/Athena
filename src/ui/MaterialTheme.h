#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QObject>
#include <QString>

class QAbstractButton;

// Material You (Material Design 3) theming for Athena: a whole color
// scheme derived from one seed hue, the app-wide stylesheet that gives
// stock widgets M3 shapes, and Material Symbols icons rendered from the
// bundled icon font.
//
// The scheme is always available, whichever look is active: under the
// Breeze look it's derived from the current QPalette instead, so the new
// custom-painted widgets (navigation rail, app cards, list rows) fit in
// either way.
namespace MaterialTheme {

struct Scheme {
    bool dark = false;
    QColor primary, onPrimary, primaryContainer, onPrimaryContainer;
    QColor secondaryContainer, onSecondaryContainer;
    QColor tertiaryContainer, onTertiaryContainer;
    QColor surface, surfaceContainerLow, surfaceContainer, surfaceContainerHigh, surfaceContainerHighest;
    QColor onSurface, onSurfaceVariant, outline, outlineVariant;
    QColor inverseSurface, inverseOnSurface, inversePrimary;
    QColor error, errorContainer, onErrorContainer;
    QColor warnContainer, onWarnContainer;
};

// A color from OKLCH coordinates (lightness 0-1, chroma, hue in degrees),
// gamut-clipped to sRGB.
QColor oklch(double lightness, double chroma, double hueDegrees);

// The Material You scheme for a seed hue.
Scheme schemeForHue(int hue, bool dark);

// Applies the Material You look app-wide: Fusion (under AthenaStyle's
// metrics), the scheme's palette, and the generated stylesheet.
void applyMaterial(int hue, bool dark);
// Leaves the Material look: clears the stylesheet and switches the scheme
// back to following the palette (call after the Breeze look is applied).
void applyPaletteDerived();

const Scheme &current();
bool materialActive();

// Emits changed() whenever the scheme is re-derived, so widgets holding
// rendered icons (which a stylesheet can't recolor) can refresh them.
class Notifier : public QObject {
    Q_OBJECT
signals:
    void changed();
};
Notifier *notifier();

// The bundled Material Symbols Rounded subset. Only the icons listed in
// assets/fonts/update-material-symbols.sh are included.
QFont iconFont(int pixelSize, bool filled = false);
QIcon icon(const QString &name, const QColor &color, int pixelSize = 20, bool filled = false);

// Gives a button an M3 role ("filled", "tonal", "outlined", "text",
// "danger", "chip", "segment", "icon") for the stylesheet, plus an
// optional Material Symbols icon that's re-rendered in the role's content
// color whenever the theme changes.
void styleButton(QAbstractButton *button, const QString &role, const QString &iconName = QString());
// Swaps the icon of a button set up with styleButton() (e.g. maximize/restore).
void setButtonIcon(QAbstractButton *button, const QString &iconName);

// Stable 0-359 hue for an app's placeholder icon tile, from its name.
int hueForName(const QString &name);

} // namespace MaterialTheme
