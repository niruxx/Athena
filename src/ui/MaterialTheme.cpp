#include "MaterialTheme.h"

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleFactory>

#include "AthenaStyle.h"

namespace MaterialTheme {

namespace {

Scheme g_scheme;
bool g_materialActive = false;

QString iconFamily()
{
    static const QString family = []() {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/MaterialSymbolsRounded-subset.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QStringLiteral("Material Symbols Rounded") : families.first();
    }();
    return family;
}

double toSrgbChannel(double linear)
{
    const double clamped = std::clamp(linear, 0.0, 1.0);
    return clamped <= 0.0031308 ? 12.92 * clamped : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

QColor mix(const QColor &a, const QColor &b, double amountOfB)
{
    const double t = std::clamp(amountOfB, 0.0, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QColor withAlpha(QColor color, double alpha)
{
    color.setAlphaF(float(alpha));
    return color;
}

// QSS wants rgba(); QColor::name() drops alpha.
QString css(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

// Dropdown/spin arrows for the stylesheet, rendered from the icon font into
// the user cache (QSS can only reference images by path).
QString arrowImage(const QString &name, const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme");
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/%2-%3.png").arg(dir, name, color.name(QColor::HexRgb).mid(1));
    if (!QFile::exists(path))
        icon(name, color, 18).pixmap(QSize(18, 18), 2.0).save(path);
    return path;
}

QPalette paletteFor(const Scheme &s)
{
    QPalette p;
    p.setColor(QPalette::Window, s.surface);
    p.setColor(QPalette::WindowText, s.onSurface);
    p.setColor(QPalette::Base, s.surface);
    p.setColor(QPalette::AlternateBase, s.surfaceContainer);
    p.setColor(QPalette::Text, s.onSurface);
    p.setColor(QPalette::Button, s.surfaceContainerHigh);
    p.setColor(QPalette::ButtonText, s.onSurface);
    p.setColor(QPalette::Highlight, s.primary);
    p.setColor(QPalette::HighlightedText, s.onPrimary);
    p.setColor(QPalette::PlaceholderText, s.onSurfaceVariant);
    p.setColor(QPalette::ToolTipBase, s.inverseSurface);
    p.setColor(QPalette::ToolTipText, s.inverseOnSurface);
    p.setColor(QPalette::Link, s.primary);
    p.setColor(QPalette::LinkVisited, s.primary);
    p.setColor(QPalette::BrightText, s.onPrimary);
    p.setColor(QPalette::Light, s.surfaceContainerHighest);
    p.setColor(QPalette::Midlight, s.surfaceContainerHigh);
    p.setColor(QPalette::Mid, s.outlineVariant);
    p.setColor(QPalette::Dark, s.outline);
    p.setColor(QPalette::Shadow, QColor(0, 0, 0, 100));
    const QColor disabled = withAlpha(s.onSurface, 0.38);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, withAlpha(s.onSurface, 0.12));
    return p;
}

// Rules for Athena's own M3 widgets (cards, chips, rail, sheet...). Used
// under both looks, so the new layout stays coherent even on Breeze.
QString roleRules(const Scheme &s)
{
    const QString hover = css(withAlpha(s.onSurface, 0.08));
    const QString pressed = css(withAlpha(s.onSurface, 0.12));
    const QString disabledFill = css(withAlpha(s.onSurface, 0.12));
    const QString disabledText = css(withAlpha(s.onSurface, 0.38));
    QString q;
    q += QStringLiteral("QWidget#NavRail { background: %1; }\n QWidget#NavBar { background: %2; }\n")
             .arg(css(s.surface), css(s.surfaceContainer));
    q += QStringLiteral("QFrame#ContentPane { background: %1; border-top-left-radius: 28px; border-bottom-left-radius: 28px; }\n")
             .arg(css(s.surfaceContainerLow));
    q += QStringLiteral("QFrame#DetailsSheet { background: %1; border-top-left-radius: 16px; border-bottom-left-radius: 16px; "
                        "border-left: 1px solid %2; }\n")
             .arg(css(s.surfaceContainerLow), css(s.outlineVariant));
    q += QStringLiteral("QFrame#Snackbar { background: %1; border-radius: 8px; }\n QFrame#Snackbar QLabel { color: %2; }\n"
                        "QFrame#Snackbar QPushButton { color: %3; }\n")
             .arg(css(s.inverseSurface), css(s.inverseOnSurface), css(s.inversePrimary));

    q += QStringLiteral("QFrame[m3=\"card\"] { background: %1; border-radius: 16px; }\n").arg(css(s.surface));
    q += QStringLiteral("QFrame[m3=\"hero\"] { background: %1; border-radius: 28px; }\n QFrame[m3=\"hero\"] QLabel { color: %2; }\n")
             .arg(css(s.primaryContainer), css(s.onPrimaryContainer));
    q += QStringLiteral("QFrame[m3=\"warn\"] { background: %1; border-radius: 16px; }\n QFrame[m3=\"warn\"] QLabel { color: %2; }\n")
             .arg(css(s.warnContainer), css(s.onWarnContainer));
    q += QStringLiteral("QFrame[m3=\"callout\"] { background: %1; border-radius: 12px; }\n QFrame[m3=\"callout\"] QLabel { color: %2; }\n")
             .arg(css(s.tertiaryContainer), css(s.onTertiaryContainer));
    q += QStringLiteral("QFrame[m3=\"fact\"] { background: %1; border-radius: 12px; }\n").arg(css(s.surface));

    q += QStringLiteral("QLabel[m3=\"display\"] { font-size: 44px; }\n QLabel[m3=\"headline\"] { font-size: 24px; }\n"
                        "QLabel[m3=\"title\"] { font-size: 22px; }\n QLabel[m3=\"titleM\"] { font-size: 16px; font-weight: 600; }\n"
                        "QLabel[m3=\"metric\"] { font-size: 32px; }\n");
    q += QStringLiteral("QLabel[m3=\"label\"] { color: %1; font-size: 12px; font-weight: 600; }\n QLabel[m3=\"muted\"] { color: %1; }\n")
             .arg(css(s.onSurfaceVariant));

    // Buttons. Base metrics shared by every role.
    q += QStringLiteral("QAbstractButton[m3] { border-radius: 20px; padding: 8px 20px; font-weight: 600; min-height: 24px; }\n");
    q += QStringLiteral("QAbstractButton[m3=\"filled\"] { background: %1; color: %2; border: none; }\n"
                        "QAbstractButton[m3=\"filled\"]:hover { background: %3; }\n"
                        "QAbstractButton[m3=\"filled\"]:disabled { background: %4; color: %5; }\n")
             .arg(css(s.primary), css(s.onPrimary), css(mix(s.primary, s.onPrimary, 0.10)), disabledFill, disabledText);
    q += QStringLiteral("QAbstractButton[m3=\"tonal\"] { background: %1; color: %2; border: none; }\n"
                        "QAbstractButton[m3=\"tonal\"]:hover { background: %3; }\n"
                        "QAbstractButton[m3=\"tonal\"]:disabled { background: %4; color: %5; }\n")
             .arg(css(s.secondaryContainer), css(s.onSecondaryContainer),
                  css(mix(s.secondaryContainer, s.onSecondaryContainer, 0.08)), disabledFill, disabledText);
    q += QStringLiteral("QAbstractButton[m3=\"danger\"] { background: %1; color: %2; border: none; }\n"
                        "QAbstractButton[m3=\"danger\"]:hover { background: %3; }\n")
             .arg(css(s.errorContainer), css(s.onErrorContainer), css(mix(s.errorContainer, s.onErrorContainer, 0.08)));
    q += QStringLiteral("QAbstractButton[m3=\"outlined\"] { background: transparent; color: %1; border: 1px solid %2; }\n"
                        "QAbstractButton[m3=\"outlined\"]:hover { background: %3; }\n"
                        "QAbstractButton[m3=\"outlined\"]:disabled { color: %4; border-color: %5; }\n")
             .arg(css(s.primary), css(s.outline), css(withAlpha(s.primary, 0.08)), disabledText, disabledFill);
    q += QStringLiteral("QAbstractButton[m3=\"text\"] { background: transparent; color: %1; border: none; padding: 8px 12px; }\n"
                        "QAbstractButton[m3=\"text\"]:hover { background: %2; }\n")
             .arg(css(s.primary), css(withAlpha(s.primary, 0.08)));
    q += QStringLiteral("QAbstractButton[m3=\"icon\"] { background: transparent; border: none; padding: 8px; min-width: 24px; }\n"
                        "QAbstractButton[m3=\"icon\"]:hover { background: %1; }\n QAbstractButton[m3=\"icon\"]:pressed { background: %2; }\n")
             .arg(hover, pressed);
    q += QStringLiteral("QAbstractButton[m3=\"window\"], QAbstractButton[m3=\"windowClose\"] { background: transparent; border: none; "
                        "border-radius: 16px; padding: 7px; min-width: 18px; }\n"
                        "QAbstractButton[m3=\"window\"]:hover { background: %1; }\n"
                        "QAbstractButton[m3=\"windowClose\"]:hover { background: %2; }\n")
             .arg(hover, css(s.errorContainer));
    q += QStringLiteral("QWidget#WindowFrame { background: %1; }\n QWidget#WindowFrame[framed=\"true\"] { border: 1px solid %2; }\n")
             .arg(css(s.surface), css(s.outlineVariant));
    q += QStringLiteral("QAbstractButton[m3=\"chip\"] { background: transparent; color: %1; border: 1px solid %2; border-radius: 8px; "
                        "padding: 5px 14px; font-weight: 500; min-height: 20px; }\n"
                        "QAbstractButton[m3=\"chip\"]:hover { background: %3; }\n"
                        "QAbstractButton[m3=\"chip\"]:checked { background: %4; color: %5; border-color: %4; }\n")
             .arg(css(s.onSurfaceVariant), css(s.outlineVariant), hover, css(s.secondaryContainer), css(s.onSecondaryContainer));
    q += QStringLiteral("QAbstractButton[m3=\"segment\"] { background: transparent; color: %1; border: 1px solid %2; border-radius: 0; "
                        "padding: 8px 16px; font-weight: 500; }\n"
                        "QAbstractButton[m3=\"segment\"][m3pos=\"first\"] { border-top-left-radius: 20px; border-bottom-left-radius: 20px; }\n"
                        "QAbstractButton[m3=\"segment\"][m3pos=\"last\"] { border-top-right-radius: 20px; border-bottom-right-radius: 20px; }\n"
                        "QAbstractButton[m3=\"segment\"][m3pos=\"only\"] { border-radius: 20px; }\n"
                        "QAbstractButton[m3=\"segment\"]:hover { background: %3; }\n"
                        "QAbstractButton[m3=\"segment\"]:checked { background: %4; color: %5; }\n")
             .arg(css(s.onSurface), css(s.outline), hover, css(s.secondaryContainer), css(s.onSecondaryContainer));

    q += QStringLiteral("QLineEdit[m3=\"search\"] { background: %1; color: %2; border: none; border-radius: 28px; padding: 0 22px; "
                        "min-height: 56px; font-size: 16px; selection-background-color: %3; selection-color: %4; }\n")
             .arg(css(s.surfaceContainerHighest), css(s.onSurface), css(s.primary), css(s.onPrimary));
    q += QStringLiteral("QAbstractItemView[m3=\"plain\"] { background: transparent; border: none; }\n");
    q += QStringLiteral("QAbstractItemView[m3=\"sidebar\"] { background: transparent; border: none; }\n");
    q += QStringLiteral("QSplitter[m3=\"divider\"]::handle { background: %1; }\n").arg(css(s.outlineVariant));
    q += QStringLiteral("QScrollArea[m3=\"page\"], QScrollArea[m3=\"page\"] > QWidget > QWidget { background: transparent; border: none; }\n");
    return q;
}

// Stock widgets restyled with M3 shapes — only under the Material look.
QString widgetRules(const Scheme &s)
{
    const QString hover = css(withAlpha(s.onSurface, 0.08));
    const QString disabledText = css(withAlpha(s.onSurface, 0.38));
    const QString down = arrowImage(QStringLiteral("expand_more"), s.onSurfaceVariant);
    const QString up = arrowImage(QStringLiteral("expand_less"), s.onSurfaceVariant);
    QString q;
    q += QStringLiteral("QToolTip { background: %1; color: %2; border: none; border-radius: 4px; padding: 4px 8px; }\n")
             .arg(css(s.inverseSurface), css(s.inverseOnSurface));
    q += QStringLiteral("QMenuBar { background: %1; padding: 2px 6px; }\n QMenuBar::item { padding: 6px 12px; border-radius: 8px; background: transparent; }\n"
                        "QMenuBar::item:selected { background: %2; }\n")
             .arg(css(s.surface), css(s.surfaceContainerHigh));
    q += QStringLiteral("QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 6px 0; }\n"
                        "QMenu::item { padding: 8px 28px 8px 16px; } QMenu::item:selected { background: %3; }\n"
                        "QMenu::item:disabled { color: %4; } QMenu::separator { height: 1px; background: %2; margin: 6px 0; }\n"
                        "QMenu::icon { padding-left: 10px; }\n")
             .arg(css(s.surfaceContainer), css(s.outlineVariant), css(s.surfaceContainerHighest), disabledText);

    // Plain push buttons in the classic pages become outlined pills.
    q += QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 18px; padding: 7px 18px; "
                        "min-height: 22px; font-weight: 500; }\n QPushButton:hover { background: %3; }\n"
                        "QPushButton:pressed { background: %4; }\n QPushButton:disabled { color: %5; border-color: %6; }\n")
             .arg(css(s.primary), css(s.outline), css(withAlpha(s.primary, 0.08)), css(withAlpha(s.primary, 0.12)), disabledText,
                  css(withAlpha(s.onSurface, 0.12)));
    q += QStringLiteral("QToolButton { border: none; border-radius: 18px; padding: 6px; background: transparent; }\n"
                        "QToolButton:hover { background: %1; }\n")
             .arg(hover);

    const QString fieldBase = QStringLiteral("background: %1; color: %2; border: 1px solid %3; border-radius: 8px; "
                                             "selection-background-color: %4; selection-color: %5;")
                                  .arg(css(s.surface), css(s.onSurface), css(s.outline), css(s.primary), css(s.onPrimary));
    q += QStringLiteral("QLineEdit, QSpinBox, QComboBox { %1 padding: 6px 10px; min-height: 22px; }\n").arg(fieldBase);
    q += QStringLiteral("QPlainTextEdit, QTextEdit { %1 padding: 4px; }\n").arg(fieldBase);
    q += QStringLiteral("QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 2px solid %1; }\n")
             .arg(css(s.primary));
    q += QStringLiteral("QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %1; border-color: %2; }\n")
             .arg(disabledText, css(withAlpha(s.onSurface, 0.12)));
    q += QStringLiteral("QComboBox::drop-down { border: none; width: 30px; }\n QComboBox::down-arrow { image: url(%1); width: 18px; height: 18px; }\n"
                        "QComboBox QAbstractItemView { background: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; "
                        "selection-background-color: %4; selection-color: %5; outline: 0; }\n")
             .arg(down, css(s.surfaceContainer), css(s.outlineVariant), css(s.secondaryContainer), css(s.onSecondaryContainer));
    q += QStringLiteral("QSpinBox::up-button, QSpinBox::down-button { border: none; width: 24px; background: transparent; }\n"
                        "QSpinBox::up-arrow { image: url(%1); width: 16px; height: 16px; }\n"
                        "QSpinBox::down-arrow { image: url(%2); width: 16px; height: 16px; }\n")
             .arg(up, down);

    q += QStringLiteral("QTabWidget::pane { border: none; }\n QTabBar { qproperty-drawBase: 0; background: transparent; }\n"
                        "QTabBar::tab { background: transparent; color: %1; padding: 10px 18px; border: none; border-bottom: 3px solid transparent; "
                        "font-weight: 600; margin-right: 2px; }\n QTabBar::tab:hover { background: %2; border-top-left-radius: 8px; "
                        "border-top-right-radius: 8px; }\n QTabBar::tab:selected { color: %3; border-bottom: 3px solid %3; }\n")
             .arg(css(s.onSurfaceVariant), hover, css(s.primary));

    q += QStringLiteral("QGroupBox { background: %1; border: none; border-radius: 16px; margin-top: 30px; padding: 14px 6px 6px 6px; "
                        "font-weight: 600; }\n QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 6px; "
                        "top: 4px; color: %2; }\n")
             .arg(css(s.surface), css(s.onSurface));

    q += QStringLiteral("QAbstractItemView { background: %1; alternate-background-color: %2; border: 1px solid %3; border-radius: 12px; "
                        "selection-background-color: %4; selection-color: %5; outline: 0; }\n"
                        "QAbstractItemView::item:hover { background: %6; }\n"
                        "QAbstractItemView::item:selected { background: %4; color: %5; }\n")
             .arg(css(s.surface), css(s.surfaceContainerLow), css(s.outlineVariant), css(s.secondaryContainer),
                  css(s.onSecondaryContainer), hover);
    // M3List views: a borderless, rounded container for the rows.
    q += QStringLiteral("QAbstractItemView[m3=\"list\"] { background: %1; border: none; border-radius: 16px; padding: 6px 0; }\n"
                        "QAbstractItemView[m3=\"listPlain\"] { background: transparent; border: none; padding: 0; }\n"
                        "QAbstractItemView[m3=\"table\"] { background: %1; border: none; border-radius: 16px; }\n"
                        "QAbstractItemView[m3=\"table\"]::item { border-bottom: 1px solid %2; padding: 0 6px; }\n"
                        "QAbstractItemView[m3=\"table\"] QHeaderView::section { background: %1; padding: 12px 10px; }\n")
             .arg(css(s.surface), css(s.surfaceContainer));
    q += QStringLiteral("QHeaderView { background: transparent; border: none; }\n"
                        "QHeaderView::section { background: %1; color: %2; border: none; border-bottom: 1px solid %3; padding: 8px 10px; "
                        "font-weight: 600; }\n QTableCornerButton::section { background: %1; border: none; }\n")
             .arg(css(s.surfaceContainer), css(s.onSurfaceVariant), css(s.outlineVariant));

    q += QStringLiteral("QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }\n"
                        "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }\n"
                        "QScrollBar::handle { background: %1; border-radius: 4px; margin: 2px; }\n"
                        "QScrollBar::handle:vertical { min-height: 32px; } QScrollBar::handle:horizontal { min-width: 32px; }\n"
                        "QScrollBar::handle:hover { background: %2; }\n"
                        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }\n"
                        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }\n")
             .arg(css(s.outlineVariant), css(s.outline));
    q += QStringLiteral("QProgressBar { background: %1; border: none; border-radius: 4px; min-height: 8px; text-align: center; color: %2; }\n"
                        "QProgressBar::chunk { background: %3; border-radius: 4px; }\n")
             .arg(css(s.surfaceContainerHighest), css(s.onSurface), css(s.primary));
    q += QStringLiteral("QSplitter::handle { background: transparent; }\n QStatusBar { background: %1; }\n").arg(css(s.surface));
    q += QStringLiteral("QDialog { background: %1; }\n").arg(css(s.surfaceContainerHigh));
    // M3Dialog paints its own rounded card on a transparent window.
    q += QStringLiteral("QDialog#M3Dialog, QWidget#M3DialogContent { background: transparent; }\n");
    return q;
}

// Under the Material look, Motion's state layers draw button hover and
// press (with a fade and ripple); the stylesheet's instant :hover/:pressed
// backgrounds would double them up.
QString withoutButtonStateRules(const QString &qss)
{
    static const QRegularExpression stateRule(QStringLiteral(
        R"re(^\s*(QAbstractButton\[m3="(filled|tonal|danger|outlined|text|icon|chip|segment|window)"\]|QPushButton|QToolButton):(hover|pressed)\b)re"));
    QStringList kept;
    for (const QString &line : qss.split('\n')) {
        if (!stateRule.match(line).hasMatch())
            kept << line;
    }
    return kept.join('\n');
}

void apply(const Scheme &scheme, bool material)
{
    g_scheme = scheme;
    g_materialActive = material;
    qApp->setStyleSheet(material ? withoutButtonStateRules(roleRules(scheme) + widgetRules(scheme)) : roleRules(scheme));
    emit notifier()->changed();
}

Scheme derivedFromPalette(const QPalette &p)
{
    Scheme s;
    s.dark = p.color(QPalette::Window).lightnessF() < 0.5;
    const QColor accent = p.color(QPalette::Highlight);
    const QColor base = p.color(QPalette::Base);
    const QColor window = p.color(QPalette::Window);
    const QColor text = p.color(QPalette::WindowText);
    s.primary = accent;
    s.onPrimary = p.color(QPalette::HighlightedText);
    s.primaryContainer = mix(base, accent, s.dark ? 0.35 : 0.18);
    s.onPrimaryContainer = text;
    s.secondaryContainer = mix(base, accent, s.dark ? 0.25 : 0.12);
    s.onSecondaryContainer = text;
    s.tertiaryContainer = mix(base, QColor::fromHsvF(std::fmod(accent.hsvHueF() + 0.2f, 1.0f), 0.5f, 0.8f), s.dark ? 0.3 : 0.15);
    s.onTertiaryContainer = text;
    s.surface = window;
    s.surfaceContainerLow = base;
    s.surfaceContainer = mix(window, text, 0.04);
    s.surfaceContainerHigh = mix(window, text, 0.07);
    s.surfaceContainerHighest = mix(window, text, 0.10);
    s.onSurface = text;
    s.onSurfaceVariant = mix(text, window, 0.3);
    s.outline = mix(text, window, 0.5);
    s.outlineVariant = mix(text, window, 0.8);
    s.inverseSurface = mix(text, window, 0.1);
    s.inverseOnSurface = window;
    s.inversePrimary = s.dark ? accent.darker(140) : accent.lighter(140);
    s.error = QColor(218, 68, 83);
    s.errorContainer = mix(base, s.error, 0.2);
    s.onErrorContainer = text;
    s.warnContainer = mix(base, QColor(246, 116, 0), 0.2);
    s.onWarnContainer = text;
    return s;
}

} // namespace

QColor oklch(double lightness, double chroma, double hueDegrees)
{
    const double h = hueDegrees * M_PI / 180.0;
    const double a = chroma * std::cos(h);
    const double b = chroma * std::sin(h);
    const double l = std::pow(lightness + 0.3963377774 * a + 0.2158037573 * b, 3);
    const double m = std::pow(lightness - 0.1055613458 * a - 0.0638541728 * b, 3);
    const double s = std::pow(lightness - 0.0894841775 * a - 1.2914855480 * b, 3);
    const double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const double bl = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return QColor::fromRgbF(float(toSrgbChannel(r)), float(toSrgbChannel(g)), float(toSrgbChannel(bl)));
}

Scheme schemeForHue(int hue, bool dark)
{
    const double h = hue;
    const double t = h + 70; // tertiary hue
    Scheme s;
    s.dark = dark;
    if (!dark) {
        s.primary = oklch(0.47, 0.11, h);
        s.onPrimary = oklch(0.99, 0.01, h);
        s.primaryContainer = oklch(0.91, 0.07, h);
        s.onPrimaryContainer = oklch(0.28, 0.08, h);
        s.secondaryContainer = oklch(0.91, 0.03, h);
        s.onSecondaryContainer = oklch(0.28, 0.03, h);
        s.tertiaryContainer = oklch(0.91, 0.06, t);
        s.onTertiaryContainer = oklch(0.30, 0.07, t);
        s.surface = oklch(0.985, 0.006, h);
        s.surfaceContainerLow = oklch(0.965, 0.009, h);
        s.surfaceContainer = oklch(0.945, 0.011, h);
        s.surfaceContainerHigh = oklch(0.925, 0.012, h);
        s.surfaceContainerHighest = oklch(0.905, 0.013, h);
        s.onSurface = oklch(0.22, 0.012, h);
        s.onSurfaceVariant = oklch(0.43, 0.016, h);
        s.outline = oklch(0.62, 0.016, h);
        s.outlineVariant = oklch(0.85, 0.012, h);
        s.inverseSurface = oklch(0.30, 0.012, h);
        s.inverseOnSurface = oklch(0.95, 0.008, h);
        s.inversePrimary = oklch(0.82, 0.10, h);
        s.error = oklch(0.52, 0.19, 27);
        s.errorContainer = oklch(0.93, 0.05, 27);
        s.onErrorContainer = oklch(0.33, 0.12, 27);
        s.warnContainer = oklch(0.93, 0.07, 85);
        s.onWarnContainer = oklch(0.36, 0.08, 70);
    } else {
        s.primary = oklch(0.82, 0.10, h);
        s.onPrimary = oklch(0.30, 0.08, h);
        s.primaryContainer = oklch(0.38, 0.09, h);
        s.onPrimaryContainer = oklch(0.92, 0.06, h);
        s.secondaryContainer = oklch(0.36, 0.03, h);
        s.onSecondaryContainer = oklch(0.90, 0.03, h);
        s.tertiaryContainer = oklch(0.38, 0.07, t);
        s.onTertiaryContainer = oklch(0.92, 0.05, t);
        s.surface = oklch(0.17, 0.008, h);
        s.surfaceContainerLow = oklch(0.20, 0.009, h);
        s.surfaceContainer = oklch(0.22, 0.010, h);
        s.surfaceContainerHigh = oklch(0.25, 0.011, h);
        s.surfaceContainerHighest = oklch(0.28, 0.012, h);
        s.onSurface = oklch(0.92, 0.008, h);
        s.onSurfaceVariant = oklch(0.78, 0.015, h);
        s.outline = oklch(0.60, 0.015, h);
        s.outlineVariant = oklch(0.36, 0.012, h);
        s.inverseSurface = oklch(0.90, 0.008, h);
        s.inverseOnSurface = oklch(0.25, 0.01, h);
        s.inversePrimary = oklch(0.47, 0.11, h);
        s.error = oklch(0.78, 0.12, 27);
        s.errorContainer = oklch(0.36, 0.10, 27);
        s.onErrorContainer = oklch(0.90, 0.04, 27);
        s.warnContainer = oklch(0.38, 0.07, 75);
        s.onWarnContainer = oklch(0.92, 0.06, 85);
    }
    return s;
}

void applyMaterial(int hue, bool dark)
{
    const Scheme scheme = schemeForHue(hue, dark);
    QApplication::setStyle(new AthenaStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QApplication::setPalette(paletteFor(scheme));
    apply(scheme, true);
}

void applyPaletteDerived()
{
    apply(derivedFromPalette(QApplication::palette()), false);
}

const Scheme &current()
{
    return g_scheme;
}

bool materialActive()
{
    return g_materialActive;
}

Notifier *notifier()
{
    static Notifier instance;
    return &instance;
}

QFont iconFont(int pixelSize, bool filled)
{
    QFont font(iconFamily());
    font.setPixelSize(pixelSize);
    font.setStyleStrategy(QFont::PreferAntialias);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setVariableAxis(QFont::Tag("FILL"), filled ? 1.0f : 0.0f);
#else
    Q_UNUSED(filled);
#endif
    return font;
}

QIcon icon(const QString &name, const QColor &color, int pixelSize, bool filled)
{
    QIcon result;
    for (const qreal ratio : {1.0, 2.0}) {
        QPixmap pixmap(QSize(pixelSize, pixelSize) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(iconFont(pixelSize, filled));
        painter.setPen(color);
        painter.drawText(QRect(0, 0, pixelSize, pixelSize), Qt::AlignCenter, name);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

namespace {
// Renders a styled button's icon from its "m3" role and "m3icon" name, in
// the role's content color (a stylesheet can't recolor icons).
void renderButtonIcon(QAbstractButton *button)
{
    const QString role = button->property("m3").toString();
    const QString iconName = button->property("m3icon").toString();
    if (iconName.isEmpty())
        return;
    const Scheme &s = current();
    QColor normal = s.primary;
    const QColor checked = s.onSecondaryContainer;
    if (role == QLatin1String("filled"))
        normal = s.onPrimary;
    else if (role == QLatin1String("tonal"))
        normal = s.onSecondaryContainer;
    else if (role == QLatin1String("danger"))
        normal = s.onErrorContainer;
    else if (role == QLatin1String("segment"))
        normal = s.onSurface;
    else if (role == QLatin1String("chip") || role == QLatin1String("icon") || role.startsWith(QLatin1String("window")))
        normal = s.onSurfaceVariant;

    const int size = role == QLatin1String("icon") ? 22 : 18;
    QIcon composed;
    for (const QIcon::State state : {QIcon::Off, QIcon::On}) {
        const QColor color = state == QIcon::On ? checked : normal;
        const QString glyph = (state == QIcon::On && role == QLatin1String("chip")) ? QStringLiteral("check") : iconName;
        for (const qreal ratio : {1.0, 2.0}) {
            composed.addPixmap(icon(glyph, color, size, state == QIcon::On).pixmap(QSize(size, size), ratio), QIcon::Normal, state);
            composed.addPixmap(icon(glyph, withAlpha(s.onSurface, 0.38), size).pixmap(QSize(size, size), ratio), QIcon::Disabled,
                               state);
        }
    }
    button->setIcon(composed);
    button->setIconSize(QSize(size, size));
}
} // namespace

void styleButton(QAbstractButton *button, const QString &role, const QString &iconName)
{
    button->setProperty("m3", role);
    button->setCursor(Qt::PointingHandCursor);
    if (iconName.isEmpty())
        return;
    button->setProperty("m3icon", iconName);
    renderButtonIcon(button);
    QObject::connect(notifier(), &Notifier::changed, button, [button]() { renderButtonIcon(button); });
}

void setButtonIcon(QAbstractButton *button, const QString &iconName)
{
    button->setProperty("m3icon", iconName);
    renderButtonIcon(button);
}

int hueForName(const QString &name)
{
    return int(qHash(name.toLower()) % 360);
}

} // namespace MaterialTheme
