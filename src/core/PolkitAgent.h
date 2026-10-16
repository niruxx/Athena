#pragma once

#include <QString>

// pkexec can only ask for a password through a polkit authentication
// agent running in the user's session. Full desktops start their own
// (KDE, GNOME, Cinnamon, ...); bare compositors (niri, Hyprland, sway) and
// minimal installs often don't, and then every privileged action fails
// without a prompt. This finds a running agent and, when there's none,
// starts an installed one for the rest of the session.
namespace PolkitAgent {

// Whether a known agent (or a desktop shell with one built in) is running
// as this user.
bool isRunning();

// The first agent program installed on this system, or empty.
QString installedAgent();

// Starts installedAgent() if no agent is running. Returns true if an agent
// is running afterwards (or was just started). Cheap enough to call
// before every privileged command.
bool ensureRunning();

} // namespace PolkitAgent
