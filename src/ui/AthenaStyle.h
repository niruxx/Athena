#pragma once

#include <QPalette>
#include <QProxyStyle>

class QAbstractItemView;

// Application-wide look, modeled on KDE Plasma's Discover: wraps the real
// widget style (Breeze when it's installed, Fusion otherwise) and only
// loosens its metrics — roomier layout margins/spacing, taller buttons,
// fields, tabs, and list rows — so every widget keeps native rendering
// instead of going through Qt stylesheets (which replace the native
// drawing of whatever they touch).
class AthenaStyle : public QProxyStyle {
    Q_OBJECT

public:
    explicit AthenaStyle(QStyle *baseStyle);

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize,
                           const QWidget *widget) const override;

    // "Breeze" if that style plugin is installed, else "Fusion" — the
    // style forced Light/Dark modes paint with (both draw strictly from
    // QPalette, so an explicit palette fully controls their colors).
    static QString preferredStyleName();

    // KDE's own Breeze Light / Breeze Dark color schemes.
    static QPalette breezeLightPalette();
    static QPalette breezeDarkPalette();

    // Turns a left-hand navigation list into a Discover-style sidebar:
    // borderless, painted in the window color rather than as a white/black
    // input field, with list-sized icons.
    static void makeSidebar(QAbstractItemView *view);

    // Gap between a sidebar and the content beside it, applied as a
    // margin on both sides of a hairline splitter handle (see
    // makeSidebarSplitter), so neither side's edge crowds the divider.
    static constexpr int kSidebarGap = 16;

    // A sidebar|content splitter drawn the way Discover separates its
    // sidebar: a 1px divider line instead of a grabbable bar.
    static void makeSidebarSplitter(class QSplitter *splitter);

    // Wraps a sidebar in a container that holds it kSidebarGap away from
    // the divider on its right, so its scrollbar doesn't sit on the line.
    static QWidget *wrapSidebar(QWidget *sidebar, QWidget *parent);
};
