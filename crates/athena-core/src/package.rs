//! The data the backends report: packages, groups, repositories, and what a
//! transaction would do.

/// One package (or Flatpak app, or snap) as a backend reports it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PackageInfo {
    /// What install/remove act on: a package name, Flatpak app ID, or snap name.
    pub name: String,
    /// Optional source-specific install reference, e.g. a Flatpak branch/architecture.
    pub install_ref: String,
    /// Empty when not installed.
    pub installed_version: String,
    /// The newest version known to be available; empty when unknown.
    pub available_version: String,
    pub architecture: String,
    pub repository: String,
    /// A short, one-line summary.
    pub description: String,
    /// The full description, when the backend fetched it.
    pub long_description: String,
    /// Human-readable ("12.3 MB"); empty when the backend doesn't report one.
    pub size: String,
    pub homepage_url: String,
    pub installed: bool,
}

/// A package group (pacman group, dnf group or environment, tasksel task).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PackageGroupInfo {
    pub id: String,
    pub name: String,
    pub description: String,
    /// A group of groups (a dnf environment, a tasksel task).
    pub is_meta: bool,
    pub installed: bool,
    pub packages: Vec<String>,
    pub sub_groups: Vec<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct RepositoryInfo {
    /// Opaque: routes a toggle back to this repository.
    pub id: String,
    pub name: String,
    pub url: String,
    pub enabled: bool,
}

/// One field of a backend's "add repository" form.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepositoryAddField {
    /// The key its value comes back under in `add_repository`.
    pub key: &'static str,
    pub label: &'static str,
    pub placeholder: &'static str,
    pub required: bool,
}

/// What an install or removal would do, worked out without doing it, for
/// the confirmation dialog. `available` is false when the backend couldn't
/// work it out; the dialog then shows a plain summary.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TransactionPreview {
    pub available: bool,
    pub plan_text: String,
}

/// Whether an operation that ran to completion in the background worked,
/// with its output for an error message when it didn't.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OperationResult {
    pub success: bool,
    pub output: String,
}

impl OperationResult {
    pub fn failed(message: impl Into<String>) -> Self {
        Self {
            success: false,
            output: message.into(),
        }
    }
}
