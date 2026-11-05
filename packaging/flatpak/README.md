# Flathub readiness

Athena is not currently distributed as a Flatpak. The native and AppImage build
paths operate in the host's user session. Merely adding a Flatpak manifest and
prefixing commands with `flatpak-spawn --host` would produce an incomplete build:

- Native package databases, repositories, logs, AppStream metadata and caches
  are read directly by the core. Inside a sandbox, many of those paths refer to
  the runtime or are inaccessible. They need a host-side query interface.
- Package transactions need the host package managers and polkit agent, while
  AUR review/build receipts need exact shared paths and user ownership.
- Dotfile backup/restore, cleanup and snapshots need host paths and the same
  ownership and symlink checks as the native application.
- Desktop app launching, system Flatpak installations, and named installations
  need host-aware discovery, not the sandbox's `/usr` or application database.

The recommended next implementation is a versioned host companion exposing
structured read/query results and reviewed command plans, rather than remote
shell strings. File operations should remain in that companion so that previews
and execution share the host namespace. The UI can then remain inside the
sandbox. Builds must vendor Cargo dependencies for Flathub's offline build and
use pinned SDK/runtime versions and source commits.

Any proposed `org.freedesktop.Flatpak` access requires review. Flathub documents
[restricted criteria for Flatpak talk-name exceptions](https://docs.flathub.org/docs/for-app-authors/linter#finish-args-flatpak-talk-name).
The [Flatpak permission documentation](https://docs.flatpak.org/en/latest/sandbox-permissions.html)
explains the sandbox boundary. No permission exception has been requested or
approved in this work, and no Flathub availability is claimed.

Before submission, build and exercise all supported managers in disposable host
VMs, verify denial/cancellation paths, test the companion protocol against
untrusted input, and obtain agreement on required host access. A submission should
include the manifest, vendored sources, AppStream metadata, screenshots, and an
explicit permission justification.
