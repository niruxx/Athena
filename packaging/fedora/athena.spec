# Packages an already-built Athena: build-in-podman.sh compiles it with
# cargo and stages it under %{_stage} with packaging/install.sh, so this
# only collects those files.
Name:           athena
Version:        @VERSION@
Release:        1%{?dist}
Summary:        Package manager for DNF, Flatpak, and Snap
License:        GPL-3.0-or-later
URL:            https://github.com/niruxx/Athena

Requires:       gtk4 >= 4.14
Requires:       libadwaita >= 1.5
Requires:       polkit
# pkexec's password prompt needs an agent; any one will do.
Requires:       (polkit-gnome or polkit-kde or lxqt-policykit or mate-polkit or xfce-polkit)
Suggests:       flatpak
Suggests:       snapd

%description
Install, update, and remove software from every source on the system in
one GTK app: searches all of them at once, lists everything installed, and
runs each change in a window with its progress and full output.

%install
cp -a %{_stage}/. %{buildroot}/
install -Dm644 %{_license_file} %{buildroot}%{_licensedir}/%{name}/LICENSE

%files
%license %{_licensedir}/%{name}/LICENSE
%{_bindir}/athena
%{_datadir}/applications/io.github.niruxx.Athena.desktop
%{_datadir}/icons/hicolor/*/apps/io.github.niruxx.Athena.*
