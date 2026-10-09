#include "AthenaStyle.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QSplitter>
#include <QStyleFactory>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Breeze's own defaults are 6px child margins and 6px spacing.
constexpr int kWindowMargin = 16;
constexpr int kChildMargin = 12;
constexpr int kSpacing = 10;

QPalette makePalette(const QColor &window, const QColor &view, const QColor &button, const QColor &text,
                     const QColor &disabledText, const QColor &alternateView, const QColor &highlight)
{
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, view);
    palette.setColor(QPalette::AlternateBase, alternateView);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabledText);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::LinkVisited, QColor(155, 89, 182));

    // 3D shading roles, derived the way KDE's color schemes derive them.
    palette.setColor(QPalette::Light, button.lighter(130));
    palette.setColor(QPalette::Midlight, button.lighter(115));
    palette.setColor(QPalette::Mid, button.darker(130));
    palette.setColor(QPalette::Dark, button.darker(160));
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, 120));

    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, disabledText);
    return palette;
}

} // namespace

AthenaStyle::AthenaStyle(QStyle *baseStyle) : QProxyStyle(baseStyle) { }

int AthenaStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin:
        return (widget && widget->isWindow()) ? kWindowMargin : kChildMargin;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return kSpacing;
    case PM_ButtonIconSize:
    case PM_SmallIconSize:
        return 16;
    case PM_ListViewIconSize:
        return 22;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize AthenaStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize,
                                    const QWidget *widget) const
{
    QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    switch (type) {
    case CT_PushButton:
        size += QSize(12, 6);
        break;
    case CT_LineEdit:
    case CT_ComboBox:
    case CT_SpinBox:
        size.rheight() += 6;
        break;
    case CT_TabBarTab:
        size += QSize(16, 8);
        break;
    case CT_ItemViewItem:
        size.rheight() += 10;
        break;
    default:
        break;
    }
    return size;
}

QString AthenaStyle::preferredStyleName()
{
    for (const QString &key : QStyleFactory::keys()) {
        if (key.compare(QLatin1String("Breeze"), Qt::CaseInsensitive) == 0)
            return key;
    }
    return QStringLiteral("Fusion");
}

QPalette AthenaStyle::breezeLightPalette()
{
    return makePalette(QColor(239, 240, 241), QColor(255, 255, 255), QColor(252, 252, 252), QColor(35, 38, 41),
                       QColor(112, 125, 138), QColor(247, 247, 247), QColor(61, 174, 233));
}

QPalette AthenaStyle::breezeDarkPalette()
{
    return makePalette(QColor(32, 35, 38), QColor(20, 22, 24), QColor(41, 44, 48), QColor(252, 252, 252),
                       QColor(161, 169, 177), QColor(29, 31, 34), QColor(61, 174, 233));
}

void AthenaStyle::makeSidebarSplitter(QSplitter *splitter)
{
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
}

QWidget *AthenaStyle::wrapSidebar(QWidget *sidebar, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, kSidebarGap, 0);
    layout->addWidget(sidebar);
    return container;
}

void AthenaStyle::makeSidebar(QAbstractItemView *view)
{
    if (auto *frame = qobject_cast<QFrame *>(view))
        frame->setFrameShape(QFrame::NoFrame);
    // A role, not a fixed color, so it keeps following theme switches.
    view->viewport()->setBackgroundRole(QPalette::Window);
    view->setIconSize(QSize(22, 22));
    if (auto *tree = qobject_cast<QTreeView *>(view)) {
        // No indentation (and so no branch lines): section headers are
        // bold instead, as in Discover's sidebar.
        tree->setIndentation(0);
        tree->setRootIsDecorated(false);
    }
}
