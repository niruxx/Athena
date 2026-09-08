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

inline QIcon folder()
{
    return QIcon::fromTheme(QStringLiteral("folder"), qApp->style()->standardIcon(QStyle::SP_DirIcon));
}

inline QIcon download()
{
    return QIcon::fromTheme(QStringLiteral("folder-download"),
                             qApp->style()->standardIcon(QStyle::SP_ArrowDown));
}

inline QIcon trash()
{
    return QIcon::fromTheme(QStringLiteral("user-trash"), qApp->style()->standardIcon(QStyle::SP_TrashIcon));
}

inline QIcon temporary()
{
    return QIcon::fromTheme(QStringLiteral("folder-temp"), qApp->style()->standardIcon(QStyle::SP_DirIcon));
}

inline QIcon cache()
{
    return QIcon::fromTheme(QStringLiteral("preferences-system-time"),
                             qApp->style()->standardIcon(QStyle::SP_DriveHDIcon));
}

inline QIcon image()
{
    return QIcon::fromTheme(QStringLiteral("image-x-generic"),
                             qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView));
}

inline QIcon terminal()
{
    return QIcon::fromTheme(QStringLiteral("utilities-terminal"),
                             qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView));
}

inline QIcon history()
{
    return QIcon::fromTheme(QStringLiteral("document-open-recent"),
                             qApp->style()->standardIcon(QStyle::SP_FileIcon));
}

inline QIcon archive()
{
    return QIcon::fromTheme(QStringLiteral("package-x-generic"),
                             qApp->style()->standardIcon(QStyle::SP_DriveFDIcon));
}

} // namespace AppIcons
