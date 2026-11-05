# Catalog, AUR review, and source tools

Phases 3–5 extend the GTK app without adding GTK dependencies to `athena-core`.
The core owns catalog parsing, RPC, dependency planning, review verification,
and command construction. GTK pages call blocking work on worker threads.

## AppStream implementation choice

We considered libappstream's GObject pool and its `appstreamcli` interface against
an in-process parser. Libappstream already understands distribution metadata,
but using its introspection/bindings would introduce another native build
requirement and binding-version constraint. Athena currently uses its own
fixture-tested XML and DEP-11 parser so it can retain Flatpak remote,
architecture, and installation scope explicitly in each offer.

The format references are the upstream [catalog specification](https://www.freedesktop.org/software/appstream/docs/chap-CatalogData.html)
and [DEP-11 specification](https://www.freedesktop.org/software/appstream/docs/sect-AppStream-YAML.html).
Catalogs are read from the standard swcatalog/app-info directories, APT's DEP-11
lists, and Flatpak appstream directories for system, user, and named installations.
Gzip inputs are supported. Missing native catalogs can be installed from
Discover's catalog-support button, then refreshed through Repositories.

Application IDs, provided IDs, desktop launchers, and package names form the
merge index. Native package names can link matching upstream component IDs;
known `-bin` variants share the upstream card. Titles alone do not merge apps.
Catalog descriptions and icons also decorate Installed and Updates.

JPEG XL icons use an installed GdkPixbuf loader when available. If loading fails,
Athena can decode them with `djxl` into its PNG cache. Distributions offer this
through libjxl/libjxl-tools. A missing decoder leaves a theme-icon fallback.
The full catalog is local; icon and screenshot downloads require HTTPS and are
cached. Individual malformed catalogs are skipped and recorded as warnings.

## AUR review boundary

AUR searches and package info use [RPC v5](https://aur.archlinux.org/rpc/).
Installed foreign packages are matched to RPC results, and update versions are
compared with `vercmp`. Dependency planning uses RPC and `.SRCINFO`, checks
installed/native providers first, and orders AUR build units before their users.
Ambiguous AUR virtual providers stop planning with an explicit error.

Fetching does not execute PKGBUILD, `makepkg --printsrcinfo`, repository hooks,
or source filters. Git's system/global configuration is disabled for source
fetching. Submodules, symlinks, binary files, and oversized reviews are rejected.
Git metadata is removed before review. Every remaining build input is displayed
and editable; changing a file clears its reviewed state.

All files must be marked reviewed before approval. Previously approved copies
are kept in the user's state directory, with unified diffs on subsequent reviews.
Risk markers are prompts for review, not a script-safety assessment.

The approval receipt hashes the entire manifest. Each input also has a SHA-256
hash. The runner checks the receipt and exact file set before executing anything,
consumes the approval once, and checks each build unit again immediately before
building it. Extra files, changed files, symlinks, and reintroduced git metadata
stop execution.

Builds always use the reviewed local copy with the built-in `makepkg` executor.
A helper is never allowed to refetch or substitute a different PKGBUILD after
approval. Paru/yay can themselves be installed, updated, and removed in Arch
and Mirrors, with installation going through the same review pipeline.
`makepkg` uses a generated configuration with
`PACMAN_AUTH=(pkexec --disable-internal-agent)`; native dependencies and package
installation use polkit. AUR dependencies are installed as dependencies while
preserving existing explicit installation reasons, including split packages.
The [makepkg manual](https://man.archlinux.org/man/makepkg.8.en) and
[configuration manual](https://man.archlinux.org/man/makepkg.conf.5.en) describe
these interfaces. Builds run as the user and are not an isolation sandbox.

Individual installs, reinstalls, updates, and batches all pass through review.
Update Everything completes supported-source updates first, then opens AUR
review. The skip-confirmations preference never skips AUR review.

## News, mirrors, and source tools

Home contains unread [Arch news](https://archlinux.org/feeds/). Full Arch updates
check news first and offer Read First or an explicit Update Anyway response.
A failed news check also requires an explicit override. The rest of the Home
dashboard remains Phase 6 work.

Reflector/rate-mirrors generate a user-owned preview. Manjaro and Artix use their
own rate-mirrors targets; unknown distributions are refused rather than replacing
their repositories with Arch mirrors. Applying it backs up the
existing mirror list before installation. DNF's fastestmirror option is exposed
separately.

Flatpak's page combines permissions, per-app data size/open/clear controls, and
leftovers from removed apps. Data measurement does not follow symlinks. Clearing
is restricted to a valid application directory, checks running Flatpak apps,
and requires a destructive confirmation. Leftover user overrides can be reset.
Snap Manage exposes channel refresh, revision revert, enable/disable, classic
confinement information, and [interface connections](https://snapcraft.io/docs/how-to-guides/manage-snaps/connect-interfaces/).
All package-manager changes use the terminal transaction UI.

## Validation

Run `cargo test --locked --workspace` and
`cargo clippy --locked --workspace -- -D warnings`.
The read-only `aur_probe` example prepares reviews without approving or building:

```sh
XDG_CACHE_HOME=/tmp/athena-aur-probe-cache \
XDG_STATE_HOME=/tmp/athena-aur-probe-state \
cargo run --locked -p athena-core --example aur_probe -- paru
```

Debug builds support `ATHENA_SCREENSHOTS=<folder>` with
`ATHENA_SCREENSHOT_STEPS=installed,discover,flatpak,snap,arch,home,aur-review,narrow-discover,narrow-flatpak,narrow-arch,narrow-review`.
The AUR review screenshot uses a non-buildable preview fixture; it does not fetch
or execute a package. Live installs, destructive cleanup, and mirror changes
require their own distribution-level integration testing.
