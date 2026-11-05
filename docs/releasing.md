# Desktop integration and releasing 4.0.0

Version 4.0.0 is prepared in Cargo, native packaging and AppStream metadata.
No tag, GitHub release or Flathub submission is created by the local implementation.
The roadmap keeps publication and unverified distribution formats open.

## Background checks

Preferences has an opt-in switch and a 15–1440 minute interval, defaulting to four
hours. Athena requests `org.freedesktop.portal.Background.RequestBackground`
before starting checks or its tray. The portal manages login startup using the
installed executable (or persistent AppImage path) with `--background`. Moving an
AppImage requires re-enabling the preference to update its login command.

Denied or unavailable portal access disables the preference and reports the error.
Disabling during a pending request closes it. Disabling after approval requests
removal of portal-managed login startup. A portal may grant background permission
without granting autostart; login behavior follows the desktop's response.

Checks run on workers using the enabled sources' read-only update queries. They
use each backend's metadata behavior, so a source with stale local metadata may
need a normal foreground refresh. They never install packages or prompt for
administrator access. Identical package/version results do not repeatedly notify.
Notifications and tray actions open Athena's existing review/confirmation flow.

A temporary application hold keeps a pending consent request alive. Approval keeps
the app running after the window closes; Quit ends it. The tray exports
StatusNotifierItem and DBusMenu only while background checks are enabled and
registers with a watcher when it appears. GNOME does not require a tray.

## Checks

```
cargo test --locked --workspace
cargo clippy --locked --workspace -- -D warnings
cargo build --release --locked -p athena
appstreamcli validate --no-net data/io.github.niruxx.Athena.metainfo.xml
desktop-file-validate data/io.github.niruxx.Athena.desktop
```

The ignored portal/tray integration test uses a private bus and a GTK display;
it must never run against a real session's service names:

```
ATHENA_PRIVATE_BUS_TEST=1 GDK_BACKEND=x11 GSK_RENDERER=cairo \
  xvfb-run -a dbus-run-session -- cargo test --locked -p athena \
  portal_consent_cancellation_and_tray_wire_protocol -- --ignored --test-threads=1
```

It covers portal grant, denial, cancellation/Close, watcher registration,
DBusMenu's layout signature, tray properties and the Update action. The local
Broadway run passed. Screenshots were refreshed with the debug harness; a Spanish
catalog was compiled and exercised in the running interface. Native package
staging validates schema, metainfo, desktop entry and compiled catalogs.

## Distribution artifacts

Native build scripts remain in `packaging/{arch,debian,fedora}`. They install the
same metadata and catalogs using `packaging/install.sh`. Debian installs Rust
1.92 in its disposable container because the distribution compiler can be older
than the GTK bindings' minimum. Building these packages requires the existing
container tooling; no host packages are installed by validation.

`packaging/appimage/build.sh` and the CI job target **Ubuntu 24.04 x86_64**. The
upstream GTK plugin, linuxdeploy and their checksums are pinned in `tools.json`.
The wrapper preserves Wayland selection and libadwaita appearance. Host package
commands have the bundle's libraries/module paths removed so they use host
libraries. The internal reviewed-AUR runner keeps its own bundle environment.

The development Arch machine's newer GdkPixbuf uses Glycin helpers and lacks the
legacy module directory expected by linuxdeploy's GTK plugin. The local build
failed there. Do not label an AppImage validated until the Ubuntu CI artifact
passes command-line and GUI checks on that baseline and another supported distro.
The script rejects other build hosts rather than emitting a misleading artifact.

The [Flathub assessment](../packaging/flatpak/README.md) records the remaining
host-companion work and external permission review. There is no working Flatpak
artifact or Flathub approval yet.

## Publication

After CI artifacts pass and the remaining integration limits are accepted, review
`CHANGELOG.md`, the metainfo release date and version consistency, then commit and
tag `v4.0.0`. Create the GitHub release with verified artifacts and checksums.
This is a separate publication step; these instructions do not publish anything.
