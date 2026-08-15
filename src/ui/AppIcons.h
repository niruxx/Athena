#pragma once

#include <QApplication>
#include <QIcon>
#include <QStyle>

// Centralized icon choices so the same action (refresh, install, ...)
// looks the same everywhere it appears, rather than each page picking its
// own. Freedesktop theme icon names first (native look on Linux desktops),
// falling back to Qt's built-in standard icons so buttons still get some
// icon on minimal/icon-less setups instead of showing nothing.
namespace AppIcons {

inline QIcon refresh()
{
    return QIcon::fromTheme(QStringLiteral("view-refresh"), qApp->style()->standardIcon(QStyle::SP_BrowserReload));
}

inline QIcon install()
{
    return QIcon::fromTheme(QStringLiteral("list-add"), qApp->style()->standardIcon(QStyle::SP_DialogApplyButton));
}

inline QIcon uninstall()
{
    return QIcon::fromTheme(QStringLiteral("list-remove"),
                             qApp->style()->standardIcon(QStyle::SP_DialogCancelButton));
}

inline QIcon reinstall()
{
    return QIcon::fromTheme(QStringLiteral("document-revert"),
                             qApp->style()->standardIcon(QStyle::SP_BrowserReload));
}

inline QIcon update()
{
    return QIcon::fromTheme(QStringLiteral("system-software-update"),
                             qApp->style()->standardIcon(QStyle::SP_ArrowUp));
}

inline QIcon search()
{
    return QIcon::fromTheme(QStringLiteral("edit-find"),
                             qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView));
}

inline QIcon clean()
{
    return QIcon::fromTheme(QStringLiteral("edit-clear-all"), qApp->style()->standardIcon(QStyle::SP_TrashIcon));
}

inline QIcon addItem()
{
    return QIcon::fromTheme(QStringLiteral("list-add"),
                             qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder));
}

inline QIcon selectAll()
{
    return QIcon::fromTheme(QStringLiteral("edit-select-all"));
}

inline QIcon selectNone()
{
    return QIcon::fromTheme(QStringLiteral("edit-clear"));
}

} // namespace AppIcons
