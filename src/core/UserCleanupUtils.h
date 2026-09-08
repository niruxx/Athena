#pragma once

#include <QString>

// Helpers for the User Management > General/Advanced cleanup tabs. Every
// path these touch is cleared without pkexec (plain file I/O the user
// already owns), so — unlike /tmp, which is shared with every other user
// on the system — ownership is checked before anything is removed rather
// than assuming the whole tree belongs to the current user.
namespace UserCleanupUtils {

// Sum of the sizes of files under `path` that are owned by the current
// user. For a plain file, its own size (or 0 if not owned). For a
// directory, only descends into entries owned by the current user at each
// level, so another user's subtree under a shared directory (e.g. /tmp)
// isn't sized at all.
qint64 ownedSize(const QString &path);

// Removes `path` if it's a file owned by the current user, or — if it's a
// directory — removes every top-level entry inside it that's owned by the
// current user (recursively), leaving the directory itself and any
// not-owned entries in place. Returns false if any owned entry could not
// be removed.
bool clearOwnedPath(const QString &path);

} // namespace UserCleanupUtils
