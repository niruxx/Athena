# Tools and recovery

Phase 6 adds a Home dashboard and a Tools destination. All package-manager reads,
filesystem inventories and backup operations run away from the GTK thread.
Home loads sources independently and refreshes after package changes. Its installed
counts follow each source: AUR entries are also counted by pacman.

## Free Up Space

Native download-cache inventories cover pacman, APT and DNF. The size is an upper
bound, with unreadable paths counted separately. Pacman prefers `paccache -r`
(its normal retained-version policy), falling back to `pacman -Sc --noconfirm`.
APT uses `apt-get clean`; DNF uses `dnf clean all`. Unused-dependency inspections
show native manager plans, including installed-size details for pacman. Flatpak
previews explicitly answer **no** to each installation's proposed unused-runtime
removal and show runtime sizes. Flatpak has no `--assumeno` flag, and
`--noninteractive` implies consent, so it must never be used for this preview.

User cleanup includes owned regular files in `/tmp` older than seven days, the
XDG cache directory, Trash, thumbnails, Bash/Zsh/Fish history and the recent-file
list. Nothing is selected automatically. A mandatory preview lists the paths and
sizes. Cleanup does not follow links or cross mounts, verifies ownership, inode,
device, size and modification time again, and anchors deletion to open directory
descriptors. Changed files stop the operation. Empty directories remain. Apps
should be closed first; deleting an open cache file need not immediately free its
space. Trash/history removal is permanent, and running shells may rewrite history.

## Backups

Choose hidden home files/folders or `.var/app` for Flatpak data. `.config` is the
only default selection. Close affected apps before making a consistent copy.
The backup also contains the enabled sources' package manifest. A failed source
query is reported rather than silently exporting an incomplete package list.

Both formats have `athena-backup.json` (version, file list, skipped paths, package
choices) and a `home/` namespace. Compressed backups are gzip-compressed tar files;
plain backups are directory trees. Existing backup destinations are refused.
Archives and plain-backup roots are private to the user. Regular-file permission
bits are preserved; ownership, special permission bits, ACLs and extended attributes
are not. Symbolic links and special files are skipped and reported. Empty
directories are not stored. These are file backups, not encrypted disk images.

Restore previews enumerate every file as CREATE or REPLACE and compute SHA256
hashes. Incomplete file lists, duplicate paths, traversal, links and special entries
are rejected. A second inspection must match the approved preview. Restore opens
parents without following links, writes private temporary files, checks hashes,
and atomically renames each file into place. Failure can leave earlier files
restored; this is not an atomic operation across the whole backup. The package
list is separate: use **Apps from backup** to select missing applications through
the normal import flow. Restoring configuration never silently installs software.

## Package manifests

JSON version 1 contains `source`, `name`, `repository` and `reference` for each
choice. Pacman uses explicit install reasons, APT uses manual marks intersected
with installed packages, and DNF uses user-installed packages. Native architecture
qualifiers are retained. Pacman foreign packages are retained as AUR candidates;
local-only packages may need their original build/artifact when no AUR entry exists.
Their presence is checked against pacman's installed database too.

Flatpak records app refs including architecture/branch, origin and user/system/named
installation scope. Import requires an existing remote with that name; arbitrary
URLs and mismatched app refs are rejected. Snap's read-only `/v2/snaps` API exports
application snaps, leaving base/kernel/gadget/snapd dependencies to Snap.

Import shows missing entries with switches initially off. Unavailable sources are
shown and disabled. Selected scoped Flatpak commands and native operations form
one serial batch; AUR choices always enter mandatory build-file review after that
batch succeeds. There are no shell commands in a manifest. Versions are not pinned.

## Snapshots

Install and configure a provider first. Athena prefers Snapper if both executables
are present and uses its `root` configuration; otherwise it uses Timeshift's
existing configuration. The Preferences switch **Take one system snapshot before
each transaction batch** defaults off. When enabled, one snapshot command is
prepended to a serial package batch, including an AUR build. Continuing into AUR
review does not create another snapshot. Downloads and metadata/cache maintenance
are excluded. A missing provider or failed snapshot stops the transaction.

Snapper listing is unprivileged and follows configured access permissions. The
UI offers snapshot creation, deletion, Btrfs rollback and immediate undo of changes
between the chosen snapshot and the current filesystem. Rollback requires a
compatible root layout and a restart to boot the new default subvolume.

Timeshift listing is only started by **List as Administrator**. Home never prompts
for access; after an authorized list it can show the latest snapshot from that
check. Restore opens the upstream `timeshift-launcher`: select the displayed
snapshot in Timeshift, then use its restore wizard for disk and bootloader choices.
The GTK launcher does not accept CLI snapshot/restore arguments. System snapshot
coverage is determined by the provider configuration and may exclude home data.

Upstream command references: [Snapper manual source](https://github.com/openSUSE/snapper/blob/master/doc/snapper.xml.in),
[Timeshift CLI](https://github.com/linuxmint/timeshift/blob/master/src/AppConsole.vala),
[Timeshift GTK](https://github.com/linuxmint/timeshift/blob/master/src/AppGtk.vala),
[Flatpak uninstall](https://github.com/flatpak/flatpak/blob/main/app/flatpak-builtins-uninstall.c),
[Snap raw API command](https://github.com/canonical/snapd/blob/master/cmd/snapd/cli/cmd_debug_api.go).

## Validation

Unit tests cover both backup formats, hostile archives, changed backup contents,
restoration through a linked parent, changed cleanup files/parents, malformed
manifests, scoped refs, snapshot identifiers/listings and mutation classification.
The read-only `cargo run -p athena-core --example tools_probe` round-tripped 188
installed choices on the development Arch/Flatpak system with no missing entries.
Wide/narrow GTK Broadway smoke checks exercise Home and every Tools destination.

Real package removals, personal-file cleanup and system snapshot restoration are
not run by tests. Snapper/Timeshift are absent on the development machine, so their
commands and parsers are verified against upstream sources and fixtures; full
provider integration still requires an appropriately configured test system.
