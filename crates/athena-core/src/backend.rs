//! The interface every package source implements.

use crate::package::{OperationResult, PackageGroupInfo, PackageInfo, RepositoryAddField, RepositoryInfo, TransactionPreview};
use crate::process::Command;

/// A package manager driven through its CLI. Every method blocks (they
/// shell out, sometimes over the network), so the UI calls them off the
/// main thread. Privileged changes aren't run here: the `*_commands`
/// methods say what to run, and the UI runs them in its terminal window so
/// the output and progress are visible.
pub trait Backend: Send + Sync {
    /// Stable short key: "pacman", "dnf", "apt", "flatpak", "snap".
    fn key(&self) -> &'static str;
    /// Display name, e.g. "Pacman (Arch Linux)".
    fn name(&self) -> &'static str;
    fn is_available(&self) -> bool;

    fn list_installed(&self) -> Vec<PackageInfo>;
    fn search(&self, query: &str) -> Vec<PackageInfo>;
    /// Packages that provide (`find_requires` false) or require (true) a
    /// name or capability. Backends without a safe, non-interactive query
    /// for a direction return nothing rather than guessing.
    fn dependency_query(&self, _capability: &str, _find_requires: bool) -> Vec<PackageInfo> {
        Vec::new()
    }
    /// Full details for these exact names, in as few calls as possible.
    fn package_details(&self, names: &[String]) -> Vec<PackageInfo>;

    fn list_groups(&self) -> Vec<PackageGroupInfo> {
        Vec::new()
    }
    fn group_details(&self, id: &str, is_meta: bool) -> PackageGroupInfo {
        PackageGroupInfo { id: id.to_owned(), name: id.to_owned(), is_meta, ..Default::default() }
    }

    /// One transaction, one password prompt, for any number of packages.
    fn install_commands(&self, names: &[String]) -> Vec<Command>;
    /// Also removes dependencies the packages pulled in that nothing else needs.
    fn remove_commands(&self, names: &[String]) -> Vec<Command>;
    fn reinstall_commands(&self, names: &[String]) -> Vec<Command>;
    fn list_updates(&self) -> Vec<PackageInfo>;
    /// No names means "update everything": a full system upgrade where the
    /// backend has one (`pacman -Syu`) rather than a list of package names.
    fn upgrade_commands(&self, names: &[String]) -> Vec<Command>;
    /// Dependencies installed at some point that nothing needs anymore.
    fn clean_unused_commands(&self) -> Vec<Command>;
    /// Downloads without installing. Backends that can't redirect the
    /// download (Flatpak) fetch into their own cache instead.
    fn download_commands(&self, names: &[String], destination: &str, include_dependencies: bool) -> Vec<Command>;

    /// What an install or removal would do, without privileges.
    fn preview_install(&self, _names: &[String]) -> TransactionPreview {
        TransactionPreview::default()
    }
    fn preview_remove(&self, _names: &[String]) -> TransactionPreview {
        TransactionPreview::default()
    }

    /// Re-syncs repository metadata, so a just-added repository's packages
    /// show up in searches.
    fn refresh_metadata_commands(&self) -> Vec<Command> {
        Vec::new()
    }
    /// Recent activity in the backend's own log format, newest first.
    fn recent_history(&self) -> String;

    fn list_repositories(&self) -> Vec<RepositoryInfo> {
        Vec::new()
    }
    fn set_repository_enabled(&self, _id: &str, _enabled: bool) -> OperationResult {
        OperationResult::failed("This source has no repositories to switch on or off.")
    }
    fn repository_add_fields(&self) -> Vec<RepositoryAddField> {
        Vec::new()
    }
    /// `values` pairs each [`RepositoryAddField::key`] with what was entered.
    fn add_repository(&self, _values: &[(String, String)]) -> OperationResult {
        OperationResult::failed("This source doesn't support adding repositories.")
    }
}

/// The value entered for `key` in an add-repository form.
pub(crate) fn field<'a>(values: &'a [(String, String)], key: &str) -> &'a str {
    values.iter().find(|(k, _)| k == key).map(|(_, v)| v.trim()).unwrap_or("")
}
